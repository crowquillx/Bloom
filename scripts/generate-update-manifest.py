#!/usr/bin/env python3

import argparse
import datetime
import json
import re
import sys
from urllib.parse import urlparse
from pathlib import Path

ALLOWED_CHANNELS = {"stable", "dev"}
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
SHA256_RE = re.compile(r"^[A-Fa-f0-9]{64}$")


def fail(arg_name: str, message: str) -> int:
    print(f"error: {arg_name}: {message}", file=sys.stderr)
    return 1


def validate_semver(value: str, arg_name: str) -> str:
    if not SEMVER_RE.fullmatch(value.strip()):
        raise ValueError(f"{arg_name} must be a semantic version")
    return value.strip()


def validate_iso8601(value: str, arg_name: str) -> str:
    normalized = value.strip()
    try:
        datetime.datetime.fromisoformat(normalized.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError(f"{arg_name} must be an ISO8601 datetime") from exc
    return normalized


def validate_sha256(value: str, arg_name: str) -> str:
    normalized = value.strip()
    if not SHA256_RE.fullmatch(normalized):
        raise ValueError(f"{arg_name} must be a 64-character SHA-256 hex string")
    return normalized.lower()


def validate_filename(value: str, arg_name: str) -> str:
    normalized = value.strip()
    if not normalized:
        raise ValueError(f"{arg_name} must not be empty")
    return normalized


def validate_url(value: str, arg_name: str) -> str:
    normalized = value.strip()
    parsed = urlparse(normalized)
    if not normalized or parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(f"{arg_name} must be a well-formed http(s) URL")
    return normalized


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Bloom update manifest JSON.")
    parser.add_argument("--channel", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--release-tag", required=True)
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--notes-file")
    parser.add_argument("--installer-url", required=True)
    parser.add_argument("--installer-filename", required=True)
    parser.add_argument("--installer-sha256", required=True)
    parser.add_argument("--portable-url", required=True)
    parser.add_argument("--portable-filename", required=True)
    parser.add_argument("--portable-sha256", required=True)
    parser.add_argument("--minimum-supported-version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if args.channel not in ALLOWED_CHANNELS:
        return fail("args.channel", f"must be one of: {', '.join(sorted(ALLOWED_CHANNELS))}")

    try:
        version = validate_semver(args.version, "args.version")
        published_at = validate_iso8601(args.published_at, "args.published_at")
        installer_url = validate_url(args.installer_url, "args.installer_url")
        portable_url = validate_url(args.portable_url, "args.portable_url")
        installer_filename = validate_filename(args.installer_filename, "args.installer_filename")
        portable_filename = validate_filename(args.portable_filename, "args.portable_filename")
        installer_sha256 = validate_sha256(args.installer_sha256, "args.installer_sha256")
        portable_sha256 = validate_sha256(args.portable_sha256, "args.portable_sha256")
        minimum_supported_version = validate_semver(args.minimum_supported_version, "args.minimum_supported_version")
    except ValueError as exc:
        arg_name, _, message = str(exc).partition(" must ")
        return fail(arg_name, f"must {message}" if message else str(exc))

    notes = ""
    if args.notes_file:
        try:
            notes = Path(args.notes_file).read_text(encoding="utf-8").strip()
        except Exception as exc:
            return fail(args.notes_file, str(exc))

    normalized_build_id = args.build_id.strip()
    normalized_release_tag = args.release_tag.strip()

    payload = {
        "channel": args.channel,
        "version": version,
        "build_id": normalized_build_id,
        "release_tag": normalized_release_tag,
        "published_at": published_at,
        "notes": notes,
        "installer": {
            "url": installer_url,
            "filename": installer_filename,
            "sha256": installer_sha256,
        },
        "portable": {
            "url": portable_url,
            "filename": portable_filename,
            "sha256": portable_sha256,
        },
        "rollout": {
            "enabled": True,
            "minimum_supported_version": minimum_supported_version,
        },
    }

    output_path = Path(args.output)
    output_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
