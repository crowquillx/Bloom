#!/usr/bin/env python3
"""Generate Bloom's reviewable Jellyfin OpenAPI surface manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


HTTP_METHODS = {
    "delete",
    "get",
    "head",
    "options",
    "patch",
    "post",
    "put",
    "trace",
}


def _resolve(document: dict[str, Any], value: dict[str, Any]) -> dict[str, Any]:
    reference = value.get("$ref")
    if not isinstance(reference, str) or not reference.startswith("#/components/"):
        return value
    resolved: Any = document
    for segment in reference.removeprefix("#/").split("/"):
        resolved = resolved[segment]
    if not isinstance(resolved, dict):
        raise ValueError(f"OpenAPI reference is not an object: {reference}")
    return resolved


def _schema_reference(schema: Any) -> str:
    if not isinstance(schema, dict):
        return ""
    reference = schema.get("$ref")
    if isinstance(reference, str):
        return reference.removeprefix("#/components/schemas/")
    return str(schema.get("type", ""))


def generate_manifest(document: dict[str, Any], source: str, source_sha256: str):
    operations = []
    for route, path_item in sorted(document.get("paths", {}).items()):
        if not isinstance(path_item, dict):
            continue
        path_parameters = path_item.get("parameters", [])
        for method, operation in sorted(path_item.items()):
            if method not in HTTP_METHODS or not isinstance(operation, dict):
                continue
            parameters = []
            for raw_parameter in [*path_parameters, *operation.get("parameters", [])]:
                parameter = _resolve(document, raw_parameter)
                parameters.append(
                    {
                        "deprecated": bool(parameter.get("deprecated", False)),
                        "in": parameter.get("in", ""),
                        "name": parameter.get("name", ""),
                        "required": bool(parameter.get("required", False)),
                    }
                )
            request_body = _resolve(document, operation.get("requestBody", {}))
            content = request_body.get("content", {}) if request_body else {}
            request_schemas = sorted(
                {
                    _schema_reference(media.get("schema"))
                    for media in content.values()
                    if isinstance(media, dict) and _schema_reference(media.get("schema"))
                }
            )
            operations.append(
                {
                    "deprecated": bool(operation.get("deprecated", False)),
                    "method": method.upper(),
                    "operationId": operation.get("operationId", ""),
                    "parameters": sorted(
                        parameters, key=lambda item: (item["in"], item["name"])
                    ),
                    "path": route,
                    "requestSchemas": request_schemas,
                }
            )

    schemas = {}
    for name, raw_schema in sorted(
        document.get("components", {}).get("schemas", {}).items()
    ):
        schema = _resolve(document, raw_schema)
        properties = {}
        for property_name, raw_property in sorted(schema.get("properties", {}).items()):
            prop = _resolve(document, raw_property)
            properties[property_name] = {
                "deprecated": bool(prop.get("deprecated", False)),
                "reference": _schema_reference(raw_property),
                "type": prop.get("type", ""),
            }
        schemas[name] = {
            "deprecated": bool(schema.get("deprecated", False)),
            "enum": schema.get("enum", []),
            "properties": properties,
            "required": sorted(schema.get("required", [])),
            "type": schema.get("type", ""),
        }

    return {
        "apiVersion": document.get("info", {}).get("version", ""),
        "openapiVersion": document.get("openapi", ""),
        "operations": operations,
        "schemaVersion": 1,
        "schemas": schemas,
        "source": source,
        "sourceSha256": source_sha256,
    }


def main(argv: list[str] | None = None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="downloaded upstream openapi.json")
    parser.add_argument(
        "--contracts",
        type=Path,
        default=Path(__file__).with_name("provider-contracts.json"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).with_name("jellyfin-12-openapi-manifest.json"),
    )
    args = parser.parse_args(argv)

    snapshot = json.loads(args.contracts.read_text(encoding="utf-8"))["snapshot"]
    source_bytes = args.input.read_bytes()
    actual_sha256 = hashlib.sha256(source_bytes).hexdigest()
    expected_sha256 = snapshot["jellyfinOpenApiSha256"]
    if actual_sha256 != expected_sha256:
        raise SystemExit(
            f"OpenAPI SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}"
        )
    document = json.loads(source_bytes)
    manifest = generate_manifest(
        document, snapshot["jellyfinOpenApiSource"], actual_sha256
    )
    if manifest["apiVersion"] != snapshot["jellyfinOpenApiVersion"]:
        raise SystemExit(
            "OpenAPI version mismatch: "
            f"expected {snapshot['jellyfinOpenApiVersion']}, "
            f"got {manifest['apiVersion']}"
        )
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
