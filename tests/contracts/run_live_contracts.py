#!/usr/bin/env python3
"""Run Bloom's MediaBrowser contract probes against an operator-owned server."""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from validate_contracts import load_and_validate


@dataclass
class Response:
    status: int
    headers: dict[str, str]
    body: bytes

    def json(self):
        if not self.body:
            return None
        try:
            return json.loads(self.body)
        except json.JSONDecodeError:
            return None


@dataclass
class ProbeResult:
    contract: str
    expected: str
    observed: str
    passed: bool
    evidence: str


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Keep credentials on the configured origin by refusing automatic redirects."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HttpTransport:
    def __init__(self, base_url: str, timeout: float):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self._base_origin = self._origin(self.base_url)
        self._opener = urllib.request.build_opener(NoRedirectHandler())

    @staticmethod
    def _origin(url: str):
        parsed = urllib.parse.urlsplit(url)
        scheme = parsed.scheme.lower()
        hostname = (parsed.hostname or "").lower()
        port = parsed.port or (443 if scheme == "https" else 80 if scheme == "http" else None)
        return scheme, hostname, port

    def is_same_origin(self, url: str):
        return not url.startswith(("http://", "https://")) or self._origin(url) == self._base_origin

    def request(
        self,
        method: str,
        path: str,
        *,
        headers: dict[str, str] | None = None,
        json_body: Any = None,
        raw_body: bytes | None = None,
    ):
        url = path if path.startswith(("http://", "https://")) else self.base_url + path
        body = raw_body
        request_headers = dict(headers or {})
        if json_body is not None:
            body = json.dumps(json_body).encode("utf-8")
            request_headers.setdefault("Content-Type", "application/json")
        request = urllib.request.Request(url, data=body, headers=request_headers, method=method)
        try:
            with self._opener.open(request, timeout=self.timeout) as reply:
                return Response(reply.status, dict(reply.headers.items()), reply.read())
        except urllib.error.HTTPError as error:
            return Response(error.code, dict(error.headers.items()), error.read())
        except (urllib.error.URLError, TimeoutError) as error:
            return Response(0, {"X-Bloom-Transport-Error": str(error)}, b"")


class MediaBrowserV1Probe:
    """The current Bloom wire surface. Other protocol drivers can be added beside it."""

    def __init__(self, transport: HttpTransport, username: str, password: str, version: str):
        self.transport = transport
        self.username = username
        self.password = password
        self.version = version
        self.token = ""
        self.user_id = ""
        self.variables: dict[str, str] = {}

    def headers(self, token: str | None = None, device_id: str = "bloom-contract-baseline"):
        auth = (
            f'MediaBrowser Client="Bloom Contract", Device="Desktop", '
            f'DeviceId="{device_id}", Version="{self.version}"'
        )
        effective_token = self.token if token is None else token
        if effective_token:
            auth += f', Token="{effective_token}"'
        return {"Authorization": auth, "Content-Type": "application/json"}

    def request(self, method: str, path: str, **kwargs: Any):
        headers = dict(self.headers()) if self.transport.is_same_origin(path) else {}
        headers.update(kwargs.pop("headers", {}))
        return self.transport.request(method, path, headers=headers, **kwargs)

    @staticmethod
    def _items(payload: Any):
        if isinstance(payload, list):
            return payload
        if isinstance(payload, dict) and isinstance(payload.get("Items"), list):
            return payload["Items"]
        return []

    @staticmethod
    def _outcome_for_missing(response: Response):
        return "missing" if response.status in {404, 405, 501} else "partial"

    def follow_redirect_once(self, response: Response, original_path: str, *, headers: dict[str, str] | None = None):
        if response.status not in {301, 302, 303, 307, 308}:
            return response
        location = response.headers.get("Location")
        if not location:
            return response
        original_url = urllib.parse.urljoin(self.transport.base_url + "/", original_path)
        target = urllib.parse.urljoin(original_url, location)
        return self.request("GET", target, headers=headers or {})

    def authenticate(self):
        response = self.transport.request(
            "POST",
            "/Users/AuthenticateByName",
            headers=self.headers(token=""),
            json_body={"Username": self.username, "Pw": self.password},
        )
        try:
            payload = response.json() or {}
        except json.JSONDecodeError:
            payload = {}
        self.token = payload.get("AccessToken", "") if isinstance(payload, dict) else ""
        user = payload.get("User", {}) if isinstance(payload, dict) else {}
        self.user_id = user.get("Id", "") if isinstance(user, dict) else ""
        if self.token and self.user_id and user.get("Name"):
            return "supported", f"authenticated profile {user['Name']!r} with a stable user id"
        return self._outcome_for_missing(response), f"login returned HTTP {response.status} without required identity fields"

    def run(self, expected: dict[str, str], allow_mutations: bool):
        results: list[ProbeResult] = []

        def record(contract: str, observed: str, evidence: str):
            wanted = expected[contract]
            passed = observed == "inconclusive" or observed == wanted or (wanted == "partial" and observed == "supported")
            results.append(ProbeResult(contract, wanted, observed, passed, evidence))

        outcome, evidence = self.authenticate()
        record("auth.login", outcome if outcome != "supported" else expected["auth.login"], evidence)
        if not self.token or not self.user_id:
            return results

        invalid = self.transport.request(
            "POST",
            "/Users/AuthenticateByName",
            headers=self.headers(token=""),
            json_body={"Username": self.username, "Pw": self.password + "-invalid-contract-probe"},
        )
        record("auth.invalid", "supported" if invalid.status == 401 else "partial", f"invalid login returned HTTP {invalid.status}")

        validation = self.request("GET", f"/Users/{urllib.parse.quote(self.user_id)}")
        valid_payload = validation.json() if validation.status == 200 else {}
        record(
            "auth.validate",
            "supported" if isinstance(valid_payload, dict) and valid_payload.get("Id") == self.user_id else "partial",
            f"session validation returned HTTP {validation.status} with matching identity={isinstance(valid_payload, dict) and valid_payload.get('Id') == self.user_id}",
        )

        expired = self.request("GET", f"/Users/{urllib.parse.quote(self.user_id)}", headers=self.headers(token=self.token + "-revoked"))
        record("errors.expired-session", "supported" if expired.status == 401 else "partial", f"invalid token returned HTTP {expired.status}")

        views_response = self.request("GET", f"/Users/{urllib.parse.quote(self.user_id)}/Views")
        views = self._items(views_response.json() if views_response.status == 200 else None)
        library = next((item for item in views if isinstance(item, dict) and item.get("Id")), None)
        record("catalog.views", "supported" if library else "partial", f"found {len(views)} visible views")
        if library:
            self.variables["libraryId"] = str(library["Id"])

        query = {
            "Recursive": "true",
            "IncludeItemTypes": "Movie,Episode",
            "SortBy": "SortName",
            "SortOrder": "Ascending",
            "StartIndex": "0",
            "Limit": "5",
            "Fields": "Overview,ProviderIds,UserData,ImageTags,BackdropImageTags,MediaSources,Chapters,Trickplay",
            "EnableImages": "true",
        }
        if library:
            query["ParentId"] = str(library["Id"])
        items_path = f"/Users/{urllib.parse.quote(self.user_id)}/Items?{urllib.parse.urlencode(query)}"
        items_response = self.request("GET", items_path)
        items_payload = items_response.json() if items_response.status == 200 else None
        items = self._items(items_payload)
        total_count = items_payload.get("TotalRecordCount") if isinstance(items_payload, dict) else None
        item = next((entry for entry in items if isinstance(entry, dict) and entry.get("Id")), None)
        first_ids = [str(entry["Id"]) for entry in items if isinstance(entry, dict) and entry.get("Id")]
        first_names = [str(entry.get("Name", "")).casefold() for entry in items if isinstance(entry, dict)]
        paging_ok = len(first_ids) == len(set(first_ids)) and first_names == sorted(first_names)
        second_count = 0
        if isinstance(total_count, int) and total_count > len(items):
            second_query = dict(query)
            second_query["StartIndex"] = str(len(items))
            second_path = f"/Users/{urllib.parse.quote(self.user_id)}/Items?{urllib.parse.urlencode(second_query)}"
            second_response = self.request("GET", second_path)
            second_items = self._items(second_response.json() if second_response.status == 200 else None)
            second_ids = [str(entry["Id"]) for entry in second_items if isinstance(entry, dict) and entry.get("Id")]
            second_count = len(second_items)
            paging_ok = paging_ok and second_response.status == 200 and not set(first_ids).intersection(second_ids)
        item_contract = expected["catalog.items"] if item is not None and isinstance(total_count, int) and paging_ok else "missing"
        record("catalog.items", item_contract, f"first page has {len(items)} items, second page has {second_count}, TotalRecordCount={total_count!r}, unique/sorted pages={paging_ok}")
        if not item:
            return results

        item_id = str(item["Id"])
        title = str(item.get("Name") or "")
        self.variables["itemId"] = item_id
        item_type = str(item.get("Type") or "")
        if item_type == "Episode":
            self.variables["episodeId"] = item_id
        if item.get("SeriesId"):
            self.variables["seriesId"] = str(item["SeriesId"])

        detail_path = f"/Users/{urllib.parse.quote(self.user_id)}/Items/{urllib.parse.quote(item_id)}?Fields=Overview,ProviderIds,People,MediaSources,Chapters,Trickplay"
        detail_response = self.request("GET", detail_path)
        detail = detail_response.json() if detail_response.status == 200 else {}
        detail_ok = isinstance(detail, dict) and detail.get("Id") == item_id and detail.get("Type") and detail.get("Name")
        record("catalog.details", expected["catalog.details"] if detail_ok else "missing", f"detail required fields present={bool(detail_ok)}")

        latest_query = {"Limit": "10", "Fields": "UserData,ProviderIds"}
        if library:
            latest_query["ParentId"] = str(library["Id"])
        latest = self.request("GET", f"/Users/{urllib.parse.quote(self.user_id)}/Items/Latest?{urllib.parse.urlencode(latest_query)}")
        latest_items = self._items(latest.json() if latest.status == 200 else None)
        latest_ids = [str(entry["Id"]) for entry in latest_items if isinstance(entry, dict) and entry.get("Id")]
        latest_ok = bool(latest_ids) and len(latest_ids) == len(latest_items) == len(set(latest_ids))
        record("catalog.latest", "supported" if latest_ok else "inconclusive" if latest.status == 200 else "missing", f"latest returned {len(latest_items)} items with stable unique ids={latest_ok}")

        next_up = self.request("GET", f"/Shows/NextUp?{urllib.parse.urlencode({'UserId': self.user_id, 'Limit': '10', 'Fields': 'UserData,SeriesId'})}")
        next_items = self._items(next_up.json() if next_up.status == 200 else None)
        next_shape_ok = bool(next_items) and next_up.status == 200 and all(
            isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Episode"
            for entry in next_items
        )
        record("catalog.next-up", "supported" if next_shape_ok else "inconclusive" if next_up.status == 200 else "partial", f"Next Up returned HTTP {next_up.status} and {len(next_items)} usable episode items")

        next_episode = next(
            (entry for entry in next_items if isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Episode"),
            None,
        )
        if next_episode:
            self.variables["episodeId"] = str(next_episode["Id"])
        elif "episodeId" not in self.variables:
            episode_query = {"IncludeItemTypes": "Episode", "Recursive": "true", "Limit": "1"}
            episode_response = self.request(
                "GET",
                f"/Users/{urllib.parse.quote(self.user_id)}/Items?{urllib.parse.urlencode(episode_query)}",
            )
            episode_items = self._items(episode_response.json() if episode_response.status == 200 else None)
            episode = next(
                (entry for entry in episode_items if isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Episode"),
                None,
            )
            if episode:
                self.variables["episodeId"] = str(episode["Id"])
            else:
                series_query = {"IncludeItemTypes": "Series", "Recursive": "true", "Limit": "1"}
                series_response = self.request(
                    "GET",
                    f"/Users/{urllib.parse.quote(self.user_id)}/Items?{urllib.parse.urlencode(series_query)}",
                )
                series_items = self._items(series_response.json() if series_response.status == 200 else None)
                series = next(
                    (entry for entry in series_items if isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Series"),
                    None,
                )
                if series:
                    self.variables["seriesId"] = str(series["Id"])
                    episodes_response = self.request(
                        "GET",
                        f"/Shows/{urllib.parse.quote(str(series['Id']))}/Episodes?{urllib.parse.urlencode({'UserId': self.user_id, 'Limit': '1'})}",
                    )
                    episodes = self._items(episodes_response.json() if episodes_response.status == 200 else None)
                    episode = next(
                        (entry for entry in episodes if isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Episode"),
                        None,
                    )
                    if episode:
                        self.variables["episodeId"] = str(episode["Id"])

        search_term = title.split()[0] if title else "contract"
        search_query = {"SearchTerm": search_term, "IncludeItemTypes": "Movie,Series,Episode", "Recursive": "true", "Limit": "20"}
        search = self.request("GET", f"/Users/{urllib.parse.quote(self.user_id)}/Items?{urllib.parse.urlencode(search_query)}")
        search_items = self._items(search.json() if search.status == 200 else None)
        search_matches = any(search_term.casefold() in str(entry.get("Name", "")).casefold() for entry in search_items if isinstance(entry, dict))
        record("catalog.search", "supported" if search_matches else "partial", f"search for {search_term!r} returned {len(search_items)} items with a matching title={search_matches}")

        facet_query = {"UserId": self.user_id, "Limit": "500"}
        if library:
            facet_query["ParentId"] = str(library["Id"])
        facets = []
        for path in ("/Items/Filters", "/Genres", "/Studios"):
            response = self.request("GET", f"{path}?{urllib.parse.urlencode(facet_query)}")
            payload = response.json() if response.status == 200 else None
            facets.append((path, response.status, payload))
        non_empty = sum(bool(self._items(payload)) or (isinstance(payload, dict) and any(bool(value) for value in payload.values())) for _, _, payload in facets)
        filter_outcome = "supported" if non_empty == 3 else "partial" if all(status == 200 for _, status, _ in facets) else "missing"
        record("catalog.filters", filter_outcome, f"{non_empty}/3 facet responses contained data")

        image_tags = detail.get("ImageTags", {}) if isinstance(detail, dict) else {}
        if isinstance(image_tags, dict) and image_tags.get("Primary"):
            image_path = f"/Items/{urllib.parse.quote(item_id)}/Images/Primary?maxWidth=64"
            image = self.request("GET", image_path)
            image = self.follow_redirect_once(image, image_path)
            content_type = image.headers.get("Content-Type", "")
            image_ok = image.status == 200 and content_type.startswith("image/") and bool(image.body)
            record("artwork.standard", "supported" if image_ok else "partial", f"Primary image returned HTTP {image.status}, {content_type!r}, {len(image.body)} bytes")
        else:
            record("artwork.standard", "inconclusive", "selected fixture item has no Primary image tag")

        chapters = detail.get("Chapters", []) if isinstance(detail, dict) else []
        chapter = chapters[0] if isinstance(chapters, list) and chapters else None
        chapter_response = self.request("GET", f"/Items/{urllib.parse.quote(item_id)}/Images/Chapter/0?maxWidth=64")
        chapter_observed = "supported" if chapter_response.status == 200 and chapter_response.headers.get("Content-Type", "").startswith("image/") else self._outcome_for_missing(chapter_response)
        record("artwork.chapter", chapter_observed, f"chapter metadata present={bool(chapter)}; route returned HTTP {chapter_response.status}")

        provider_ids = detail.get("ProviderIds") if isinstance(detail, dict) else None
        provider_observed = "supported" if isinstance(provider_ids, dict) and any(provider_ids.values()) else "stubbed" if isinstance(provider_ids, dict) else "missing"
        record("metadata.provider-ids", provider_observed, f"provider id keys={sorted(provider_ids) if isinstance(provider_ids, dict) else []}")

        etag_response = self.request("GET", detail_path)
        etag = etag_response.headers.get("ETag") or etag_response.headers.get("Etag")
        if etag:
            conditional = self.request("GET", detail_path, headers={"If-None-Match": etag})
            cache_observed = "supported" if conditional.status == 304 else "partial"
            cache_evidence = f"ETag round trip returned HTTP {conditional.status}"
        else:
            cache_observed = "missing"
            cache_evidence = "detail response provided no ETag"
        record("cache.conditional-refresh", cache_observed, cache_evidence)

        additional = self.request("GET", f"/Videos/{urllib.parse.quote(item_id)}/AdditionalParts?{urllib.parse.urlencode({'UserId': self.user_id})}")
        additional_items = self._items(additional.json() if additional.status == 200 else None)
        if additional.status == 200 and additional_items:
            additional_observed = (
                "supported"
                if all(isinstance(entry, dict) and entry.get("Id") for entry in additional_items)
                else "partial"
            )
        elif additional.status == 200:
            additional_observed = "inconclusive"
        else:
            additional_observed = self._outcome_for_missing(additional)
        record("playback.additional-parts", additional_observed, f"route returned HTTP {additional.status} with {len(additional_items)} parts")

        ancestors = self.request("GET", f"/Items/{urllib.parse.quote(item_id)}/Ancestors?{urllib.parse.urlencode({'UserId': self.user_id})}")
        ancestor_payload = ancestors.json() if ancestors.status == 200 else None
        ancestor_ok = isinstance(ancestor_payload, list) and any(isinstance(entry, dict) and entry.get("Type") == "CollectionFolder" and entry.get("Id") for entry in ancestor_payload)
        record("hierarchy.ancestors", "supported" if ancestor_ok else self._outcome_for_missing(ancestors), f"route returned HTTP {ancestors.status}; usable collection ancestor={ancestor_ok}")

        trickplay = detail.get("Trickplay") if isinstance(detail, dict) else None
        trickplay_observed = "supported" if isinstance(trickplay, dict) and bool(trickplay) else "missing"
        record("playback.trickplay", trickplay_observed, f"Trickplay metadata has entries={bool(trickplay)}")

        episode_id = self.variables.get("episodeId")
        if episode_id:
            plugin_segments = self.request("GET", f"/Episode/{urllib.parse.quote(episode_id)}/IntroSkipperSegments")
            plugin_payload = plugin_segments.json() if plugin_segments.status == 200 else None
            plugin_valid = isinstance(plugin_payload, dict) and any(
                isinstance(value, dict) and value.get("Valid") for value in plugin_payload.values()
            )
            plugin_observed = "supported" if plugin_valid else self._outcome_for_missing(plugin_segments)
            if plugin_segments.status == 200 and not plugin_valid:
                plugin_observed = "stubbed"
            record("segments.plugin-intro-skipper", plugin_observed, f"episode route returned HTTP {plugin_segments.status}; valid segment={plugin_valid}")
        else:
            record("segments.plugin-intro-skipper", "inconclusive", "fixture exposes no episode for the episode-specific plugin probe")

        standard_segments = self.request("GET", f"/MediaSegments/{urllib.parse.quote(item_id)}")
        segment_payload = standard_segments.json() if standard_segments.status == 200 else None
        segment_items = self._items(segment_payload)
        segments_shape_ok = standard_segments.status == 200 and isinstance(segment_payload, dict) and isinstance(segment_payload.get("Items"), list)
        segments_semantic_ok = segments_shape_ok and bool(segment_items) and all(
            isinstance(segment, dict)
            and segment.get("Type")
            and isinstance(segment.get("StartTicks"), int)
            and isinstance(segment.get("EndTicks"), int)
            and 0 <= segment["StartTicks"] < segment["EndTicks"]
            for segment in segment_items
        )
        record("segments.standard", "supported" if segments_semantic_ok else "inconclusive" if segments_shape_ok else self._outcome_for_missing(standard_segments), f"route returned HTTP {standard_segments.status} with {len(segment_items)} valid markers={segments_semantic_ok}")

        sessions = self.request("GET", "/Sessions")
        sessions_payload = sessions.json() if sessions.status == 200 else None
        if isinstance(sessions_payload, list) and sessions_payload:
            sessions_observed = "supported"
        elif isinstance(sessions_payload, list):
            sessions_observed = "stubbed"
        else:
            sessions_observed = self._outcome_for_missing(sessions)
        record("sessions.list", sessions_observed, f"route returned HTTP {sessions.status} with {len(sessions_payload) if isinstance(sessions_payload, list) else 0} sessions")

        if expected["sessions.revoke"] == "supported" and allow_mutations:
            secondary_login = self.transport.request(
                "POST",
                "/Users/AuthenticateByName",
                headers=self.headers(token="", device_id="bloom-contract-revocation-target"),
                json_body={"Username": self.username, "Pw": self.password},
            )
            secondary_payload = secondary_login.json() if secondary_login.status == 200 else {}
            secondary_token = secondary_payload.get("AccessToken", "") if isinstance(secondary_payload, dict) else ""
            secondary_session = secondary_payload.get("SessionInfo", {}) if isinstance(secondary_payload, dict) else {}
            secondary_session_id = secondary_session.get("Id", "") if isinstance(secondary_session, dict) else ""
            if secondary_token and secondary_session_id:
                revoke = self.request("POST", f"/Sessions/{urllib.parse.quote(secondary_session_id)}/Logout", raw_body=b"")
                revoked_validation = self.transport.request(
                    "GET",
                    f"/Users/{urllib.parse.quote(self.user_id)}",
                    headers=self.headers(token=secondary_token, device_id="bloom-contract-revocation-target"),
                )
                revoke_ok = revoke.status in {200, 204} and revoked_validation.status == 401
                record("sessions.revoke", "supported" if revoke_ok else "partial", f"target revoke HTTP {revoke.status}; revoked token validation HTTP {revoked_validation.status}")
            else:
                record("sessions.revoke", "partial", "could not create an isolated secondary session for revocation")
        elif expected["sessions.revoke"] == "supported":
            record("sessions.revoke", "inconclusive", "revocation probe skipped; pass --allow-mutations to create and revoke an isolated session")
        else:
            revoke = self.request("POST", "/Sessions/nonexistent-contract-session/Logout", raw_body=b"")
            revoke_observed = "missing" if revoke.status in {404, 405} else "partial"
            record("sessions.revoke", revoke_observed, f"named-session logout probe returned HTTP {revoke.status}")

        series_id = self.variables.get("seriesId", item_id)
        themes = self.request("GET", f"/Items/{urllib.parse.quote(series_id)}/ThemeSongs?{urllib.parse.urlencode({'UserId': self.user_id})}")
        theme_items = self._items(themes.json() if themes.status == 200 else None)
        theme_observed = "supported" if theme_items else "stubbed" if themes.status == 200 else self._outcome_for_missing(themes)
        record("metadata.theme-songs", theme_observed, f"route returned HTTP {themes.status} with {len(theme_items)} theme items")

        playback = self.request(
            "POST",
            f"/Items/{urllib.parse.quote(item_id)}/PlaybackInfo?{urllib.parse.urlencode({'UserId': self.user_id})}",
            json_body={},
        )
        playback_payload = playback.json() if playback.status == 200 else {}
        play_session = playback_payload.get("PlaySessionId", "") if isinstance(playback_payload, dict) else ""
        media_sources = playback_payload.get("MediaSources", []) if isinstance(playback_payload, dict) else []
        source = media_sources[0] if isinstance(media_sources, list) and media_sources else {}
        stream_url = source.get("DirectStreamUrl") or source.get("TranscodingUrl") if isinstance(source, dict) else None
        info_ok = bool(play_session and isinstance(source, dict) and source.get("Id") and stream_url)
        record("playback.info", expected["playback.info"] if info_ok else "missing", f"play session present={bool(play_session)}, media sources={len(media_sources) if isinstance(media_sources, list) else 0}, usable URL={bool(stream_url)}")

        media_streams = source.get("MediaStreams", []) if isinstance(source, dict) else []
        external_subtitles = [stream for stream in media_streams if isinstance(stream, dict) and stream.get("IsExternal")]
        delivery_urls = [stream.get("DeliveryUrl") or stream.get("DeliveryURL") for stream in external_subtitles]
        subtitle_observed = "supported" if any(delivery_urls) else "partial"
        record("subtitles.delivery-url", subtitle_observed, f"external subtitle tracks={len(external_subtitles)}, delivery URLs={sum(bool(url) for url in delivery_urls)}")

        if stream_url:
            absolute_stream = urllib.parse.urljoin(self.transport.base_url + "/", str(stream_url))
            ranged = self.request("GET", absolute_stream, headers={"Range": "bytes=0-31"})
            ranged = self.follow_redirect_once(ranged, absolute_stream, headers={"Range": "bytes=0-31"})
            content_range = ranged.headers.get("Content-Range", "")
            range_ok = ranged.status == 206 and content_range.startswith("bytes 0-31/") and len(ranged.body) == 32
            record("playback.range-stream", "supported" if range_ok else "partial", f"range returned HTTP {ranged.status}, Content-Range={content_range!r}, bytes={len(ranged.body)}")
        else:
            record("playback.range-stream", "missing", "PlaybackInfo provided no stream URL")

        if allow_mutations and play_session and isinstance(source, dict) and source.get("Id"):
            report = {
                "ItemId": item_id,
                "MediaSourceId": source["Id"],
                "PlaySessionId": play_session,
                "PositionTicks": 10_000_000,
                "CanSeek": True,
                "IsPaused": False,
                "PlayMethod": "DirectPlay",
            }
            started = self.request("POST", "/Sessions/Playing", json_body=report)
            progressed = self.request("POST", "/Sessions/Playing/Progress", json_body={**report, "PositionTicks": 20_000_000, "EventName": "TimeUpdate"})
            progress_detail = self.request("GET", detail_path)
            progress_payload = progress_detail.json() if progress_detail.status == 200 else {}
            progress_user_data = progress_payload.get("UserData", {}) if isinstance(progress_payload, dict) else {}
            progress_observable = (
                isinstance(progress_user_data, dict)
                and (
                    int(progress_user_data.get("PlaybackPositionTicks") or 0) > 0
                    or bool(progress_user_data.get("Played"))
                )
            )
            stopped = self.request("POST", "/Sessions/Playing/Stopped", json_body={**report, "PositionTicks": 20_000_000, "EventName": "Stop"})
            report_ok = all(response.status in {200, 204} for response in (started, progressed, stopped)) and progress_observable
            report_observed = expected["playback.report"] if report_ok else "missing"
            record("playback.report", report_observed, f"start/progress/stop HTTP statuses={started.status}/{progressed.status}/{stopped.status}; progress observable={progress_observable}")
        elif not allow_mutations:
            record("playback.report", expected["playback.report"], "reporting probe skipped; pass --allow-mutations with a dedicated fixture account")
        else:
            record("playback.report", "missing", "no playback session was available for reporting")

        if allow_mutations:
            encoded_user = urllib.parse.quote(self.user_id)
            encoded_item = urllib.parse.quote(item_id)
            original_user_data = detail.get("UserData", {}) if isinstance(detail, dict) else {}
            for contract, suffix, field in (
                ("state.played", "PlayedItems", "Played"),
                ("state.favorite", "FavoriteItems", "IsFavorite"),
            ):
                state_path = f"/Users/{encoded_user}/{suffix}/{encoded_item}"
                original_state = bool(original_user_data.get(field)) if isinstance(original_user_data, dict) else False
                mutation_method = "DELETE" if original_state else "POST"
                restore_method = "POST" if original_state else "DELETE"
                changed = self.request(mutation_method, state_path, raw_body=b"")
                changed_detail = self.request("GET", detail_path)
                changed_payload = changed_detail.json() if changed_detail.status == 200 else {}
                changed_state = bool(changed_payload.get("UserData", {}).get(field)) if isinstance(changed_payload, dict) else original_state
                restored = self.request(restore_method, state_path, raw_body=b"")
                restored_detail = self.request("GET", detail_path)
                restored_payload = restored_detail.json() if restored_detail.status == 200 else {}
                restored_state = bool(restored_payload.get("UserData", {}).get(field)) if isinstance(restored_payload, dict) else not original_state
                state_ok = (
                    changed.status in {200, 204}
                    and changed_state != original_state
                    and restored.status in {200, 204}
                    and restored_state == original_state
                )
                record(
                    contract,
                    "supported" if state_ok else "partial",
                    f"original={original_state}, mutate HTTP {changed.status}, observable={changed_state}, restore HTTP {restored.status}, restored={restored_state}",
                )
        else:
            for contract in ("state.played", "state.favorite"):
                record(contract, expected[contract], "mutation probe skipped; pass --allow-mutations to verify and restore state")

        return results


DRIVERS = {"mediabrowser-v1": MediaBrowserV1Probe}


def main(argv: list[str] | None = None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deployment", required=True, help="deployment id from provider-contracts.json")
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--username", default=os.environ.get("BLOOM_CONTRACT_USERNAME", ""))
    parser.add_argument("--password-env", default="BLOOM_CONTRACT_PASSWORD", help="environment variable containing the password")
    parser.add_argument("--version", default="0.0-contract")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--allow-mutations", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--matrix", type=Path, default=Path(__file__).with_name("provider-contracts.json"))
    args = parser.parse_args(argv)

    password = os.environ.get(args.password_env, "")
    if not args.username or not password:
        parser.error(f"--username/BLOOM_CONTRACT_USERNAME and {args.password_env} are required")

    data = load_and_validate(args.matrix)
    deployment = next((item for item in data["deployments"] if item["id"] == args.deployment), None)
    if not deployment:
        parser.error(f"unknown deployment {args.deployment!r}")
    surface = deployment["surface"]
    driver_type = DRIVERS.get(surface)
    if not driver_type:
        parser.error(f"no live probe driver is registered for surface {surface!r}")

    expected = {
        contract["id"]: contract["expectations"][args.deployment]["outcome"]
        for contract in data["contracts"]
        if args.deployment in contract["expectations"]
    }
    driver = driver_type(HttpTransport(args.base_url, args.timeout), args.username, password, args.version)
    results = driver.run(expected, args.allow_mutations)

    for result in results:
        mark = "SKIP" if result.observed == "inconclusive" else "PASS" if result.passed else "FAIL"
        print(f"{mark:4} {result.contract:34} expected={result.expected:9} observed={result.observed:12} {result.evidence}")

    report = {
        "schemaVersion": 1,
        "deployment": args.deployment,
        "baseUrl": args.base_url,
        "results": [asdict(result) for result in results],
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    failed = [result for result in results if not result.passed]
    inconclusive = [result for result in results if result.observed == "inconclusive"]
    print(f"\n{len(results) - len(failed) - len(inconclusive)}/{len(results)} probes matched the baseline; {len(inconclusive)} inconclusive")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
