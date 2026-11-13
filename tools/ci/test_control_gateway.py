import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys
import json
import threading
from urllib.error import HTTPError
from urllib.request import Request, urlopen

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("pet_control_gateway", root / "server/control_gateway.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class BridgeBoundary:
    def __init__(self):
        self.calls = 0
        self.requests = []
        self.unknown = False

    def execute(self, body):
        self.calls += 1
        self.requests.append(body)
        if self.unknown:
            raise TimeoutError()
        return {"requestId": body["requestId"], "state": "succeeded", "result": {"launchRequested": True}}

    def lookup(self, request_id):
        return {"requestId": request_id, "state": "succeeded", "result": {"launchRequested": True}}


class ControlGatewayTest(unittest.TestCase):
    def test_http_authentication_strict_json_and_scoped_result(self):
        with tempfile.TemporaryDirectory() as directory:
            bridge = BridgeBoundary()
            config = {"devices": {"pet-test": {"token": "a" * 32, "applications": ["idea"], "projects": []}}}
            gateway = module.Gateway(config, Path(directory) / "audit.sqlite", bridge, lambda: 1000)
            server = module.create_server(gateway, 0)
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            endpoint = "http://127.0.0.1:" + str(server.server_port)

            def send(path, body=None, token="a" * 32):
                request = Request(endpoint + path, data=body, headers={"Authorization": "Bearer " + token,
                                  "Content-Type": "application/json"})
                try:
                    with urlopen(request, timeout=3) as response:
                        return response.status, json.load(response)
                except HTTPError as error:
                    with error:
                        return error.code, json.load(error)

            try:
                body = {"deviceId": "pet-test", "requestId": 1, "toolId": "app.open",
                        "arguments": {"applicationId": "idea"}, "approved": True, "expiresAt": 1050}
                wire = module.canonical(body).encode()
                self.assertEqual(send("/v1/execute", wire, "b" * 32)[0], 401)
                duplicate = wire[:-1] + b',"requestId":2}'
                self.assertEqual(send("/v1/execute", duplicate)[0], 400)
                self.assertEqual(bridge.calls, 0)
                self.assertEqual(send("/v1/execute", wire), (200, {"requestId": 1, "state": "succeeded", "summary": "command accepted"}))
                self.assertEqual(send("/v1/results/pet-test/1")[1]["state"], "succeeded")
                self.assertEqual(send("/v1/results/other-device/1")[0], 401)
                self.assertEqual(send("/v1/results/pet-test/2")[0], 404)
                self.assertEqual(send("/v1/execute", wire)[0], 200)
                self.assertEqual(bridge.calls, 1)
            finally:
                server.shutdown()
                server.server_close()
                worker.join()

    def test_confirmation_scope_restart_and_unknown_result(self):
        with tempfile.TemporaryDirectory() as directory:
            bridge = BridgeBoundary()
            config = {"devices": {"pet-test": {"token": "a" * 32, "applications": ["idea"], "projects": []}}}
            clock = lambda: 1000
            gateway = module.Gateway(config, Path(directory) / "audit.sqlite", bridge, clock)
            body = {"deviceId": "pet-test", "requestId": 1, "toolId": "app.open",
                    "arguments": {"applicationId": "idea"}, "approved": False, "expiresAt": 1050}
            with self.assertRaises(module.Denied):
                gateway.execute("a" * 32, body)
            self.assertEqual(bridge.calls, 0)
            body["approved"] = True
            self.assertEqual(gateway.execute("a" * 32, body)["state"], "succeeded")
            self.assertEqual(bridge.calls, 1)
            restarted = module.Gateway(config, Path(directory) / "audit.sqlite", bridge, clock)
            self.assertEqual(restarted.execute("a" * 32, body)["state"], "succeeded")
            self.assertEqual(bridge.calls, 1)
            body["requestId"] = 2
            bridge.unknown = True
            self.assertEqual(restarted.execute("a" * 32, body)["state"], "unknown")
            self.assertEqual(restarted.execute("a" * 32, body)["state"], "unknown")
            self.assertEqual(bridge.calls, 2)
            self.assertEqual(restarted.lookup("a" * 32, "pet-test", 2)["state"], "succeeded")
            body["requestId"] = 3
            body["arguments"] = {"applicationId": "terminal"}
            with self.assertRaises(module.Denied):
                restarted.execute("a" * 32, body)
            with self.assertRaises(module.Denied):
                restarted.lookup("b" * 32, "pet-test", 1)
            self.assertEqual(bridge.calls, 2)


if __name__ == "__main__":
    unittest.main()
