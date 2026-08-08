import copy
import json
import sys
import unittest
import urllib.error
from pathlib import Path
from unittest import mock


CONTRACT_DIR = Path(__file__).resolve().parent
if str(CONTRACT_DIR) not in sys.path:
    sys.path.insert(0, str(CONTRACT_DIR))

import run_live_contracts
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
        self.assertGreater(len(validated["nativeSiloContract"]["requirements"]), 20)

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

    def test_native_health_allows_omitted_server_id(self):
        data = copy.deepcopy(self.valid_data)
        detection = data["nativeSiloContract"]["detection"]
        self.assertNotIn("server_id", detection["requiredFields"])
        self.assertIn("server_id", detection["optionalFields"])
        validate_contract_data(data)
    def test_rejects_native_route_shape_drift(self):
        data = copy.deepcopy(self.valid_data)
        page = next(
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] == "native.catalog.page"
        )
        page["path"] = "/api/v1/catalog/search"

        with self.assertRaisesRegex(ContractValidationError, "route shape drifted"):
            validate_contract_data(data)

    def test_rejects_native_auth_and_catalog_shape_drift(self):
        data = copy.deepcopy(self.valid_data)
        login = next(
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] == "native.auth.login"
        )
        login["requiredSemantics"] = ["HTTP 200"]

        with self.assertRaisesRegex(ContractValidationError, "not only an HTTP status"):
            validate_contract_data(data)

    def test_native_deployment_is_distinct_from_compatibility_mode(self):
        native = next(
            deployment
            for deployment in self.valid_data["deployments"]
            if deployment["surface"] == "silo-native-v1"
        )
        compatibility = next(
            deployment
            for deployment in self.valid_data["deployments"]
            if deployment["id"] == "silo-8044eb8-compat"
        )
        self.assertEqual(native["protocolMode"], "native")
        self.assertNotEqual(native["id"], compatibility["id"])
        self.assertNotEqual(native["supportLabel"], compatibility["supportLabel"])
        validate_contract_data(self.valid_data)

    def test_rejects_native_contract_without_live_probe(self):
        data = copy.deepcopy(self.valid_data)
        for requirement in data["nativeSiloContract"]["requirements"]:
            requirement.pop("liveProbe", None)

        with self.assertRaisesRegex(ContractValidationError, "at least one liveProbe"):
            validate_contract_data(data)

    def test_live_runner_rejects_empty_selected_surface(self):
        data = copy.deepcopy(self.valid_data)
        for requirement in data["nativeSiloContract"]["requirements"]:
            requirement["liveProbe"] = False

        with mock.patch.object(run_live_contracts, "load_and_validate", return_value=data):
            with self.assertRaises(SystemExit) as raised:
                run_live_contracts.main(
                    [
                        "--deployment",
                        "silo-8044eb8-native",
                        "--base-url",
                        "https://silo.example",
                    ]
                )

        self.assertEqual(raised.exception.code, 2)



    def test_rejects_missing_native_requirement(self):
        data = copy.deepcopy(self.valid_data)
        data["nativeSiloContract"]["requirements"] = [
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] != "native.playback.track"
        ]

        with self.assertRaisesRegex(ContractValidationError, "coverageRequirements and requirements must match"):
            validate_contract_data(data)

    def test_rejects_native_evidence_from_another_revision(self):
        data = copy.deepcopy(self.valid_data)
        data["nativeSiloContract"]["requirements"][0]["evidenceSources"] = [
            "https://github.com/Silo-Server/silo-server/blob/main/internal/api/handlers/health.go"
        ]

        with self.assertRaisesRegex(ContractValidationError, "pinned to native sourceRevision"):
            validate_contract_data(data)

    def test_rejects_unavailable_capability_labeled_supported(self):
        data = copy.deepcopy(self.valid_data)
        trickplay = next(
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] == "native.trickplay"
        )
        trickplay["outcome"] = "supported"

        with self.assertRaisesRegex(ContractValidationError, "unavailable capability cannot be labeled supported"):
            validate_contract_data(data)

    def test_live_drivers_are_registered_by_protocol_surface(self):
        self.assertIn("mediabrowser-v1", DRIVERS)
        self.assertIn("silo-native-v1", DRIVERS)
        self.assertFalse(DRIVERS["silo-native-v1"].requires_credentials)
        self.assertNotIn("jellyfin-supported", DRIVERS)
        self.assertNotIn("silo-8044eb8-compat", DRIVERS)

    def test_live_response_parser_accepts_json_envelopes(self):
        response = Response(200, {"Content-Type": "application/json"}, b'{"Items": []}')
        self.assertEqual(response.json(), {"Items": []})
        malformed = Response(200, {"Content-Type": "text/html"}, b"not-json")
        self.assertIsNone(malformed.json())

    def test_native_live_probe_is_read_only_and_accepts_omitted_server_id(self):
        class RecordingTransport:
            def __init__(self):
                self.calls = []

            def request(self, method, path, **kwargs):
                self.calls.append((method, path, kwargs))
                return Response(200, {"Content-Type": "application/json"}, b'{"status": "ok"}')

        transport = RecordingTransport()
        probe = DRIVERS["silo-native-v1"](transport, "", "", "0.0-contract")
        results = probe.run({"native.health": "partial"}, allow_mutations=False)

        self.assertEqual([(call[0], call[1]) for call in transport.calls], [("GET", "/api/v1/health")])
        self.assertEqual(len(results), 1)
        self.assertTrue(results[0].passed)
        self.assertIn("server_id=omitted", results[0].evidence)

    def test_native_live_probe_rejects_success_shaped_bad_health(self):
        class BadHealthTransport:
            def request(self, method, path, **kwargs):
                return Response(200, {"Content-Type": "application/json"}, b'{"status": "degraded"}')

        probe = DRIVERS["silo-native-v1"](BadHealthTransport(), "", "", "0.0-contract")
        result = probe.run({"native.health": "partial"}, allow_mutations=False)[0]

        self.assertEqual(result.observed, "missing")
        self.assertFalse(result.passed)

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
