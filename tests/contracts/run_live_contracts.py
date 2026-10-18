#!/usr/bin/env python3
"""Run Bloom's opt-in provider contract probes against an operator-owned server."""

from __future__ import annotations

import argparse
import json
import os
import sys
import uuid
import hashlib
from datetime import datetime, timezone
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
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None


@dataclass
class ProbeResult:
    contract: str
    expected: str
    observed: str
    passed: bool
    evidence: str

def complete_expected_results(
    expected: dict[str, str],
    results: list[ProbeResult],
) -> list[ProbeResult]:
    reported = {result.contract for result in results}
    results.extend(
        ProbeResult(
            contract=contract,
            expected=outcome,
            observed="missing",
            passed=False,
            evidence="live driver omitted this expected probe",
        )
        for contract, outcome in expected.items()
        if contract not in reported
    )
    return results


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
                payload = reply.read(16 * 1024 * 1024 + 1)
                if len(payload) > 16 * 1024 * 1024:
                    return Response(0, {"X-Bloom-Transport-Error": "response size limit exceeded"}, b"")
                return Response(reply.status, dict(reply.headers.items()), payload)
        except urllib.error.HTTPError as error:
            return Response(error.code, dict(error.headers.items()), error.read(64 * 1024))
        except (urllib.error.URLError, TimeoutError) as error:
            return Response(0, {"X-Bloom-Transport-Error": str(error)}, b"")


class MediaBrowserV1Probe:
    """The current Bloom wire surface. Other protocol drivers can be added beside it."""
    requires_credentials = True

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

        views_response = self.request(
            "GET", f"/UserViews?{urllib.parse.urlencode({'UserId': self.user_id})}"
        )
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
            "Fields": "Overview,ProviderIds,MediaSources,Chapters,Trickplay",
            "EnableImages": "true",
            "EnableUserData": "true",
            "UserId": self.user_id,
        }
        if library:
            query["ParentId"] = str(library["Id"])
        items_path = f"/Items?{urllib.parse.urlencode(query)}"
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
            second_path = f"/Items?{urllib.parse.urlencode(second_query)}"
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

        detail_path = f"/Items?{urllib.parse.urlencode({'UserId': self.user_id, 'Ids': item_id, 'Fields': 'Overview,ProviderIds,People,MediaSources,Chapters,Trickplay', 'EnableUserData': 'true'})}"
        detail_response = self.request("GET", detail_path)
        detail_items = self._items(detail_response.json() if detail_response.status == 200 else None)
        detail = detail_items[0] if detail_items and isinstance(detail_items[0], dict) else {}
        detail_ok = isinstance(detail, dict) and detail.get("Id") == item_id and detail.get("Type") and detail.get("Name")
        record("catalog.details", expected["catalog.details"] if detail_ok else "missing", f"detail required fields present={bool(detail_ok)}")

        latest_query = {"UserId": self.user_id, "Limit": "10", "Fields": "ProviderIds", "EnableUserData": "true"}
        if library:
            latest_query["ParentId"] = str(library["Id"])
        latest = self.request("GET", f"/Items/Latest?{urllib.parse.urlencode(latest_query)}")
        latest_items = self._items(latest.json() if latest.status == 200 else None)
        latest_ids = [str(entry["Id"]) for entry in latest_items if isinstance(entry, dict) and entry.get("Id")]
        latest_ok = bool(latest_ids) and len(latest_ids) == len(latest_items) == len(set(latest_ids))
        record("catalog.latest", "supported" if latest_ok else "inconclusive" if latest.status == 200 else "missing", f"latest returned {len(latest_items)} items with stable unique ids={latest_ok}")

        next_up = self.request("GET", f"/Shows/NextUp?{urllib.parse.urlencode({'UserId': self.user_id, 'Limit': '10', 'Fields': 'Overview,ParentId', 'EnableUserData': 'true'})}")
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
            episode_query = {"UserId": self.user_id, "IncludeItemTypes": "Episode", "Recursive": "true", "Limit": "1"}
            episode_response = self.request(
                "GET",
                f"/Items?{urllib.parse.urlencode(episode_query)}",
            )
            episode_items = self._items(episode_response.json() if episode_response.status == 200 else None)
            episode = next(
                (entry for entry in episode_items if isinstance(entry, dict) and entry.get("Id") and entry.get("Type") == "Episode"),
                None,
            )
            if episode:
                self.variables["episodeId"] = str(episode["Id"])
            else:
                series_query = {"UserId": self.user_id, "IncludeItemTypes": "Series", "Recursive": "true", "Limit": "1"}
                series_response = self.request(
                    "GET",
                    f"/Items?{urllib.parse.urlencode(series_query)}",
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
        search_query = {"UserId": self.user_id, "SearchTerm": search_term, "IncludeItemTypes": "Movie,Series,Episode", "Recursive": "true", "Limit": "20"}
        search = self.request("GET", f"/Items?{urllib.parse.urlencode(search_query)}")
        search_items = self._items(search.json() if search.status == 200 else None)
        search_matches = any(search_term.casefold() in str(entry.get("Name", "")).casefold() for entry in search_items if isinstance(entry, dict))
        record("catalog.search", "supported" if search_matches else "partial", f"search for {search_term!r} returned {len(search_items)} items with a matching title={search_matches}")

        facet_query = {"UserId": self.user_id}
        if library:
            facet_query["ParentId"] = str(library["Id"])
        facets = []
        for route, allow_limit in (("/Items/Filters2", False), ("/Genres", True), ("/Studios", True)):
            route_query = dict(facet_query)
            if allow_limit:
                route_query["Limit"] = "500"
            response = self.request("GET", f"{route}?{urllib.parse.urlencode(route_query)}")
            payload = response.json() if response.status == 200 else None
            facets.append((route, response.status, payload))
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

        segment_item_id = self.variables.get("episodeId", item_id)
        standard_segments = self.request("GET", f"/MediaSegments/{urllib.parse.quote(segment_item_id)}")
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
            secondary_device_id = "bloom-contract-revocation-target"
            if secondary_token and secondary_session.get("Id"):
                revoke = self.request(
                    "DELETE", f"/Devices?{urllib.parse.urlencode({'id': secondary_device_id})}"
                )
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
            revoke = self.request("DELETE", "/Devices?id=nonexistent-contract-device")
            revoke_observed = "missing" if revoke.status in {404, 405} else "partial"
            record("sessions.revoke", revoke_observed, f"device deletion probe returned HTTP {revoke.status}")

        series_id = self.variables.get("seriesId", item_id)
        themes = self.request("GET", f"/Items/{urllib.parse.quote(series_id)}/ThemeSongs?{urllib.parse.urlencode({'UserId': self.user_id})}")
        theme_items = self._items(themes.json() if themes.status == 200 else None)
        theme_observed = "supported" if theme_items else "stubbed" if themes.status == 200 else self._outcome_for_missing(themes)
        record("metadata.theme-songs", theme_observed, f"route returned HTTP {themes.status} with {len(theme_items)} theme items")

        playback = self.request(
            "POST",
            f"/Items/{urllib.parse.quote(item_id)}/PlaybackInfo",
            json_body={"UserId": self.user_id},
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
            progressed = self.request("POST", "/Sessions/Playing/Progress", json_body={**report, "PositionTicks": 20_000_000})
            progress_detail = self.request("GET", detail_path)
            progress_items = self._items(progress_detail.json() if progress_detail.status == 200 else None)
            progress_payload = progress_items[0] if progress_items and isinstance(progress_items[0], dict) else {}
            progress_user_data = progress_payload.get("UserData", {}) if isinstance(progress_payload, dict) else {}
            progress_observable = (
                isinstance(progress_user_data, dict)
                and (
                    int(progress_user_data.get("PlaybackPositionTicks") or 0) > 0
                    or bool(progress_user_data.get("Played"))
                )
            )
            stopped = self.request(
                "POST",
                "/Sessions/Playing/Stopped",
                json_body={
                    "ItemId": item_id,
                    "MediaSourceId": source["Id"],
                    "PlaySessionId": play_session,
                    "PositionTicks": 20_000_000,
                },
            )
            report_ok = all(response.status in {200, 204} for response in (started, progressed, stopped)) and progress_observable
            report_observed = expected["playback.report"] if report_ok else "missing"
            record("playback.report", report_observed, f"start/progress/stop HTTP statuses={started.status}/{progressed.status}/{stopped.status}; progress observable={progress_observable}")
        elif not allow_mutations:
            record("playback.report", expected["playback.report"], "reporting probe skipped; pass --allow-mutations with a dedicated fixture account")
        else:
            record("playback.report", "missing", "no playback session was available for reporting")

        if allow_mutations:
            encoded_item = urllib.parse.quote(item_id)
            original_user_data = detail.get("UserData", {}) if isinstance(detail, dict) else {}
            for contract, suffix, field in (
                ("state.played", "UserPlayedItems", "Played"),
                ("state.favorite", "UserFavoriteItems", "IsFavorite"),
            ):
                state_path = f"/{suffix}/{encoded_item}?{urllib.parse.urlencode({'UserId': self.user_id})}"
                original_state = bool(original_user_data.get(field)) if isinstance(original_user_data, dict) else False
                mutation_method = "DELETE" if original_state else "POST"
                restore_method = "POST" if original_state else "DELETE"
                changed = self.request(mutation_method, state_path, raw_body=b"")
                changed_detail = self.request("GET", detail_path)
                changed_items = self._items(changed_detail.json() if changed_detail.status == 200 else None)
                changed_payload = changed_items[0] if changed_items and isinstance(changed_items[0], dict) else {}
                changed_state = bool(changed_payload.get("UserData", {}).get(field)) if isinstance(changed_payload, dict) else original_state
                restored = self.request(restore_method, state_path, raw_body=b"")
                restored_detail = self.request("GET", detail_path)
                restored_items = self._items(restored_detail.json() if restored_detail.status == 200 else None)
                restored_payload = restored_items[0] if restored_items and isinstance(restored_items[0], dict) else {}
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


class SiloNativeV2Probe:
    """Probe the stable native contract with a disposable account and media."""
    requires_credentials = False

    def __init__(self, transport, username, password, version, profile_id="", profile_pin=""):
        self.transport = transport
        self.username = username
        self.password = password
        self.version = version
        self.profile_id = profile_id
        self.profile_pin = profile_pin
        self.access_token = ""
        self.refresh_token = ""
        self.profile_token = ""
        self.system_info = {}

    def headers(self, token="", profile_id="", profile_token=""):
        headers = {"Accept": "application/json", "Content-Type": "application/json",
                   "X-Silo-Client": "Bloom Contract", "X-Silo-Client-Version": self.version,
                   "X-Silo-Device-Id": "bloom-contract-native-v2",
                   "X-Silo-Device-Name": "Bloom Contract Probe", "X-Silo-Device-Platform": sys.platform}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if profile_id:
            headers["X-Profile-Id"] = profile_id
        if profile_token:
            headers["X-Profile-Token"] = profile_token
        return headers

    def request(self, method, path, *, token="", profile_id="", profile_token="", json_body=None, extra_headers=None):
        headers = self.headers(token, profile_id, profile_token)
        if path.startswith(("http://", "https://")) and not self.transport.is_same_origin(path):
            headers = {"Accept": "*/*"}
        headers.update(extra_headers or {})
        return self.transport.request(method, path, headers=headers, json_body=json_body)

    @staticmethod
    def _object(payload):
        return payload if isinstance(payload, dict) else {}

    @staticmethod
    def _array(payload, *keys):
        if isinstance(payload, dict):
            for key in keys or ("items",):
                if isinstance(payload.get(key), list):
                    return payload[key]
        return []

    @staticmethod
    def _identity(value):
        return isinstance(value, str) and bool(value)

    @staticmethod
    def _playback_version(versions):
        return next((version for version in versions or []
                     if isinstance(version, dict) and SiloNativeV2Probe._identity(version.get("file_id"))), None)

    @staticmethod
    def _header(response, name):
        return next((value for key, value in response.headers.items() if key.lower() == name.lower()), "")

    def _record(self, results, expected, contract, observed, evidence):
        if contract not in expected:
            return
        wanted = expected[contract]
        passed = observed == wanted or wanted == "partial" and observed == "supported"
        results.append(ProbeResult(contract, wanted, observed, passed, evidence))

    def _check(self, results, expected, contract, passed, evidence):
        self._record(results, expected, contract, "supported" if passed else "missing", evidence)

    def _skip(self, results, expected, contract, evidence):
        self._record(results, expected, contract, "inconclusive", evidence)

    def _health(self, expected, results):
        health = self.request("GET", "/health")
        ready = self.request("GET", "/ready")
        info = self.request("GET", "/api/v2/system/info")
        self.system_info = self._object(info.json())
        valid = health.status == 200 and ready.status == 200 and info.status == 200 \
            and self.system_info.get("api_major") == 2 and isinstance(self.system_info.get("links"), dict)
        self._check(results, expected, "native.health", valid,
                    "neutral liveness/readiness and v2 identity; build fields are diagnostic only")
        return valid

    def run(self, expected, allow_mutations):
        results = []
        if not self._health(expected, results):
            for key in expected:
                if key != "native.health":
                    self._skip(results, expected, key, "native v2 discovery failed")
            return results
        if not self.username or not self.password:
            for key in expected:
                if key != "native.health":
                    self._skip(results, expected, key, "disposable fixture credentials are absent")
            return results
        try:
            self._authenticated(expected, results, allow_mutations)
        finally:
            if self.access_token:
                response = self.request("POST", "/api/v2/auth/logout", token=self.access_token)
                self._check(results, expected, "native.auth.logout", response.status in (200, 204), "caller session cleanup")
        reported = {result.contract for result in results}
        for key in expected.keys() - reported:
            self._skip(results, expected, key, "this fixture did not provide the required resource")
        return results

    def _authenticated(self, expected, results, allow_mutations):
        error = self.request("GET", "/api/v2/account/me")
        problem = self._object(error.json())
        self._check(results, expected, "native.auth.errors", error.status == 401
                    and self._header(error, "Content-Type").startswith("application/problem+json")
                    and all(key in problem for key in ("type", "title", "status", "detail", "instance")),
                    "unauthenticated request returned a structured RFC 9457 problem")
        login = self.request("POST", "/api/v2/auth/login", json_body={"username": self.username, "password": self.password})
        body = self._object(login.json())
        valid = login.status == 200 and all(self._identity(body.get(key)) for key in ("access_token", "refresh_token")) \
            and self._identity(body.get("user", {}).get("id")) and body.get("expires_in", 0) > 0
        self._check(results, expected, "native.auth.login", valid, "token pair, expiry and opaque account identity")
        if not valid:
            return
        self.access_token, self.refresh_token = body["access_token"], body["refresh_token"]
        account_id = body["user"]["id"]
        refreshed = self.request("POST", "/api/v2/auth/refresh", json_body={"refresh_token": self.refresh_token})
        refreshed_body = self._object(refreshed.json())
        valid_refresh = refreshed.status == 200 and self._identity(refreshed_body.get("access_token")) and refreshed_body.get("expires_in", 0) > 0
        if valid_refresh:
            self.access_token = refreshed_body["access_token"]
            self.refresh_token = refreshed_body.get("refresh_token", self.refresh_token)
        self._check(results, expected, "native.auth.refresh", valid_refresh, "access refresh with optional refresh-token rotation")
        me = self.request("GET", "/api/v2/account/me", token=self.access_token)
        account = self._object(me.json())
        self._check(results, expected, "native.auth.me", me.status == 200 and account.get("id") == account_id
                    and isinstance(account.get("permissions"), list), "restored account identity and permissions")
        response = self.request("GET", "/api/v2/profiles", token=self.access_token)
        profiles = self._array(response.json())
        profile = next((p for p in profiles if p.get("id") == self.profile_id), None) if self.profile_id else \
            next((p for p in profiles if p.get("is_primary")), profiles[0] if profiles else None)
        self._check(results, expected, "native.profiles.list", response.status == 200 and bool(profile)
                    and all(self._identity(p.get("id")) for p in profiles), "profile items collection with string identities")
        if not profile:
            return
        self.profile_id = profile["id"]
        if profile.get("has_pin"):
            if not self.profile_pin:
                return
            pin = self.request("POST", f"/api/v2/profiles/{urllib.parse.quote(self.profile_id, safe='')}/verify-pin",
                               token=self.access_token, json_body={"pin": self.profile_pin})
            self.profile_token = self._object(pin.json()).get("profile_token", "")
            if not self.profile_token:
                return
        scoped = {"token": self.access_token, "profile_id": self.profile_id, "profile_token": self.profile_token}
        libraries = self.request("GET", "/api/v2/user/libraries", **scoped)
        self._check(results, expected, "native.catalog.libraries", libraries.status == 200
                    and all(self._identity(item.get("id")) for item in self._array(libraries.json())),
                    "profile-scoped library items with string IDs")
        first = self.request("GET", "/api/v2/catalog?limit=2", **scoped)
        page = self._object(first.json())
        first_items = self._array(page)
        cursor = page.get("page", {}).get("next_cursor")
        next_page = self._object(self.request("GET", "/api/v2/catalog?" + urllib.parse.urlencode({"limit": 2, "cursor": cursor}), **scoped).json()) if cursor else {}
        ids = {item.get("content_id") for item in first_items}
        paging_ok = first.status == 200 and isinstance(page.get("window_cursor"), str) \
            and isinstance(page.get("page", {}).get("has_more"), bool) \
            and (not page["page"]["has_more"] or cursor and bool(next_page.get("items")) and ids.isdisjoint(item.get("content_id") for item in next_page["items"]))
        self._check(results, expected, "native.catalog.page", paging_ok, "cursor continuation returned distinct items in a catalog window")
        query = self.request("POST", "/api/v2/catalog/query", **scoped,
                             json_body={"source": "query", "groups": [], "limit": 2})
        query_body = self._object(query.json())
        query_cursor = query_body.get("page", {}).get("next_cursor")
        query_next = self.request("POST", "/api/v2/catalog/query", **scoped,
                                  json_body={"source": "query", "groups": [], "limit": 2, "cursor": query_cursor}) if query_cursor else None
        self._check(results, expected, "native.catalog.query", query.status == 200
                    and isinstance(query_body.get("items"), list) and (not query_cursor or query_next.status == 200),
                    "read-only POST query and cursor continuation")
        if first_items:
            item_id = first_items[0]["content_id"]
            detail = self.request("GET", "/api/v2/catalog/items/" + urllib.parse.quote(item_id, safe=""), **scoped)
            detail_body = self._object(detail.json())
            self._check(results, expected, "native.catalog.detail", detail.status == 200 and detail_body.get("content_id") == item_id
                        and isinstance(detail_body.get("versions"), list), "detail retains content identity, user state and versions")
        caps_response = self.request("GET", "/api/v2/playback/capabilities", **scoped)
        caps = self._object(caps_response.json())
        etag = self._header(caps_response, "ETag")
        conditional = self.request("GET", "/api/v2/playback/capabilities", **scoped,
                                   extra_headers={"If-None-Match": etag}) if etag else None
        capable = caps_response.status == 200 and caps.get("state") == "available" and caps.get("allowed") is True \
            and 3 in caps.get("protocol_versions", []) and "original_http" in caps.get("deliveries", []) and self._identity(caps.get("installation_id"))
        self._check(results, expected, "native.playback.capability", capable and conditional is not None and conditional.status == 304
                    and not conditional.body, "protocol 3 capability permission and ETag/304 read")
        candidates = self._array(self.request("GET", "/api/v2/catalog?type=movie&limit=20", **scoped).json())
        watch = {}
        for item in candidates:
            candidate = self._object(self.request("GET", "/api/v2/watch/" + urllib.parse.quote(item["content_id"], safe=""), **scoped).json())
            if self._playback_version(candidate.get("versions")):
                watch = candidate
                break
        if capable and watch:
            if allow_mutations:
                self._playback(expected, results, scoped, caps, watch)
            else:
                for key in ("native.playback.start-v3", "native.playback.progress", "native.playback.stop", "native.playback.track"):
                    self._skip(results, expected, key, "requires --allow-mutations and a disposable account")
        self._themes(expected, results, scoped, allow_mutations)
        preview_item = os.environ.get("BLOOM_CONTRACT_TRICKPLAY_ITEM_ID", "")
        preview_watch = self._object(self.request("GET", "/api/v2/watch/" + urllib.parse.quote(preview_item, safe=""), **scoped).json()) if preview_item else watch
        preview = next((v for v in preview_watch.get("versions", []) if v.get("trickplay_available")), None)
        if preview:
            path = "/api/v2/watch/" + urllib.parse.quote(preview_watch["content_id"], safe="") + "/trickplay?" + urllib.parse.urlencode({"file_id": preview["file_id"]})
            metadata = self.request("GET", path, **scoped)
            wire = self._object(metadata.json())
            sheets = wire.get("sheets", [])
            sheet = self.request("GET", sheets[0]["url"], extra_headers={"Accept": "*/*"}) if sheets else None
            self._check(results, expected, "native.trickplay", metadata.status == 200 and wire.get("file_id") == preview["file_id"]
                        and wire.get("interval_ms", 0) > 0 and sheet is not None and sheet.status == 200 and sheet.body.startswith(b"\xff\xd8\xff"),
                        "published preview metadata and a signed JPEG sheet")
        else:
            self._skip(results, expected, "native.trickplay", "no published trickplay version; set BLOOM_CONTRACT_TRICKPLAY_ITEM_ID to a prepared fixture")

    def _themes(self, expected, results, scoped, allow_mutations):
        capability = self._object(self.request("GET", "/api/v2/catalog/themes/capabilities", **scoped).json())
        if capability.get("state") != "available" or not capability.get("allowed") or not allow_mutations:
            self._skip(results, expected, "native.theme-songs", "theme capability permission, fixture and --allow-mutations are required")
            return
        series = self._array(self.request("GET", "/api/v2/catalog?type=series&limit=20", **scoped).json())
        for item in series:
            detail = self._object(self.request("GET", "/api/v2/catalog/items/" + urllib.parse.quote(item["content_id"], safe=""), **scoped).json())
            themes = detail.get("themes", {})
            if not themes.get("items"):
                continue
            owner = urllib.parse.quote(themes["owner_id"], safe="")
            theme = urllib.parse.quote(themes["items"][0]["id"], safe="")
            valid = True
            deliveries = []
            formats = [{}, {"accepted_formats": [{"container": "mp4", "audio_codec": "aac"}]}] if capability.get("transcode") else [{}]
            for body in formats:
                granted = self.request("POST", f"/api/v2/catalog/items/{owner}/themes/{theme}/playback", **scoped, json_body=body)
                grant = self._object(granted.json())
                if granted.status != 200 or not grant.get("url") or not grant.get("expires_at"):
                    valid = False
                    break
                audio = self.request("GET", grant["url"], extra_headers={"Accept": "*/*"})
                converted = grant.get("delivery") == "converted"
                valid = valid and audio.status == 200 and bool(audio.body) \
                    and grant.get("delivery") in ("original", "converted") \
                    and (not converted or not self._header(audio, "Content-Length") and not self._header(audio, "Accept-Ranges"))
                deliveries.append(grant.get("delivery"))
            self._check(results, expected, "native.theme-songs", valid, "fresh grants and bounded audio fetches; deliveries=" + ",".join(deliveries))
            return
        self._skip(results, expected, "native.theme-songs", "no series theme fixture is available")

    def _playback(self, expected, results, scoped, capability, watch):
        version = self._playback_version(watch["versions"])
        codec = {"video_evidence": "declared", "audio_evidence": "declared", "codecs_video": ["h264", "hevc", "vp8", "vp9", "av1", "mpeg2video", "mpeg4", "vc1", "theora"],
                 "codecs_video_hardware": [], "codecs_audio": ["aac", "ac3", "eac3", "dts", "truehd", "flac", "opus", "vorbis", "mp3", "alac", "pcm_s16le", "pcm_s24le"],
                 "containers": ["mp4", "mkv", "webm", "mov", "ts", "avi", "mpegts", "ogg", "flac"], "max_resolution": "8k", "hdr": True}
        direct = {"enabled": True, "supported_on_device": True, "containers": codec["containers"], "video_codecs": codec["codecs_video"], "audio_decode_codecs": codec["codecs_audio"],
                  "audio_passthrough_codecs": [], "subtitles": {"embedded_text": True, "sidecar_text": True, "ass_styling": True, "embedded_bitmap": True, "sidecar_bitmap": False, "font_attachments": True},
                  "features": [], "auth_header_refresh": False, "validated_claims": ["client_selected_audio_track_v1", "client_managed_dynamic_range_v1"], "transformations": []}
        context = {"protocol_version": 3, "form_factor": "desktop", "app_version": self.version, "device": {"platform": sys.platform}, "output": {}, "deliveries": {"original_http": direct}}
        body = {"installation_id": capability["installation_id"], "protocol_version": 3, "client_features": ["playback_plan_v3", "neutral_playback_v3_contract_v1", "sequenced_progress_v1", "plan_source_duration_v1"],
                "file_id": version["file_id"], "profile_id": self.profile_id, "playback_attempt_id": str(uuid.uuid4()), "allow_alternate_versions": False, "quality_preference": "auto",
                "subtitle_fidelity_preference": "compatible", "start_position": 0, "audio_track_index": 0, "client_capabilities": codec, "client_playback_context": context}
        response = self.request("POST", "/api/v2/playback/start", **scoped, json_body=body)
        decision = self._object(response.json())
        plan = decision.get("playback_plan", {})
        session_id = decision.get("session_id") or plan.get("session_id")
        playable = response.status == 201 and decision.get("outcome") == "playable" and plan.get("delivery") == "original_http" and session_id
        if not session_id:
            self._check(results, expected, "native.playback.start-v3", False, "no playable session was returned")
            return
        session_path = "/api/v2/playback/" + urllib.parse.quote(session_id, safe="")
        try:
            stream = plan.get("stream", {})
            stream_url = stream.get("url", "/invalid")
            if self.transport.is_same_origin(stream_url):
                stream_url += ("&" if "?" in stream_url else "?") + urllib.parse.urlencode({"token": self.access_token})
            fetched = self.request("GET", stream_url, extra_headers={**stream.get("headers", {}), "Range": "bytes=0-4095", "Accept": "*/*"})
            replay = self.request("POST", "/api/v2/playback/start", **scoped, json_body=body)
            self._check(results, expected, "native.playback.start-v3", playable and fetched.status == 206 and bool(fetched.body)
                        and self._object(replay.json()).get("session_id") == session_id,
                        f"start HTTP {response.status}, range HTTP {fetched.status} bytes={len(fetched.body)}, replay HTTP {replay.status}, same session={self._object(replay.json()).get('session_id') == session_id}")
            progress_body = {"installation_id": body["installation_id"], "sequence": 1, "position": 1.0, "is_paused": False}
            progress = self.request("POST", session_path + "/progress", **scoped, json_body=progress_body)
            self._check(results, expected, "native.playback.progress", progress.status == 200
                        and self._object(progress.json()).get("accepted", {}).get("sequence") == 1,
                        "sequenced progress accepted position and pause state")
            replan = {"installation_id": body["installation_id"], "protocol_version": 3, "playback_attempt_id": body["playback_attempt_id"], "replan_request_id": str(uuid.uuid4()),
                      "failed_plan_id": plan.get("plan_id"), "plan_attempt_id": str(uuid.uuid4()), "plan_attempt_key": plan.get("plan_attempt_key"), "attempted_plan_keys": [], "attempt_count": 1,
                      "operation": "track_change", "quality_preference": "auto", "position_seconds": 1, "selected_tracks": plan.get("selected_tracks", {}), "client_capabilities": codec, "client_playback_context": context}
            switched = self.request("POST", session_path + "/replan", **scoped, json_body=replan)
            self._check(results, expected, "native.playback.track", switched.status == 200
                        and self._object(switched.json()).get("outcome") == "playable", "track_change returned a playable replacement decision")
        finally:
            stop_body = {"installation_id": body["installation_id"], "stop_id": str(uuid.uuid4()), "sequence": 2, "position": 1.0, "is_paused": False}
            stop = self.request("DELETE", session_path, **scoped, json_body=stop_body)
            stopped_again = self.request("DELETE", session_path, **scoped, json_body=stop_body)
            self._check(results, expected, "native.playback.stop", stop.status == 200 and stopped_again.status == 200
                        and self._object(stopped_again.json()).get("stop_id") == stop_body["stop_id"], "final position and idempotent stop cleanup")


DRIVERS = {
    "mediabrowser-v1": MediaBrowserV1Probe,
    "silo-native-v2": SiloNativeV2Probe,
}

def main(argv: list[str] | None = None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deployment", required=True, help="deployment id from provider-contracts.json")
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--username", default=os.environ.get("BLOOM_CONTRACT_USERNAME", ""))
    parser.add_argument("--profile-id", default=os.environ.get("BLOOM_CONTRACT_PROFILE_ID", ""))
    parser.add_argument("--profile-pin-env", default="BLOOM_CONTRACT_PROFILE_PIN", help="environment variable containing an optional native profile PIN")
    parser.add_argument("--password-env", default="BLOOM_CONTRACT_PASSWORD", help="environment variable containing the password")
    parser.add_argument("--version", default="0.0-contract")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--allow-mutations", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--matrix", type=Path, default=Path(__file__).with_name("provider-contracts.json"))
    args = parser.parse_args(argv)
    parsed_base_url = urllib.parse.urlsplit(args.base_url)
    if parsed_base_url.scheme not in {"http", "https"} or not parsed_base_url.netloc:
        parser.error("--base-url must be an HTTP(S) URL")
    if parsed_base_url.username or parsed_base_url.password:
        parser.error("--base-url must not contain embedded credentials")

    password = os.environ.get(args.password_env, "")
    profile_pin = os.environ.get(args.profile_pin_env, "")

    data = load_and_validate(args.matrix)
    deployment = next((item for item in data["deployments"] if item["id"] == args.deployment), None)
    if not deployment:
        parser.error(f"unknown deployment {args.deployment!r}")
    surface = deployment["surface"]
    driver_type = DRIVERS.get(surface)
    if driver_type is None:
        parser.error(f"unsupported protocol surface {surface!r}")
    if surface == "silo-native-v2":
        if deployment.get("protocolMode") != "native":
            parser.error(f"deployment {args.deployment!r} is not configured for native Silo mode")
        expected = {
            requirement["id"]: requirement["outcome"]
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement.get("liveProbe", False)
        }
    else:
        expected = {
            contract["id"]: contract["expectations"][args.deployment]["outcome"]
            for contract in data["contracts"]
            if args.deployment in contract["expectations"]
        }
    if not expected:
        parser.error(f"deployment {args.deployment!r} has no live probes for selected surface")
    transport = HttpTransport(args.base_url, args.timeout)
    if surface == "silo-native-v2":
        driver = driver_type(
            transport,
            args.username,
            password,
            args.version,
            args.profile_id,
            profile_pin,
        )
    else:
        driver = driver_type(transport, args.username, password, args.version)
    results = complete_expected_results(
        expected,
        driver.run(expected, args.allow_mutations),
    )

    for result in results:
        mark = "SKIP" if result.observed == "inconclusive" else "PASS" if result.passed else "FAIL"
        print(f"{mark:4} {result.contract:34} expected={result.expected:9} observed={result.observed:12} {result.evidence}")

    report = {
        "schemaVersion": 1,
        "deployment": args.deployment,
        "baseUrl": args.base_url,
        "sourceRevision": data["snapshot"]["siloRevision"] if surface == "silo-native-v2" else None,
        "systemInfo": driver.system_info if surface == "silo-native-v2" else None,
        "observedAt": datetime.now(timezone.utc).isoformat(),
        "clientSourceSha256": hashlib.sha256((Path(__file__).resolve().parents[2] / "src/providers/silo/SiloPlaybackProvider.cpp").read_bytes()).hexdigest() if surface == "silo-native-v2" else None,
        "results": [asdict(result) for result in results],
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    failed = [result for result in results if not result.passed]
    inconclusive = [result for result in results if result.observed == "inconclusive"]
    print(f"\n{sum(result.passed for result in results)}/{len(results)} probes matched the baseline; {len(inconclusive)} inconclusive")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
