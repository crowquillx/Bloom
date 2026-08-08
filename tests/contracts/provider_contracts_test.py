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

    def test_issue_80_rows_cover_scope_and_canonical_outcomes(self):
        expected_ids = {
            "artwork.standard",
            "artwork.chapter",
            "playback.additional-parts",
            "playback.trickplay",
            "playback.report",
            "segments.plugin-intro-skipper",
            "segments.standard",
            "sessions.list",
            "sessions.revoke",
            "metadata.theme-songs",
            "catalog.recommendations",
            "catalog.server-sections",
            "playback.versions",
            "playback.multipart-reporting",
            "sessions.auth-playback-separation",
            "preferences.profile-track-local",
            "optional.household-overview",
            "optional.watchlist",
            "optional.watch-together",
            "optional.requests",
            "optional.notifications",
            "optional.downloads",
            "optional.audiobooks",
            "optional.ebooks",
        }
        rows = [contract for contract in self.valid_data["contracts"] if contract.get("issue") == 80]
        self.assertEqual({row["id"] for row in rows}, expected_ids)
        self.assertTrue(expected_ids.issubset(set(self.valid_data["coverageRequirements"])))

        deployments = {"jellyfin-supported", "silo-8044eb8-compat"}
        observed_outcomes = set()
        for row in rows:
            self.assertEqual(set(row["expectations"]), deployments, row["id"])
            self.assertTrue(row["requestSemantics"], row["id"])
            self.assertTrue(row["requiredSemantics"], row["id"])
            for expectation in row["expectations"].values():
                outcome = expectation["outcome"]
                self.assertIn(outcome, self.valid_data["outcomes"])
                observed_outcomes.add(outcome)
        self.assertTrue(
            {"supported", "partial", "missing", "not-applicable"}.issubset(observed_outcomes)
        )

        optional_rows = [row for row in rows if row["journey"] == "optional-follow-up"]
        self.assertEqual(len(optional_rows), 8)
        for row in optional_rows:
            self.assertTrue(
                all(
                    expectation["outcome"] == "not-applicable"
                    for expectation in row["expectations"].values()
                ),
                row["id"],
            )

    def test_canonical_unavailable_and_out_of_scope_labels_are_not_supported(self):
        labels = self.valid_data["outcomes"]
        self.assertIn("missing", labels)
        self.assertIn("not-applicable", labels)
        self.assertIn("unavailable", labels["missing"].lower())
        self.assertIn("out-of-scope", labels["not-applicable"].lower())
        rows = {row["id"]: row for row in self.valid_data["contracts"]}
        compat_missing = {
            "artwork.chapter",
            "playback.additional-parts",
            "playback.trickplay",
            "segments.plugin-intro-skipper",
            "sessions.revoke",
            "catalog.recommendations",
            "playback.versions",
            "playback.multipart-reporting",
            "sessions.auth-playback-separation",
        }
        for contract_id in compat_missing:
            self.assertEqual(
                rows[contract_id]["expectations"]["silo-8044eb8-compat"]["outcome"],
                "missing",
                contract_id,
            )

        for contract_id in (
            "optional.household-overview",
            "optional.watchlist",
            "optional.watch-together",
            "optional.requests",
            "optional.notifications",
            "optional.downloads",
            "optional.audiobooks",
            "optional.ebooks",
        ):
            self.assertTrue(
                all(
                    expectation["outcome"] == "not-applicable"
                    for expectation in rows[contract_id]["expectations"].values()
                ),
                contract_id,
            )

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
    def test_native_playback_routes_and_semantics_are_pinned(self):
        requirements = {
            requirement["id"]: requirement
            for requirement in self.valid_data["nativeSiloContract"]["requirements"]
        }
        expected_routes = {
            "native.playback.capability": ("GET", "/api/v1/playback/capability"),
            "native.playback.start-legacy": ("POST", "/api/v1/playback/start"),
            "native.playback.progress": ("POST", "/api/v1/playback/{session_id}/progress"),
            "native.playback.stop": ("DELETE", "/api/v1/playback/{session_id}"),
            "native.playback.track": ("PATCH", "/api/v1/playback/{session_id}/audio"),
        }
        for contract_id, (expected_method, expected_path) in expected_routes.items():
            with self.subTest(contract_id=contract_id):
                requirement = requirements[contract_id]
                self.assertEqual(requirement["method"], expected_method)
                self.assertEqual(requirement["path"], expected_path)
                semantics = " ".join(requirement["requestSemantics"] + requirement["requiredSemantics"]).lower()
                expected_term = {
                    "native.playback.capability": "protocol v3",
                    "native.playback.start-legacy": "file_id",
                    "native.playback.progress": "seconds",
                    "native.playback.stop": "tears down",
                    "native.playback.track": "audio_track_index",
                }[contract_id]
                self.assertIn(expected_term, semantics)

        start = requirements["native.playback.start-legacy"]
        start_semantics = " ".join(start["requestSemantics"] + start["requiredSemantics"]).lower()
        for term in ("start_position", "audio_track_index", "codec", "container", "hdr", "subtitle", "stream_url", "playback_info", "range", "multipart"):
            self.assertIn(term, start_semantics)

    def test_native_playback_pin_identity_and_implementation_evidence(self):
        native = self.valid_data["nativeSiloContract"]
        snapshot = self.valid_data["snapshot"]
        self.assertEqual(native["sourceRevision"], snapshot["siloRevision"])
        self.assertEqual(snapshot["siloImageTag"], snapshot["siloRevision"][:7])
        for requirement in native["requirements"]:
            if requirement["id"] in {"native.playback.start-legacy", "native.playback.progress", "native.playback.stop", "native.playback.track"}:
                self.assertTrue(requirement["implementationEvidence"])
                self.assertTrue(all(path.startswith("src/") for path in requirement["implementationEvidence"]))

    def test_rejects_native_playback_pin_drift(self):
        data = copy.deepcopy(self.valid_data)
        data["nativeSiloContract"]["sourceRevision"] = "0" * 40
        with self.assertRaisesRegex(ContractValidationError, "sourceRevision must match"):
            validate_contract_data(data)
    def test_native_state_roundtrips_are_opt_in_mutations(self):
        requirements = {
            requirement["id"]: requirement
            for requirement in self.valid_data["nativeSiloContract"]["requirements"]
        }
        for contract_id in ("native.state.watched", "native.state.favorite"):
            with self.subTest(contract_id=contract_id):
                self.assertTrue(requirements[contract_id]["liveProbe"])
                self.assertTrue(requirements[contract_id]["requiresMutationFlag"])
    def test_live_report_fails_when_driver_omits_expected_probe(self):
        results = [
            run_live_contracts.ProbeResult(
                contract="native.health",
                expected="partial",
                observed="partial",
                passed=True,
                evidence="health checked",
            )
        ]

        completed = run_live_contracts.complete_expected_results(
            {
                "native.health": "partial",
                "native.state.watched": "supported",
            },
            results,
        )

        self.assertEqual(
            [result.contract for result in completed],
            ["native.health", "native.state.watched"],
        )
        self.assertFalse(completed[1].passed)
        self.assertEqual(completed[1].observed, "missing")
        self.assertIn("omitted", completed[1].evidence)



    def test_rejects_native_route_shape_drift(self):
        data = copy.deepcopy(self.valid_data)
        page = next(
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] == "native.catalog.page"
        )
        page["path"] = "/api/v1/catalog/search"

        with self.assertRaisesRegex(
            ContractValidationError,
            r"native\.catalog\.page route shape drifted from the pinned native contract",
        ):
            validate_contract_data(data)

    def test_rejects_native_auth_and_catalog_shape_drift(self):
        data = copy.deepcopy(self.valid_data)
        login = next(
            requirement
            for requirement in data["nativeSiloContract"]["requirements"]
            if requirement["id"] == "native.auth.login"
        )
        login["requiredSemantics"] = ["HTTP 200"]

        with self.assertRaisesRegex(ContractValidationError, "missing deterministic shape semantics"):
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
            requirement.pop("requiresMutationFlag", None)

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
        invalid_utf8 = Response(200, {"Content-Type": "application/json"}, b"\xff")
        self.assertIsNone(invalid_utf8.json())

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
    def test_native_playback_probe_requires_explicit_mutation_flag(self):
        class RecordingTransport:
            def __init__(self):
                self.calls = []

            def request(self, method, path, **kwargs):
                self.calls.append((method, path, kwargs))
                return Response(200, {"Content-Type": "application/json"}, b'{"status": "ok"}')

        transport = RecordingTransport()
        probe = DRIVERS["silo-native-v1"](transport, "", "", "0.0-contract")
        expected = {
            "native.health": "partial",
            "native.playback.start-legacy": "supported",
            "native.playback.progress": "supported",
            "native.playback.stop": "supported",
            "native.playback.track": "supported",
        }
        results = probe.run(expected, allow_mutations=False)

        self.assertEqual([(call[0], call[1]) for call in transport.calls], [("GET", "/api/v1/health")])
        self.assertEqual({result.observed for result in results[1:]}, {"inconclusive"})
        self.assertTrue(all(result.passed for result in results))

    def test_native_playback_version_accepts_numeric_wire_identity(self):
        select_version = run_live_contracts.SiloNativeV1Probe._playback_version

        numeric = {"file_id": 17282}
        numeric_string = {"file_id": "17283"}
        single_track = {"file_id": 17284, "audio_tracks": [{}]}
        multi_track = {"file_id": 17285, "audio_tracks": [{}, {}]}
        self.assertIs(select_version([numeric]), numeric)
        self.assertIs(select_version([numeric_string]), numeric_string)
        self.assertIs(select_version([single_track, multi_track]), multi_track)
        self.assertIsNone(select_version([{"file_id": True}]))
        self.assertIsNone(select_version([{"file_id": -1}]))
        self.assertIsNone(select_version([{"file_id": "²"}]))
        self.assertIsNone(select_version([{"file_id": "9" * 5000}]))
        self.assertIsNone(select_version([{"file_id": "not-numeric"}]))


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
