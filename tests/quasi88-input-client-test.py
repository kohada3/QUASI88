import json
import socket
import subprocess
import sys
import threading
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "tools" / "quasi88-input.py"


class FakeServer:
    def __init__(self, response=None, delay=0):
        self.response = response
        self.delay = delay
        self.request = b""
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.thread.join(2)
        self.listener.close()

    def _serve(self):
        connection, _ = self.listener.accept()
        with connection:
            while not self.request.endswith(b"\n"):
                chunk = connection.recv(4096)
                if not chunk:
                    break
                self.request += chunk
            if self.delay:
                time.sleep(self.delay)
            if self.response is not None:
                connection.sendall((json.dumps(self.response, separators=(",", ":")) + "\n").encode())


def run_client(port, *arguments, timeout=None):
    command = [sys.executable, str(CLIENT), "--port", str(port)]
    if timeout is not None:
        command += ["--timeout", str(timeout)]
    command += list(arguments)
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=5)


class ClientTest(unittest.TestCase):
    def assert_request(self, arguments, expected):
        with FakeServer({"id": 1, "ok": True, "status": "queued"}) as server:
            result = run_client(server.port, *arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(server.request.decode(), expected)
        self.assertEqual(json.loads(result.stdout), {"id": 1, "ok": True, "status": "queued"})

    def test_ping_request(self):
        self.assert_request(("ping",), '{"id":1,"op":"ping"}\n')

    def test_tap_defaults_to_80_ms(self):
        self.assert_request(
            ("tap", "KEY88_1"),
            '{"id":1,"op":"tap","key":"KEY88_1","hold_ms":80}\n',
        )

    def test_custom_hold_and_special_key(self):
        self.assert_request(
            ("tap", "KEY88_RETURN", "--hold-ms", "120"),
            '{"id":1,"op":"tap","key":"KEY88_RETURN","hold_ms":120}\n',
        )

    def test_keydown_special_key(self):
        self.assert_request(
            ("keydown", "KEY88_SHIFT"),
            '{"id":1,"op":"keydown","key":"KEY88_SHIFT"}\n',
        )

    def test_error_response_is_failure(self):
        with FakeServer({"id": 1, "ok": False, "error": "invalid_key"}) as server:
            result = run_client(server.port, "keyup", "BAD")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["error"], "invalid_key")

    def test_mismatched_response_id_is_failure(self):
        with FakeServer({"id": 2, "ok": True, "status": "ready"}) as server:
            result = run_client(server.port, "ping")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("response id", result.stderr)

    def test_timeout_is_failure(self):
        with FakeServer(None, delay=0.2) as server:
            result = run_client(server.port, "ping", timeout=0.05)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timed out", result.stderr.lower())

    def test_connection_refused_is_failure(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        listener.close()
        result = run_client(port, "ping", timeout=0.1)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(result.stderr.strip())


if __name__ == "__main__":
    unittest.main()
