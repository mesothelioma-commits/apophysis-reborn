"""Exercise the packaged helper as a real MCP stdio peer. Pass the executable as argv[1]."""

import base64
import errno
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import uuid

BINARY = str(Path(sys.argv.pop(1)).resolve())
REVISION = "unit-test-revision"


def request(number, method, params=None):
    message = {"jsonrpc": "2.0", "id": number, "method": method}
    if params is not None:
        message["params"] = params
    return message


def initialization(version="2025-11-25"):
    return [
        request(1, "initialize", {"protocolVersion": version, "capabilities": {},
                                  "clientInfo": {"name": "stdio-integration-test", "version": "1"}}),
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
    ]


def call(number, name, **args):
    return request(number, "tools/call", {"name": name, "arguments": args})


def run(messages, env=None):
    lines = [m if isinstance(m, bytes) else json.dumps(m).encode() for m in messages]
    completed = subprocess.run([BINARY], input=b"\n".join(lines) + b"\n", capture_output=True,
                               check=True, timeout=15, env=env)
    return [json.loads(line) for line in completed.stdout.splitlines()]


class Protocol(unittest.TestCase):
    def test_initialize_discovery_ping(self):
        for requested, expected in [("2025-11-25", "2025-11-25"), ("2025-06-18", "2025-06-18"),
                                    ("2099-01-01", "2025-11-25")]:
            with self.subTest(protocol=requested):
                handshake, listing, pong = run(initialization(requested) + [
                    request("list", "tools/list"), {"jsonrpc": "2.0", "method": "notifications/cancelled"},
                    request("ping", "ping"),
                ])
                self.assertEqual(handshake["result"]["protocolVersion"], expected)
                self.assertIn("tools", handshake["result"]["capabilities"])
                tools = listing["result"]["tools"]
                self.assertEqual({t["name"] for t in tools}, {"inspect_flame", "get_preview", "propose_script"})
                self.assertTrue(all(not t["inputSchema"]["additionalProperties"] for t in tools))
                self.assertEqual(pong, {"jsonrpc": "2.0", "id": "ping", "result": {}})

    def test_errors_and_bounded_frames(self):
        results = run([request(99, "tools/list"), b"{{", b"[]", b"x" * (512 * 1024 + 1)] +
                      initialization() + [
                          call(2, "unknown"), call(3, "inspect_flame", path="/"),
                          call(4, "propose_script", script="return"),
                          call(5, "propose_script", revision=REVISION, script="a" * 65537),
                          request(6, "resources/list"), request(7, "ping")])
        self.assertEqual([r["error"]["code"] for r in results[:4]], [-32002, -32700, -32600, -32600])
        self.assertEqual([r["error"]["code"] for r in results[5:-1]], [-32602] * 4 + [-32601])
        self.assertEqual(results[-1]["result"], {})

    @unittest.skipUnless(sys.platform.startswith("linux"), "isolated XDG and Unix socket fixture")
    def test_local_editor_discovery_and_transport(self):
        with tempfile.TemporaryDirectory(dir="/tmp") as directory:
            root = Path(directory)
            # Both Qt client and fixture use a private temporary socket namespace.
            env = {**os.environ, "XDG_CONFIG_HOME": directory, "TMPDIR": directory}
            descriptor = root / "ApophysisReborn" / "ai-session.json"
            descriptor.parent.mkdir()
            tool_call = call(2, "inspect_flame")
            disconnected = run(initialization() + [tool_call], env)[-1]
            self.assertTrue(disconnected["result"]["isError"])
            token = secrets.token_hex(32)
            server = "apophysis-reborn-ai-" + uuid.uuid4().hex
            descriptor.write_text(json.dumps({"version": 1, "server": server, "token": token}))
            descriptor.chmod(0o644)
            private = run(initialization() + [tool_call], env)[-1]
            self.assertIn("private", private["result"]["content"][0]["text"])
            descriptor.chmod(0o600)
            seen = []
            failures = []
            preview = base64.b64encode(b"test png transport").decode()

            def editor(listener):
                try:
                    for payload in [
                        {"ok": True, "result": {"name": "Unsaved flame", "revision": REVISION}},
                        {"ok": True, "result": {"mimeType": "image/png", "data": preview}},
                        {"ok": True, "result": {"status": "awaiting_review", "revision": REVISION}},
                        {"ok": False, "error": "Stale revision. Inspect the current flame again."},
                    ]:
                        with listener.accept()[0] as peer, peer.makefile("rb") as inp:
                            seen.append(json.loads(inp.readline(512 * 1024)))
                            peer.sendall(json.dumps(payload).encode() + b"\n")
                except Exception as exc:
                    failures.append(str(exc))

            try:
                listener = socket.socket(socket.AF_UNIX)
            except OSError as exc:
                if exc.errno not in (errno.EACCES, errno.EPERM) or os.getenv('APO_AI_REQUIRE_SOCKET_TEST') == '1':
                    raise
                self.skipTest("Host policy disallows Unix domain sockets")
            with listener:
                listener.bind(str(root / server))
                listener.listen()
                listener.settimeout(8)
                worker = threading.Thread(target=editor, args=(listener,), daemon=True)
                worker.start()
                proposal = {"revision": REVISION, "script": "apo.rotate_palette(1)", "title": "Palette study"}
                results = run(initialization() + [tool_call, call(3, "get_preview", revision=REVISION),
                              call(4, "propose_script", **proposal), call(5, "get_preview", revision="old")], env)
                worker.join(9)
                self.assertFalse(worker.is_alive())
            self.assertEqual(failures, [])
            self.assertEqual([m["tool"] for m in seen], ["inspect_flame", "get_preview", "propose_script", "get_preview"])
            self.assertTrue(all(m["token"] == token and m["version"] == 1 for m in seen))
            self.assertEqual(seen[2]["arguments"], proposal)
            self.assertEqual(json.loads(results[1]["result"]["content"][0]["text"])["name"], "Unsaved flame")
            self.assertEqual(results[2]["result"]["content"][0],
                             {"type": "image", "mimeType": "image/png", "data": preview})
            self.assertEqual(json.loads(results[3]["result"]["content"][0]["text"])["status"], "awaiting_review")
            self.assertTrue(results[4]["result"]["isError"])
            self.assertNotIn(token, json.dumps(results))


if __name__ == "__main__":
    unittest.main()
