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

    media_browser_deployments = {
        deployment["id"] for deployment in deployments if deployment.get("surface") == "mediabrowser-v1"
    }
    _require(media_browser_deployments, "at least one mediabrowser-v1 deployment is required")

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
    for field in ("detection", "authenticationRoutes", "profileRoutes", "catalogRoutes", "playbackRoutes", "requiredHeaders", "identityRules", "playbackDecision"):
        _require(bool(native.get(field)), f"nativeSiloContract needs {field}")
    _require(isinstance(native.get("detection"), dict), "nativeSiloContract detection must be an object")
    _require(native["detection"].get("path") == "/api/v1/health", "native detection must use /api/v1/health")
    _require("server_id" in native["detection"].get("requiredFields", []), "native health detection must require server_id")

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
