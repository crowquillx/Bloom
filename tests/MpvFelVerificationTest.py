import importlib.util
import os
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import uuid
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch


spec = importlib.util.spec_from_file_location(
    "fel_verification", Path(__file__).resolve().parents[1] / "scripts/verify-dolby-vision-fel.py"
)
verification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verification)


def make_client(sock=None, pipe=None):
    client = verification.IpcClient.__new__(verification.IpcClient)
    client._sock = sock
    client._pipe = pipe
    client._buffer = b""
    client._next_id = 1
    return client


class MpvFelVerificationTest(unittest.TestCase):
    def test_silent_pipe_expires_without_starting_a_blocking_read(self):
        pipe = Mock()
        client = make_client(pipe=pipe)
        started = time.monotonic()
        with patch.object(verification, "windows_pipe_bytes_available", return_value=0):
            with self.assertRaises(TimeoutError):
                client.command("get_property", "time-pos", timeout=0.05)
        self.assertLess(time.monotonic() - started, 2.0)
        pipe.read.assert_not_called()

    def test_pipe_reads_only_available_bytes_and_matches_the_reply(self):
        reply = b'{"request_id":1,"error":"success","data":7}\n'
        pipe = Mock()
        pipe.read.return_value = reply
        client = make_client(pipe=pipe)
        with patch.object(verification, "windows_pipe_bytes_available", side_effect=[0, len(reply)]):
            self.assertEqual(client.command("get_property", "time-pos", timeout=1), 7)
        pipe.read.assert_called_once_with(len(reply))

    def test_silent_socket_respects_the_command_deadline(self):
        client_socket, server_socket = socket.socketpair()
        client = make_client(sock=client_socket)
        try:
            started = time.monotonic()
            with self.assertRaises(TimeoutError):
                client.command("get_property", "time-pos", timeout=0.05)
            self.assertLess(time.monotonic() - started, 2.0)
        finally:
            client.close()
            server_socket.close()

    def test_closed_socket_fails_before_returning_missing_property(self):
        client_socket, server_socket = socket.socketpair()
        client = make_client(sock=client_socket)
        server_socket.shutdown(socket.SHUT_WR)
        try:
            with self.assertRaises(ConnectionError):
                client.command("get_property", "time-pos", timeout=1)
        finally:
            client.close()
            server_socket.close()

    def test_reply_timeout_records_failure_and_stops_the_child(self):
        process = Mock()
        process.wait.side_effect = [subprocess.TimeoutExpired("mpv", 15), 0]
        client = Mock()
        client.command.side_effect = TimeoutError("reply deadline exceeded")
        args = SimpleNamespace(hwdec="no", start=0, output_mode="sdr", mpv_arg=[],
                               seconds=0.01, screenshot_offset=0)
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(verification.subprocess, "Popen", return_value=process), \
                    patch.object(verification, "IpcClient", return_value=client):
                result = verification.run_mode("mpv", "unused.mkv", "el-on", args, Path(directory))
        self.assertIn("reply deadline exceeded", result["error"])
        client.close.assert_called_once()
        process.kill.assert_called_once()
        self.assertEqual(process.wait.call_count, 2)

    @unittest.skipUnless(os.name == "nt", "requires Windows named pipes")
    def test_native_windows_silent_pipe_respects_the_command_deadline(self):
        from multiprocessing.connection import Listener

        pipe_name = rf"\\.\pipe\bloom-fel-test-{uuid.uuid4().hex}"
        connections = []
        connected = threading.Event()
        with Listener(pipe_name, family="AF_PIPE") as listener:
            def accept():
                connections.append(listener.accept())
                connected.set()

            server = threading.Thread(target=accept, daemon=True)
            server.start()
            client = verification.IpcClient(pipe_name, timeout=2)
            try:
                self.assertTrue(connected.wait(timeout=2))
                started = time.monotonic()
                with self.assertRaises(TimeoutError):
                    client.command("get_property", "time-pos", timeout=0.05)
                self.assertLess(time.monotonic() - started, 2.0)
            finally:
                client.close()
                for connection in connections:
                    connection.close()
                server.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
