#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


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

    notes = ""
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8").strip()

    payload = {
        "channel": args.channel,
        "version": args.version,
        "build_id": args.build_id,
        "release_tag": args.release_tag,
        "published_at": args.published_at,
        "notes": notes,
        "installer": {
            "url": args.installer_url,
            "filename": args.installer_filename,
            "sha256": args.installer_sha256.lower(),
        },
        "portable": {
            "url": args.portable_url,
            "filename": args.portable_filename,
            "sha256": args.portable_sha256.lower(),
        },
        "rollout": {
            "enabled": True,
            "minimum_supported_version": args.minimum_supported_version,
        },
    }

    output_path = Path(args.output)
    output_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
