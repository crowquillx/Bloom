#!/usr/bin/env python3
"""Extract Bloom's Silo v2 operations and schemas from pinned server artifacts."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

CONTRACT_DIR = Path(__file__).resolve().parent


def compact(value):
    if isinstance(value, dict):
        return {key: compact(item) for key, item in value.items()
                if key not in {"description", "examples", "example", "patternDescription"}}
    if isinstance(value, list):
        return [compact(item) for item in value]
    return value


def generate(openapi_bytes, migration_bytes, baseline):
    snapshot = baseline["snapshot"]
    for label, content, pin in (("OpenAPI", openapi_bytes, "siloOpenApiSha256"),
                                ("migration", migration_bytes, "siloMigrationSha256")):
        if hashlib.sha256(content).hexdigest() != snapshot[pin]:
            raise ValueError(f"Silo {label} digest does not match the pin")
    document = json.loads(openapi_bytes)
    migration = json.loads(migration_bytes)
    if document["info"]["version"] != "2" or not migration.get("entries"):
        raise ValueError("Expected the stable v2 contract and migration inventory")
    operations = []
    schemas = {}

    def include_schemas(value):
        if isinstance(value, dict):
            reference = value.get("$ref", "")
            if reference.startswith("#/components/schemas/"):
                name = reference.rsplit("/", 1)[1]
                if name not in schemas:
                    schemas[name] = compact(document["components"]["schemas"][name])
                    include_schemas(schemas[name])
            for item in value.values():
                include_schemas(item)
        elif isinstance(value, list):
            for item in value:
                include_schemas(item)

    selected = {(op["method"], op["path"])
                for requirement in baseline["nativeSiloContract"]["requirements"]
                for op in requirement["operations"]}
    selected.update(("GET", path) for path in (
        "/api/v2/home/sections", "/api/v2/home/sections/{id}/items",
        "/api/v2/library/{id}/sections", "/api/v2/library/{id}/sections/{section_id}/items",
        "/api/v2/catalog/filters", "/api/v2/catalog/filters/search",
        "/api/v2/recommendations/similar/{item_id}", "/api/v2/stream/{session_id}",
        "/api/v2/stream/{session_id}/subtitles/{track}"))
    for method, path in sorted(selected):
        operation = document["paths"][path][method.lower()]
        item = {"method": method, "path": path, "operationId": operation["operationId"],
                "parameters": compact(operation.get("parameters", [])),
                "requestBody": compact(operation.get("requestBody", {})),
                "responses": compact(operation["responses"]),
                "conditional": operation.get("x-silo-conditional", False),
                "class": operation.get("x-silo-class"),
                "deprecated": operation.get("deprecated", False)}
        operations.append(item)
        include_schemas(item)
    return {"schemaVersion": 1, "sourceRevision": snapshot["siloRevision"],
            "source": snapshot["siloOpenApiSource"], "sourceSha256": snapshot["siloOpenApiSha256"],
            "migrationSource": snapshot["siloMigrationSource"],
            "migrationSha256": snapshot["siloMigrationSha256"], "apiVersion": "2",
            "pathCount": len(document["paths"]), "operations": operations,
            "schemas": dict(sorted(schemas.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("openapi", type=Path)
    parser.add_argument("migration", type=Path)
    parser.add_argument("--output", type=Path, default=CONTRACT_DIR / "silo-v2-openapi-manifest.json")
    args = parser.parse_args()
    baseline = json.loads((CONTRACT_DIR / "provider-contracts.json").read_text())
    manifest = generate(args.openapi.read_bytes(), args.migration.read_bytes(), baseline)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
