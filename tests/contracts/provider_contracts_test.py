import copy
import json
import sys
import unittest
import urllib.error
from pathlib import Path

CONTRACT_DIR = Path(__file__).resolve().parent
if str(CONTRACT_DIR) not in sys.path:
    sys.path.insert(0, str(CONTRACT_DIR))

from run_live_contracts import DRIVERS, HttpTransport, Response
from validate_contracts import ContractValidationError, load_and_validate, validate_contract_data


class ProviderContractValidationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract_path = CONTRACT_DIR / "provider-contracts.json"
        cls.valid_data = json.loads(cls.contract_path.read_text(encoding="utf-8"))

    def test_checked_in_contract_is_valid(self):
        validated = load_and_validate(self.contract_path)
        self.assertGreater(len(validated["contracts"]), 20)

    def test_rejects_status_only_semantics(self):
        for rule in ("HTTP status is 200", "Returns HTTP 404"):
            with self.subTest(rule=rule):
                data = copy.deepcopy(self.valid_data)
                data["contracts"][0]["requiredSemantics"] = [rule]

                with self.assertRaisesRegex(ContractValidationError, "not only an HTTP status"):
                    validate_contract_data(data)

    def test_rejects_missing_deployment_expectation(self):
        data = copy.deepcopy(self.valid_data)
        del data["contracts"][0]["expectations"]["silo-8044eb8-compat"]

        with self.assertRaisesRegex(ContractValidationError, "every mediabrowser-v1 deployment"):
            validate_contract_data(data)

    def test_rejects_mutable_image_pin(self):
        data = copy.deepcopy(self.valid_data)
        data["snapshot"]["siloImage"] = "ghcr.io/silo-server/silo-server:latest"

        with self.assertRaisesRegex(ContractValidationError, "immutable sha256 digest"):
            validate_contract_data(data)

    def test_rejects_uncovered_required_gap(self):
        data = copy.deepcopy(self.valid_data)
        data["coverageRequirements"].append("playback.untracked-gap")

        with self.assertRaisesRegex(ContractValidationError, "missing contracts"):
            validate_contract_data(data)

    def test_rejects_non_object_native_detection(self):
        data = copy.deepcopy(self.valid_data)
        data["nativeSiloContract"]["detection"] = "present-but-invalid"

        with self.assertRaisesRegex(ContractValidationError, "detection must be an object"):
            validate_contract_data(data)

    def test_live_drivers_are_registered_by_protocol_surface(self):
        self.assertIn("mediabrowser-v1", DRIVERS)
        self.assertNotIn("jellyfin-supported", DRIVERS)
        self.assertNotIn("silo-8044eb8-compat", DRIVERS)

    def test_live_response_parser_accepts_json_envelopes(self):
        response = Response(200, {"Content-Type": "application/json"}, b'{"Items": []}')
        self.assertEqual(response.json(), {"Items": []})
        malformed = Response(200, {"Content-Type": "text/html"}, b"not-json")
        self.assertIsNone(malformed.json())

    def test_transport_converts_network_failure_to_response(self):
        class FailingOpener:
            def open(self, request, timeout):
                raise urllib.error.URLError("offline")

        transport = HttpTransport("https://media.example", 1)
        transport._opener = FailingOpener()
        response = transport.request("GET", "/System/Info")
        self.assertEqual(response.status, 0)
        self.assertIn("offline", response.headers["X-Bloom-Transport-Error"])

    def test_transport_only_trusts_configured_origin(self):
        transport = HttpTransport("https://media.example:8443", 1)
        self.assertTrue(transport.is_same_origin("/Videos/item/stream"))
        self.assertTrue(transport.is_same_origin("https://media.example:8443/Videos/item/stream"))
        self.assertFalse(transport.is_same_origin("https://cdn.example/Videos/item/stream"))
        self.assertFalse(transport.is_same_origin("http://media.example:8443/Videos/item/stream"))


if __name__ == "__main__":
    unittest.main()
