#!/usr/bin/env python3

import argparse
import base64
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
from urllib.parse import urlparse
from pathlib import Path

ALLOWED_CHANNELS = {"stable", "dev"}
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
SHA256_RE = re.compile(r"^[A-Fa-f0-9]{64}$")
KEY_ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
RELEASE_URL_PREFIX = "/crowquillx/Bloom/releases/download/"


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
    if (
        not normalized
        or "/" in normalized
        or "\\" in normalized
        or ".." in normalized
        or Path(normalized).name != normalized
    ):
        raise ValueError(f"{arg_name} must be a safe basename")
    return normalized


def validate_url(value: str, arg_name: str) -> str:
    normalized = value.strip()
    parsed = urlparse(normalized)
    if (
        not normalized
        or parsed.scheme != "https"
        or parsed.hostname != "github.com"
        or parsed.port not in {None, 443}
        or parsed.username is not None
        or parsed.password is not None
        or not parsed.path.startswith(RELEASE_URL_PREFIX)
    ):
        raise ValueError(f"{arg_name} must be an official Bloom HTTPS release URL")
    return normalized


def sign_payload(payload: bytes, signing_key_file: str) -> bytes:
    payload_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", delete=False) as payload_file:
            payload_path = Path(payload_file.name)
            payload_file.write(payload)
        result = subprocess.run(
            [
                "openssl",
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                signing_key_file,
                "-in",
                payload_path,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        raise ValueError(f"--signing-key-file could not invoke OpenSSL: {exc}") from exc
    finally:
        if payload_path is not None:
            payload_path.unlink(missing_ok=True)
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()[:500]
        raise ValueError(f"--signing-key-file could not sign the manifest: {detail}")
    if len(result.stdout) != 64:
        raise ValueError("--signing-key-file did not produce an Ed25519 signature")
    return result.stdout


def atomic_write(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=path.parent, prefix=f".{path.name}.", delete=False
        ) as output:
            temporary_path = Path(output.name)
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


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
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--signing-key-file", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    normalized_channel = args.channel.strip()
    if normalized_channel not in ALLOWED_CHANNELS:
        return fail("args.channel", f"must be one of: {', '.join(sorted(ALLOWED_CHANNELS))}")

    try:
        version = validate_semver(args.version, "args.version")
        normalized_build_id = validate_filename(args.build_id, "args.build_id")
        normalized_release_tag = validate_filename(args.release_tag, "args.release_tag")
        published_at = validate_iso8601(args.published_at, "args.published_at")
        installer_url = validate_url(args.installer_url, "args.installer_url")
        portable_url = validate_url(args.portable_url, "args.portable_url")
        installer_filename = validate_filename(args.installer_filename, "args.installer_filename")
        portable_filename = validate_filename(args.portable_filename, "args.portable_filename")
        installer_sha256 = validate_sha256(args.installer_sha256, "args.installer_sha256")
        portable_sha256 = validate_sha256(args.portable_sha256, "args.portable_sha256")
        minimum_supported_version = validate_semver(args.minimum_supported_version, "args.minimum_supported_version")
        key_id = args.key_id.strip()
        if not KEY_ID_RE.fullmatch(key_id):
            raise ValueError("args.key_id must be a lowercase key identifier")
    except ValueError as exc:
        arg_name, _, message = str(exc).partition(" must ")
        return fail(arg_name, f"must {message}" if message else str(exc))

    notes = ""
    if args.notes_file:
        try:
            notes = Path(args.notes_file).read_text(encoding="utf-8").strip()
        except (OSError, UnicodeDecodeError) as exc:
            return fail("--notes-file", str(exc))

    payload = {
        "channel": normalized_channel,
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

    try:
        payload_bytes = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        ).encode("utf-8")
        signature = sign_payload(payload_bytes, args.signing_key_file)
        envelope = {
            "schema": 2,
            "key_id": key_id,
            "payload": base64.b64encode(payload_bytes).decode("ascii"),
            "signature": base64.b64encode(signature).decode("ascii"),
        }
        output_bytes = (json.dumps(envelope, indent=2, sort_keys=True) + "\n").encode("utf-8")
        atomic_write(Path(args.output), output_bytes)
    except (OSError, ValueError) as exc:
        return fail("--output", str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
