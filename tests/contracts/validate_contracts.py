#!/usr/bin/env python3
"""Validate Bloom's provider contract baseline without contacting a server."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ALLOWED_OUTCOMES = {"supported", "partial", "stubbed", "missing", "not-applicable"}
NATIVE_CAPABILITY_STATES = {"available", "conditional", "unavailable"}
REQUIRED_NATIVE_COVERAGE = {
    "native.health",
    "native.auth.providers",
    "native.auth.login",
    "native.auth.errors",
    "native.auth.refresh",
    "native.auth.me",
    "native.auth.logout",
    "native.auth.sessions",
    "native.auth.device",
    "native.profiles.list",
    "native.profiles.pin",
    "native.catalog.libraries",
    "native.catalog.page",
    "native.catalog.query",
    "native.catalog.letters",
    "native.catalog.detail",
    "native.catalog.people",
    "native.catalog.hierarchy",
    "native.state.watched",
    "native.state.favorite",
    "native.state.watchlist",
    "native.state.not-interested",
    "native.home.sections",
    "native.home.calendar",
    "native.library.sections",
    "native.home.dismissals",
    "native.artwork.refetch",
    "native.playback.capability",
    "native.playback.start-v3",
    "native.playback.progress",
    "native.playback.stop",
    "native.playback.transcode",
    "native.playback.track",
    "native.playback.track-preferences",
    "native.markers",
    "native.chapters",
    "native.trickplay",
    "native.theme-songs",
    "native.poster-badges",
    "native.requests",
}
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
DIGEST_IMAGE_RE = re.compile(r"^[^\s@]+@sha256:[0-9a-f]{64}$")
HTTP_STATUS_RE = re.compile(r"\bHTTP\s+\d{3}\b", re.IGNORECASE)
OPENAPI_MANIFEST_PATH = Path(__file__).with_name("jellyfin-12-openapi-manifest.json")


class ContractValidationError(ValueError):
    pass


def _require(condition: bool, message: str):
    if not condition:
        raise ContractValidationError(message)


def _has_behavior_semantics(rule: str):
    without_codes = HTTP_STATUS_RE.sub("", rule)
    without_status_words = re.sub(r"\b(?:http|status|returns?)\b", "", without_codes, flags=re.IGNORECASE)
    return re.search(r"[A-Za-z]{3,}", without_status_words) is not None


def _unique_ids(values: list[dict[str, Any]], section: str):
    ids = [value.get("id") for value in values]
    _require(all(isinstance(value, str) and value for value in ids), f"{section} entries need non-empty ids")
    _require(len(ids) == len(set(ids)), f"{section} ids must be unique")
    return set(ids)


def load_jellyfin_openapi_manifest():
    try:
        manifest = json.loads(OPENAPI_MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractValidationError(
            f"could not read Jellyfin OpenAPI manifest: {error}"
        ) from error
    _require(isinstance(manifest, dict), "Jellyfin OpenAPI manifest must be an object")
    return manifest


def _validate_jellyfin_openapi(snapshot: dict[str, Any]):
    manifest = load_jellyfin_openapi_manifest()
    _require(manifest.get("schemaVersion") == 1, "Jellyfin OpenAPI manifest schema must be 1")
    _require(
        manifest.get("source") == snapshot.get("jellyfinOpenApiSource"),
        "Jellyfin OpenAPI manifest source must match the snapshot",
    )
    _require(
        manifest.get("sourceSha256") == snapshot.get("jellyfinOpenApiSha256"),
        "Jellyfin OpenAPI manifest digest must match the snapshot",
    )
    _require(
        manifest.get("apiVersion") == snapshot.get("jellyfinOpenApiVersion"),
        "Jellyfin OpenAPI manifest API version must match the snapshot",
    )

    operations = {
        (operation.get("method"), operation.get("path")): operation
        for operation in manifest.get("operations", [])
        if isinstance(operation, dict)
    }
    required_operations = {
        ("GET", "/UserViews"): {"userId"},
        ("GET", "/Items"): {"userId", "fields", "ids", "personIds", "includeItemTypes",
                            "sortBy", "sortOrder", "startIndex", "limit", "parentId",
                            "recursive", "enableImages", "enableUserData",
                            "enableTotalRecordCount"},
        ("GET", "/System/Info/Public"): set(),
        ("GET", "/Items/{itemId}"): {"itemId", "userId"},
        ("GET", "/Persons"): {"userId", "searchTerm", "limit", "enableImages",
                              "enableImageTypes", "personTypes"},
        ("GET", "/Items/Latest"): {"userId", "fields"},
        ("GET", "/Shows/NextUp"): {
            "userId", "limit", "fields", "enableUserData", "enableImageTypes",
            "enableResumable", "enableRewatching", "nextUpDateCutoff"},
        ("GET", "/UserItems/Resume"): {
            "userId", "limit", "fields", "enableUserData", "enableImageTypes",
            "includeItemTypes", "mediaTypes"},
        ("GET", "/Movies/Recommendations"): {
            "userId", "parentId", "fields", "categoryLimit", "itemLimit"},
        ("GET", "/Items/Filters2"): {"userId"},
        ("GET", "/Genres"): {"userId"},
        ("GET", "/Studios"): {"userId"},
        ("POST", "/UserPlayedItems/{itemId}"): {"userId"},
        ("DELETE", "/UserPlayedItems/{itemId}"): {"userId"},
        ("POST", "/UserFavoriteItems/{itemId}"): {"userId"},
        ("DELETE", "/UserFavoriteItems/{itemId}"): {"userId"},
        ("POST", "/UserItems/{itemId}/UserData"): {"itemId", "userId"},
        ("POST", "/Items/{itemId}/PlaybackInfo"): {"itemId"},
        ("GET", "/Videos/{itemId}/AdditionalParts"): {"itemId", "userId"},
        ("GET", "/Videos/{routeItemId}/{routeMediaSourceId}/Subtitles/{routeIndex}/Stream.{routeFormat}"): {
            "routeItemId", "routeMediaSourceId", "routeIndex", "routeFormat"},
        ("GET", "/MediaSegments/{itemId}"): {"itemId"},
        ("GET", "/Items/{itemId}/SpecialFeatures"): {"itemId", "userId"},
        ("GET", "/Items/{itemId}/LocalTrailers"): {"itemId", "userId"},
        ("GET", "/Users/{userId}"): {"userId"},
        ("GET", "/Sessions"): set(),
        ("POST", "/Sessions/Logout"): set(),
        ("DELETE", "/Devices"): {"id"},
        ("POST", "/Sessions/Playing"): set(),
        ("POST", "/Sessions/Playing/Progress"): set(),
        ("POST", "/Sessions/Playing/Stopped"): set(),
        ("GET", "/QuickConnect/Enabled"): set(),
        ("POST", "/QuickConnect/Initiate"): set(),
        ("GET", "/QuickConnect/Connect"): {"secret"},
        ("POST", "/Users/AuthenticateWithQuickConnect"): set(),
    }
    for operation_key, required_parameters in required_operations.items():
        operation = operations.get(operation_key)
        _require(operation is not None, f"Jellyfin OpenAPI is missing {operation_key}")
        _require(
            not operation.get("deprecated", False),
            f"Jellyfin OpenAPI operation is obsolete: {operation_key}",
        )
        parameters = {
            parameter.get("name"): parameter
            for parameter in operation.get("parameters", [])
            if isinstance(parameter, dict)
        }
        _require(
            required_parameters.issubset(parameters),
            f"Jellyfin OpenAPI parameters changed for {operation_key}",
        )
        _require(
            all(not parameters[name].get("deprecated", False) for name in required_parameters),
            f"Bloom uses an obsolete Jellyfin parameter for {operation_key}",
        )

    for excluded_operation in (
        ("GET", "/Users/{userId}/Items"),
        ("GET", "/Episode/{itemId}/IntroSkipperSegments"),
        ("POST", "/Sessions/{sessionId}/Logout"),
    ):
        _require(
            excluded_operation not in operations,
            f"excluded legacy Jellyfin operation is still in OpenAPI: {excluded_operation}",
        )

    schemas = manifest.get("schemas", {})
    item_fields = schemas.get("ItemFields", {}).get("enum", [])
    _require(
        "SpecialEpisodeNumbers" in item_fields,
        "Jellyfin ItemFields must include SpecialEpisodeNumbers",
    )
    recommendation_types = set(schemas.get("RecommendationType", {}).get("enum", []))
    _require(
        recommendation_types == {
            "SimilarToRecentlyPlayed", "SimilarToLikedItem", "HasDirectorFromRecentlyPlayed",
            "HasActorFromRecentlyPlayed", "HasLikedDirector", "HasLikedActor"},
        "Jellyfin RecommendationType values changed; Home recommendation titles cover each one",
    )
    playback_info_properties = schemas.get("PlaybackInfoDto", {}).get("properties", {})
    _require(
        "UserId" in playback_info_properties
        and not playback_info_properties["UserId"].get("deprecated", False),
        "PlaybackInfoDto.UserId must remain supported",
    )
    user_data_update = schemas.get("UpdateUserItemDataDto", {}).get("properties", {})
    _require(
        "PlaybackPositionTicks" in user_data_update
        and not user_data_update["PlaybackPositionTicks"].get("deprecated", False),
        "UpdateUserItemDataDto.PlaybackPositionTicks must remain supported",
    )
    progress_properties = schemas.get("PlaybackProgressInfo", {}).get("properties", {})
    _require(
        "EventName" not in progress_properties,
        "PlaybackProgressInfo must not restore the removed EventName field",
    )
    stream = operations.get(("GET", "/Videos/{itemId}/stream"))
    _require(
        stream is not None,
        "Jellyfin OpenAPI is missing ('GET', '/Videos/{itemId}/stream')",
    )
    stream_parameters = {
        parameter.get("name"): parameter
        for parameter in stream.get("parameters", [])
        if isinstance(parameter, dict)
    }
    _require(
        stream_parameters.get("deviceProfileId", {}).get("deprecated", False),
        "the audited stream manifest must retain obsolete deviceProfileId evidence",
    )


def load_silo_openapi_manifest():
    try:
        return json.loads(Path(__file__).with_name("silo-v2-openapi-manifest.json").read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ContractValidationError(f"could not read Silo OpenAPI manifest: {error}") from error


def _validate_native_silo(data, snapshot):
    native = data.get("nativeSiloContract")
    _require(isinstance(native, dict), "nativeSiloContract must be an object")
    _require(native.get("sourceRevision") == snapshot["siloRevision"], "native sourceRevision must match the pinned Silo revision")
    manifest = load_silo_openapi_manifest()
    _require(manifest.get("schemaVersion") == 1 and manifest.get("apiVersion") == "2", "Silo OpenAPI manifest must target v2")
    for manifest_key, snapshot_key in (("sourceRevision", "siloRevision"), ("source", "siloOpenApiSource"),
                                      ("sourceSha256", "siloOpenApiSha256"), ("migrationSource", "siloMigrationSource"),
                                      ("migrationSha256", "siloMigrationSha256")):
        _require(manifest.get(manifest_key) == snapshot.get(snapshot_key), f"Silo manifest {manifest_key} must match the snapshot")
    for key in ("siloOpenApiSha256", "siloMigrationSha256"):
        _require(SHA256_RE.fullmatch(snapshot.get(key, "")) is not None, f"snapshot {key} needs a full SHA-256")
    source = f"https://raw.githubusercontent.com/Silo-Server/silo-server/{native['sourceRevision']}/contracts/api/v2/"
    _require(snapshot.get("siloOpenApiSource") == source + "openapi.json", "Silo OpenAPI source must be pinned")
    _require(snapshot.get("siloMigrationSource") == source + "migration.json", "Silo migration source must be pinned")
    detection = native.get("detection")
    _require(isinstance(detection, dict), "nativeSiloContract detection must be an object")
    _require(detection.get("method") == "GET" and detection.get("path") == "/api/v2/system/info"
             and detection.get("probes") == ["/health", "/ready"], "native detection must use neutral probes and v2 system info")
    _require(detection.get("requiredValues") == {"api_major": 2}, "native detection must require api_major=2")
    _require(set(detection.get("requiredFields", [])) == {"api_major", "links"}, "native detection cannot gate on build identity")
    for field in ("requiredHeaders", "identityRules"):
        _require(isinstance(native.get(field), list) and all(isinstance(v, str) and v for v in native[field]), f"native {field} must contain strings")
    _require(any("X-Profile-Id" in header for header in native["requiredHeaders"]), "native profile header is required")
    _require("opaque" in " ".join(native["identityRules"]), "native IDs must remain opaque")
    requirements = native.get("requirements")
    _require(isinstance(requirements, list) and requirements, "native requirements must be a non-empty array")
    ids = _unique_ids(requirements, "native requirement")
    coverage = native.get("coverageRequirements", [])
    _require(len(coverage) == len(set(coverage)) and set(coverage) == REQUIRED_NATIVE_COVERAGE,
             "native coverageRequirements must cover the v2 baseline")
    _require(ids == set(coverage), "native coverageRequirements and requirements must match")
    operations = {(op["method"], op["path"]): op for op in manifest["operations"]}
    evidence_prefix = f"https://github.com/Silo-Server/silo-server/blob/{native['sourceRevision']}/"
    terms = {"native.auth.login": ("access_token", "refresh_token", "string", "username"),
             "native.auth.device": ("device_code", "user_code", "interval", "poll_after", "expires_in",
                                    "non_retryable", "429"),
             "native.auth.errors": ("problem+json", "type", "title", "status", "detail", "instance"),
             "native.catalog.page": ("cursor", "page.next_cursor", "window_cursor"),
             "native.catalog.query": ("100", "cursor", "string"),
             "native.playback.start-v3": ("playback_attempt_id", "installation_id", "client_capabilities", "playback_plan"),
             "native.playback.progress": ("sequence", "position", "seconds"),
             "native.playback.stop": ("stop_id", "installation_id"),
             "native.playback.track": ("track_change", "replan_request_id"),
             "native.theme-songs": ("grant", "expires_at", "converted", "ranges"),
             "native.trickplay": ("trickplay_available", "file_id", "signed", "interval_ms")}
    for requirement in requirements:
        key = requirement["id"]
        _require(requirement.get("outcome") in ALLOWED_OUTCOMES, f"{key} has an invalid outcome")
        capability = requirement.get("capability", {})
        _require(capability.get("state") in NATIVE_CAPABILITY_STATES, f"{key} has an invalid capability state")
        _require(bool(capability.get("discovery")), f"{key} needs capability discovery evidence")
        if capability["state"] == "unavailable":
            _require(requirement["outcome"] in {"missing", "not-applicable"}, f"{key} unavailable capability cannot be labeled supported")
        refs = requirement.get("operations")
        _require(isinstance(refs, list), f"{key} needs operation references")
        _require(bool(refs) or capability["state"] == "unavailable", f"{key} available capability needs operations")
        for ref in refs:
            op = operations.get((ref.get("method"), ref.get("path")))
            _require(op is not None and not op.get("deprecated"), f"{key} uses an absent or obsolete v2 operation")
        expected_method = "/".join(dict.fromkeys(ref["method"] for ref in refs)) if refs else "NONE"
        expected_path = " and ".join(dict.fromkeys(ref["path"] for ref in refs)) if refs else "unavailable"
        _require((requirement.get("method"), requirement.get("path")) == (expected_method, expected_path),
                 f"{key} route shape drifted from the pinned native contract")
        rules = requirement.get("requestSemantics", []) + requirement.get("requiredSemantics", [])
        _require(requirement.get("requestSemantics") and requirement.get("requiredSemantics")
                 and all(isinstance(rule, str) and rule for rule in rules), f"{key} needs request and response semantics")
        _require(any(_has_behavior_semantics(rule) for rule in requirement["requiredSemantics"]), f"{key} requires behavior semantics")
        text = " ".join(rules).lower()
        _require(all(term in text for term in terms.get(key, ())), f"{key} missing deterministic shape semantics")
        sources = requirement.get("evidenceSources", [])
        _require(sources and all(isinstance(url, str) and url.startswith(evidence_prefix) for url in sources),
                 f"{key} evidenceSources must be pinned to native sourceRevision")
        evidence = requirement.get("implementationEvidence", [])
        if key.startswith("native.playback.") and key != "native.playback.transcode":
            _require(bool(evidence), f"{key} needs implementationEvidence")
        root = Path(__file__).resolve().parents[2]
        _require(all(isinstance(path, str) and not Path(path).is_absolute() and (root / path).is_file() for path in evidence),
                 f"{key} implementationEvidence path does not exist")
        _require(isinstance(requirement.get("liveProbe", False), bool), f"{key} liveProbe must be boolean")
        if requirement.get("requiresMutationFlag"):
            _require(requirement.get("liveProbe") and any(ref["method"] != "GET" for ref in refs), f"{key} invalid mutation probe")
    _require(any(r.get("liveProbe") for r in requirements), "native requirements must include at least one liveProbe")
    _require(isinstance(native.get("outOfScope"), list) and len(native["outOfScope"]) >= 8, "native optional surfaces must remain out of scope")
    schemas = manifest["schemas"]
    start = schemas["PlaybackStartBody"]
    _require(len(start["required"]) == 10 and start["properties"]["file_id"]["type"] == "string", "Silo v3 start schema drifted")
    _require("cursor" in schemas["CatalogQuery"]["properties"] and "offset" not in schemas["CatalogQuery"]["properties"], "Silo query must use cursors")
    for path in (Path(__file__).resolve().parents[2] / "src/providers/silo").glob("*"):
        if path.is_file():
            _require("api/v1" not in path.read_text(), f"native v1 request remains in {path.name}")


def validate_contract_data(data: dict[str, Any]):
    _require(data.get("schemaVersion") == 1, "schemaVersion must be 1")

    outcomes = data.get("outcomes")
    _require(isinstance(outcomes, dict), "outcomes must be an object")
    _require(set(outcomes) == ALLOWED_OUTCOMES, "outcomes must define the complete status vocabulary")
    _require(all(isinstance(value, str) and value.strip() for value in outcomes.values()), "outcome descriptions must be non-empty")

    surfaces = data.get("surfaces")
    deployments = data.get("deployments")
    contracts = data.get("contracts")
    _require(isinstance(surfaces, list) and surfaces, "surfaces must be a non-empty array")
    _require(isinstance(deployments, list) and deployments, "deployments must be a non-empty array")
    _require(isinstance(contracts, list) and contracts, "contracts must be a non-empty array")

    surface_ids = _unique_ids(surfaces, "surface")
    deployment_ids = _unique_ids(deployments, "deployment")
    contract_ids = _unique_ids(contracts, "contract")

    for deployment in deployments:
        _require(deployment.get("surface") in surface_ids, f"deployment {deployment['id']} references an unknown surface")
        _require(deployment.get("protocolMode") in {"native", "compatibility"}, f"deployment {deployment['id']} has an invalid protocolMode")
        _require(bool(deployment.get("supportLabel")), f"deployment {deployment['id']} needs a supportLabel")
        if deployment.get("product") == "Silo Server" and deployment.get("surface") == "silo-native-v2":
            _require(
                deployment.get("protocolMode") == "native",
                f"deployment {deployment['id']} must model native Silo as a native protocol mode",
            )
            _require(
                deployment.get("id") != "silo-8044eb8-compat",
                "native Silo deployment must remain distinct from its compatibility deployment",
            )
            _require(
                deployment.get("supportLabel") != "silo-compatibility-support",
                f"deployment {deployment['id']} cannot use the compatibility support label",
            )

    media_browser_deployments = {
        deployment["id"] for deployment in deployments if deployment.get("surface") == "mediabrowser-v1"
    }
    _require(media_browser_deployments, "at least one mediabrowser-v1 deployment is required")

    native_deployments = [
        deployment
        for deployment in deployments
        if deployment.get("product") == "Silo Server" and deployment.get("surface") == "silo-native-v2"
    ]
    _require(native_deployments, "a native Silo deployment is required")
    _require(
        len(native_deployments) == len({deployment["id"] for deployment in native_deployments}),
        "native Silo deployments must have unique ids",
    )

    for contract in contracts:
        contract_id = contract["id"]
        for field in ("journey", "bloomCaller", "method", "path"):
            _require(isinstance(contract.get(field), str) and contract[field].strip(), f"{contract_id} needs {field}")

        request_semantics = contract.get("requestSemantics")
        required_semantics = contract.get("requiredSemantics")
        _require(isinstance(request_semantics, list) and request_semantics, f"{contract_id} needs requestSemantics")
        _require(isinstance(required_semantics, list) and required_semantics, f"{contract_id} needs requiredSemantics")
        _require(
            all(isinstance(rule, str) and rule.strip() for rule in request_semantics + required_semantics),
            f"{contract_id} semantics must be non-empty strings",
        )
        _require(
            any(_has_behavior_semantics(rule) for rule in required_semantics),
            f"{contract_id} must assert payload or behavior semantics, not only an HTTP status",
        )

        expectations = contract.get("expectations")
        _require(isinstance(expectations, dict), f"{contract_id} expectations must be an object")
        _require(set(expectations) == media_browser_deployments, f"{contract_id} must cover every mediabrowser-v1 deployment")
        for deployment_id, expectation in expectations.items():
            _require(isinstance(expectation, dict), f"{contract_id}/{deployment_id} expectation must be an object")
            _require(expectation.get("outcome") in ALLOWED_OUTCOMES, f"{contract_id}/{deployment_id} has an invalid outcome")
            _require(isinstance(expectation.get("evidence"), str) and expectation["evidence"].strip(), f"{contract_id}/{deployment_id} needs evidence")

    required_coverage = data.get("coverageRequirements")
    _require(isinstance(required_coverage, list) and required_coverage, "coverageRequirements must be a non-empty array")
    _require(len(required_coverage) == len(set(required_coverage)), "coverageRequirements must be unique")
    missing_coverage = set(required_coverage) - contract_ids
    _require(not missing_coverage, f"coverage requirements missing contracts: {', '.join(sorted(missing_coverage))}")

    snapshot = data.get("snapshot")
    _require(isinstance(snapshot, dict), "snapshot must be an object")
    _require(SHA_RE.fullmatch(snapshot.get("bloomRevision", "")) is not None, "snapshot bloomRevision must be a full Git SHA")
    _require(isinstance(snapshot.get("jellyfinVersion"), str) and snapshot["jellyfinVersion"], "snapshot jellyfinVersion must be non-empty")
    _require(DIGEST_IMAGE_RE.fullmatch(snapshot.get("jellyfinImage", "")) is not None, "snapshot jellyfinImage must use an immutable sha256 digest")
    _require(snapshot.get("jellyfinOpenApiVersion") == "12.0.0", "snapshot Jellyfin OpenAPI must target version 12.0.0")
    _require(SHA256_RE.fullmatch(snapshot.get("jellyfinOpenApiSha256", "")) is not None, "snapshot Jellyfin OpenAPI must use a full sha256 digest")
    _require(
        snapshot.get("jellyfinOpenApiSource")
        == "https://raw.githubusercontent.com/jellyfin/jellyfin-sdk-typescript/592747ce7add446b9a14ad56aba8a7441a2e2618/openapi.json",
        "snapshot Jellyfin OpenAPI must reference the official SDK specification",
    )
    _validate_jellyfin_openapi(snapshot)
    _require(snapshot.get("jellyfin12SmokeVersion") == "12.0.0", "snapshot Jellyfin 12 smoke must report 12.0.0")
    _require(DIGEST_IMAGE_RE.fullmatch(snapshot.get("jellyfin12SmokeImage", "")) is not None, "snapshot Jellyfin 12 smoke image must use an immutable sha256 digest")
    _require(SHA_RE.fullmatch(snapshot.get("siloRevision", "")) is not None, "snapshot siloRevision must be a full Git SHA")
    _require(DIGEST_IMAGE_RE.fullmatch(snapshot.get("siloCompatibilityImage", "")) is not None, "snapshot siloCompatibilityImage must use an immutable sha256 digest")
    _require(snapshot.get("siloCompatibilityImageTag") == snapshot["siloCompatibilityRevision"][:7], "snapshot siloCompatibilityImageTag must match the Silo revision")

    _validate_native_silo(data, snapshot)
    upstream_issues = data.get("upstreamIssues")
    _require(isinstance(upstream_issues, list) and upstream_issues, "upstreamIssues must be a non-empty array")
    _require(all(isinstance(url, str) and url.startswith("https://github.com/") for url in upstream_issues), "upstreamIssues entries must be GitHub URLs")

    questions = data.get("openUpstreamQuestions")
    _require(isinstance(questions, list) and questions, "openUpstreamQuestions must be a non-empty array")
    _require(all(isinstance(question, str) and question.strip() for question in questions), "openUpstreamQuestions entries must be non-empty")


def load_and_validate(path: Path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractValidationError(f"could not read {path}: {error}") from error
    _require(isinstance(data, dict), "contract root must be an object")
    validate_contract_data(data)
    return data


def main(argv: list[str] | None = None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=Path(__file__).with_name("provider-contracts.json"),
        help="path to the provider contract JSON",
    )
    args = parser.parse_args(argv)

    try:
        data = load_and_validate(args.path)
    except ContractValidationError as error:
        print(f"provider contract validation failed: {error}", file=sys.stderr)
        return 1

    print(
        f"validated {len(data['contracts'])} contracts across "
        f"{len(data['deployments'])} deployments"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
