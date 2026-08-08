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
    "native.profiles.list",
    "native.profiles.pin",
    "native.catalog.libraries",
    "native.catalog.page",
    "native.catalog.query",
    "native.catalog.detail",
    "native.catalog.hierarchy",
    "native.state.watched",
    "native.state.favorite",
    "native.artwork.refetch",
    "native.playback.capability",
    "native.playback.start-legacy",
    "native.playback.progress",
    "native.playback.stop",
    "native.playback.transcode",
    "native.playback.track",
    "native.markers",
    "native.chapters",
    "native.trickplay",
    "native.theme-songs",
}
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
DIGEST_IMAGE_RE = re.compile(r"^[^\s@]+@sha256:[0-9a-f]{64}$")
HTTP_STATUS_RE = re.compile(r"\bHTTP\s+\d{3}\b", re.IGNORECASE)


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
        if deployment.get("product") == "Silo Server" and deployment.get("surface") == "silo-native-v1":
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
        if deployment.get("product") == "Silo Server" and deployment.get("surface") == "silo-native-v1"
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
    _require(SHA_RE.fullmatch(snapshot.get("siloRevision", "")) is not None, "snapshot siloRevision must be a full Git SHA")
    _require(DIGEST_IMAGE_RE.fullmatch(snapshot.get("siloImage", "")) is not None, "snapshot siloImage must use an immutable sha256 digest")
    _require(snapshot.get("siloImageTag") == snapshot["siloRevision"][:7], "snapshot siloImageTag must match the Silo revision")

    native = data.get("nativeSiloContract")
    _require(isinstance(native, dict), "nativeSiloContract must be an object")
    for field in ("sourceRevision", "detection", "requiredHeaders", "identityRules", "coverageRequirements", "requirements", "playbackDecision", "headResearch"):
        _require(bool(native.get(field)), f"nativeSiloContract needs {field}")
    _require(native["sourceRevision"] == snapshot["siloRevision"], "native sourceRevision must match the pinned Silo revision")
    _require(SHA_RE.fullmatch(native["sourceRevision"]) is not None, "native sourceRevision must be a full Git SHA")
    for field in ("requiredHeaders", "identityRules"):
        values = native.get(field)
        _require(isinstance(values, list) and values, f"nativeSiloContract {field} must be a non-empty array")
        _require(all(isinstance(value, str) and value.strip() for value in values), f"nativeSiloContract {field} entries must be non-empty")
    _require(any("Bearer" in header for header in native["requiredHeaders"]), "native requiredHeaders must include bearer authentication")
    _require(any("X-Profile-Id" in header for header in native["requiredHeaders"]), "native requiredHeaders must include profile selection")
    _require(any("X-Profile-Token" in header for header in native["requiredHeaders"]), "native requiredHeaders must include PIN verification")
    required_header_terms = (
        "X-Silo-Client",
        "X-Silo-Client-Version",
        "X-Silo-Device-Id",
        "X-Silo-Device-Name",
        "X-Silo-Device-Platform",
    )
    _require(
        all(any(term in header for header in native["requiredHeaders"]) for term in required_header_terms),
        "native requiredHeaders must include the complete Silo client/device identity",
    )
    identity_text = " ".join(native["identityRules"]).lower().replace("_", " ")
    _require("content id" in identity_text, "native identityRules must preserve content_id identity")
    _require("file id" in identity_text, "native identityRules must preserve file_id identity")
    _require("millisecond" in identity_text, "native identityRules must state canonical millisecond conversion")

    detection = native.get("detection")
    _require(isinstance(detection, dict), "nativeSiloContract detection must be an object")
    _require(detection.get("method") == "GET" and detection.get("path") == "/api/v1/health", "native detection must use GET /api/v1/health")
    _require("status" in detection.get("requiredFields", []), "native health detection must require status")
    _require("server_id" not in detection.get("requiredFields", []), "native health detection must not require optional server_id")
    _require("server_id" in detection.get("optionalFields", []), "native health detection must describe optional server_id")
    _require(detection.get("requiredValues", {}).get("status") == "ok", "native health detection must require status=ok")
    server_id_policy = detection.get("serverIdPolicy", "").lower()
    native_route_shapes = {
        "native.health": ("GET", "/api/v1/health"),
        "native.auth.providers": ("GET", "/api/v1/auth/providers"),
        "native.auth.login": ("POST", "/api/v1/auth/login"),
        "native.auth.errors": ("GET/POST/DELETE", "/api/v1/auth/*"),
        "native.auth.refresh": ("POST", "/api/v1/auth/refresh"),
        "native.auth.me": ("GET", "/api/v1/auth/me"),
        "native.auth.logout": ("POST", "/api/v1/auth/logout"),
        "native.auth.sessions": ("GET/DELETE", "/api/v1/auth/sessions and /api/v1/auth/sessions/{id}"),
        "native.profiles.list": ("GET", "/api/v1/profiles"),
        "native.profiles.pin": ("POST", "/api/v1/profiles/{id}/verify-pin"),
        "native.catalog.libraries": ("GET", "/api/v1/user/libraries"),
        "native.catalog.page": ("GET", "/api/v1/catalog"),
        "native.catalog.query": ("POST", "/api/v1/catalog/query"),
        "native.catalog.detail": ("GET", "/api/v1/catalog/items/{id} and /api/v1/catalog/items/{id}/versions"),
        "native.catalog.hierarchy": ("GET", "/api/v1/catalog/items/{id}/episodes and /api/v1/catalog/series/{id}/seasons*"),
        "native.state.watched": ("POST/DELETE", "/api/v1/watched/{id}"),
        "native.state.favorite": ("GET/PUT/DELETE", "/api/v1/favorites/{item_id}"),
        "native.artwork.refetch": ("GET", "opaque URLs from catalog, detail, person, and chapter resources"),
    }
    native_shape_terms = {
        "native.auth.providers": ("id", "display name", "mode", "default"),
        "native.auth.login": ("access token", "refresh token", "expires in", "user", "username"),
        "native.auth.errors": ("error", "message", "invalid credentials", "invalid token", "session revoked"),
        "native.auth.me": ("id", "username", "role", "permissions", "download allowed"),
        "native.profiles.list": ("profiles", "avatar upload enabled", "has pin", "is child", "is primary"),
        "native.profiles.pin": ("valid", "profile token", "expires at"),
        "native.catalog.libraries": ("profile accessible", "library id"),
        "native.catalog.page": ("items", "total", "total exact", "has more", "snapshot"),
        "native.catalog.query": ("items", "total", "total exact", "has more", "snapshot"),
        "native.catalog.detail": ("content id", "provider ids", "user state", "versions", "playback variants"),
        "native.catalog.hierarchy": ("content id", "ordered seasons", "episodes"),
        "native.state.watched": ("content id", "affected count", "played"),
        "native.state.favorite": ("204", "idempotent", "user state"),
        "native.artwork.refetch": ("artwork urls", "fetch locations", "refetch", "signed query"),
    }
    _require(
        all(term in server_id_policy for term in ("optional", "deterministic", "unique")),
        "native health detection must document the optional deterministic server_id caveat",
    )

    native_coverage = native.get("coverageRequirements")
    native_requirements = native.get("requirements")
    _require(isinstance(native_coverage, list) and native_coverage, "native coverageRequirements must be a non-empty array")
    _require(len(native_coverage) == len(set(native_coverage)), "native coverageRequirements must be unique")
    _require(set(native_coverage) == REQUIRED_NATIVE_COVERAGE, "native coverageRequirements must cover the #77-#80 baseline")
    _require(isinstance(native_requirements, list) and native_requirements, "native requirements must be a non-empty array")
    native_ids = _unique_ids(native_requirements, "native requirement")
    _require(native_ids == set(native_coverage), "native coverageRequirements and requirements must match")

    evidence_prefix = f"https://github.com/Silo-Server/silo-server/blob/{native['sourceRevision']}/"
    for requirement in native_requirements:
        requirement_id = requirement["id"]
        _require(requirement.get("issue") in {77, 78, 79, 80}, f"{requirement_id} must reference issue 77, 78, 79, or 80")
        for field in ("method", "path"):
            _require(isinstance(requirement.get(field), str) and requirement[field].strip(), f"{requirement_id} needs {field}")
        if requirement_id in native_route_shapes:
            expected_method, expected_path = native_route_shapes[requirement_id]
            _require(
                (requirement.get("method"), requirement.get("path")) == (expected_method, expected_path),
                f"{requirement_id} route shape drifted from the pinned native contract",
            )
        _require(requirement.get("outcome") in ALLOWED_OUTCOMES, f"{requirement_id} has an invalid outcome")

        capability = requirement.get("capability")
        _require(isinstance(capability, dict), f"{requirement_id} capability must be an object")
        capability_state = capability.get("state")
        _require(capability_state in NATIVE_CAPABILITY_STATES, f"{requirement_id} has an invalid capability state")
        _require(isinstance(capability.get("discovery"), str) and capability["discovery"].strip(), f"{requirement_id} needs capability discovery evidence")
        if capability_state == "unavailable":
            _require(requirement["outcome"] in {"missing", "not-applicable"}, f"{requirement_id} unavailable capability cannot be labeled supported")

        request_semantics = requirement.get("requestSemantics")
        required_semantics = requirement.get("requiredSemantics")
        _require(isinstance(request_semantics, list) and request_semantics, f"{requirement_id} needs requestSemantics")
        _require(isinstance(required_semantics, list) and required_semantics, f"{requirement_id} needs requiredSemantics")
        _require(
            all(isinstance(rule, str) and rule.strip() for rule in request_semantics + required_semantics),
            f"{requirement_id} semantics must be non-empty strings",
        )
        _require(
            any(_has_behavior_semantics(rule) for rule in required_semantics),
            f"{requirement_id} must assert payload or behavior semantics, not only an HTTP status",
        )
        if requirement_id in native_shape_terms:
            semantic_text = " ".join(request_semantics + required_semantics).lower()
            semantic_text = re.sub(r"[_-]+", " ", semantic_text)
            missing_terms = [term for term in native_shape_terms[requirement_id] if term not in semantic_text]
            _require(
                not missing_terms,
                f"{requirement_id} is missing deterministic shape semantics: {', '.join(missing_terms)}",
            )

        evidence_sources = requirement.get("evidenceSources")
        _require(isinstance(evidence_sources, list) and evidence_sources, f"{requirement_id} needs pinned evidenceSources")
        _require(
            all(isinstance(url, str) and url.startswith(evidence_prefix) for url in evidence_sources),
            f"{requirement_id} evidenceSources must be pinned to native sourceRevision",
        )
        _require(isinstance(requirement.get("liveProbe", False), bool), f"{requirement_id} liveProbe must be boolean")
        if requirement.get("liveProbe", False):
            _require(
                requirement["method"] == "GET" and requirement["path"] == "/api/v1/health",
                f"{requirement_id} liveProbe must be deterministic and read-only",
            )

    _require(
        any(requirement.get("liveProbe", False) for requirement in native_requirements),
        "native requirements must include at least one liveProbe",
    )
    _require(native_ids.issuperset({"native.trickplay", "native.theme-songs"}), "native baseline must name unsupported optional capabilities")
    by_native_id = {requirement["id"]: requirement for requirement in native_requirements}
    for unsupported_id in ("native.trickplay", "native.theme-songs"):
        _require(by_native_id[unsupported_id]["capability"]["state"] == "unavailable", f"{unsupported_id} must remain explicitly unavailable")

    head_research = native.get("headResearch")
    _require(isinstance(head_research, dict), "native headResearch must be an object")
    _require(SHA_RE.fullmatch(head_research.get("revision", "")) is not None, "native headResearch revision must be a full Git SHA")
    _require(head_research.get("status") == "research-only-not-release-validated", "native headResearch must not claim release validation")
    _require(isinstance(head_research.get("finding"), str) and head_research["finding"].strip(), "native headResearch needs a finding")
    _require(
        isinstance(head_research.get("source"), str) and f"/blob/{head_research['revision']}/" in head_research["source"],
        "native headResearch source must be pinned to its revision",
    )
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
