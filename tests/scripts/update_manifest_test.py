#!/usr/bin/env python3

import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "scripts" / "generate-update-manifest.py"
VERIFIER = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None


class UpdateManifestGeneratorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.private_key = self.directory / "private.pem"
        self.public_key = self.directory / "public.pem"
        subprocess.run(
            ["openssl", "genpkey", "-algorithm", "ED25519", "-out", self.private_key],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                self.private_key,
                "-pubout",
                "-out",
                self.public_key,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def command(self, output: Path) -> list[str]:
        return [
            sys.executable,
            str(GENERATOR),
            "--channel",
            "stable",
            "--version",
            "1.2.3",
            "--build-id",
            "1.2.3",
            "--release-tag",
            "v1.2.3",
            "--published-at",
            "2026-08-11T00:00:00Z",
            "--installer-url",
            "https://github.com/crowquillx/Bloom/releases/download/v1.2.3/Bloom-Setup.exe",
            "--installer-filename",
            "Bloom-Setup.exe",
            "--installer-sha256",
            "a" * 64,
            "--portable-url",
            "https://github.com/crowquillx/Bloom/releases/download/v1.2.3/Bloom-Windows.zip",
            "--portable-filename",
            "Bloom-Windows.zip",
            "--portable-sha256",
            "b" * 64,
            "--minimum-supported-version",
            "1.2.3",
            "--key-id",
            "test-key",
            "--signing-key-file",
            str(self.private_key),
            "--output",
            str(output),
        ]

    def test_generates_verifiable_signed_envelope(self) -> None:
        output = self.directory / "stable.json"
        subprocess.run(self.command(output), check=True)
        envelope = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(envelope["schema"], 2)
        self.assertEqual(envelope["key_id"], "test-key")

        payload = base64.b64decode(envelope["payload"], validate=True)
        signature = base64.b64decode(envelope["signature"], validate=True)
        payload_file = self.directory / "payload.json"
        signature_file = self.directory / "signature.bin"
        payload_file.write_bytes(payload)
        signature_file.write_bytes(signature)
        subprocess.run(
            [
                "openssl",
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                self.public_key,
                "-in",
                payload_file,
                "-sigfile",
                signature_file,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        parsed_payload = json.loads(payload)
        self.assertEqual(parsed_payload["channel"], "stable")
        self.assertEqual(parsed_payload["installer"]["sha256"], "a" * 64)

        if VERIFIER is not None:
            public_der = subprocess.run(
                [
                    "openssl",
                    "pkey",
                    "-pubin",
                    "-in",
                    self.public_key,
                    "-outform",
                    "DER",
                ],
                check=True,
                stdout=subprocess.PIPE,
            ).stdout
            ed25519_spki_prefix = bytes.fromhex("302a300506032b6570032100")
            self.assertTrue(public_der.startswith(ed25519_spki_prefix))
            public_key = public_der[len(ed25519_spki_prefix) :]
            self.assertEqual(len(public_key), 32)
            subprocess.run(
                [
                    VERIFIER,
                    "--verify-update-envelope",
                    output,
                    "test-key",
                    public_key.hex(),
                ],
                check=True,
            )

    def test_rejects_insecure_or_foreign_asset_origins(self) -> None:
        for replacement in (
            "http://github.com/crowquillx/Bloom/releases/download/v1.2.3/Bloom-Setup.exe",
            "https://evil.example/Bloom-Setup.exe",
        ):
            with self.subTest(replacement=replacement):
                output = self.directory / "rejected.json"
                command = self.command(output)
                url_index = command.index("--installer-url") + 1
                command[url_index] = replacement
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertIn("official Bloom HTTPS release URL", result.stderr)

    def test_failed_signing_leaves_no_output_or_temporary_file(self) -> None:
        output = self.directory / "failed.json"
        command = self.command(output)
        key_index = command.index("--signing-key-file") + 1
        command[key_index] = str(self.directory / "missing.pem")
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertEqual(list(self.directory.glob(".failed.json.*")), [])


if __name__ == "__main__":
    unittest.main()
