#!/usr/bin/env python3
"""Device-scoped control gateway for the existing loopback-only mac-bridge.

Run behind a TLS reverse proxy. This process only listens on 127.0.0.1 and never
launches applications itself. Secrets live in an owner-only configuration file.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import sqlite3
import threading
import time
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler
import uuid

NAMESPACE = uuid.UUID("d56476c0-a185-4496-80f8-d93fe4407f1e")
FIELDS = {"system.info": None, "project.status": "projectId", "app.open": "applicationId"}


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


class Denied(Exception):
    def __init__(self, code, status=403):
        self.code, self.status = code, status


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *_):
        return None


class MacBridge:
    def __init__(self, url, token):
        parsed = urlsplit(url)
        if parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost") or parsed.path not in ("", "/") or parsed.query or parsed.fragment or parsed.username:
            raise ValueError("bridge must use a fixed loopback endpoint")
        if len(token) < 32 or any(c.isspace() for c in token):
            raise ValueError("invalid bridge token")
        self.url, self.token = url.rstrip("/"), token
        self.opener = build_opener(NoRedirect())

    def send(self, path, body=None):
        data = None if body is None else canonical(body).encode()
        request = Request(self.url + path, data=data, headers={"Authorization": "Bearer " + self.token,
                          "Content-Type": "application/json", "Accept": "application/json"})
        with self.opener.open(request, timeout=5) as response:
            raw = response.read(8193)
            if len(raw) > 8192 or response.status != 200:
                raise Denied("bridge_unavailable", 502)
            value = json.loads(raw)
            if not isinstance(value, dict):
                raise Denied("bridge_unavailable", 502)
            return value

    def execute(self, body):
        payload = dict(body)
        if payload["toolId"] == "app.open":
            expires = int(time.time()) + 30
            actor = "pet_gateway"
            digest = hashlib.sha256(canonical(payload["arguments"]).encode()).hexdigest()
            proof = f'{payload["requestId"]}\n{actor}\n{payload["toolId"]}\n{digest}\n{expires}'
            payload["approval"] = {"actor": actor, "expiresAt": expires,
                "signature": hmac.new(self.token.encode(), proof.encode(), hashlib.sha256).hexdigest()}
        return self.send("/execute", payload)

    def lookup(self, request_id):
        return self.send("/executions/" + request_id)


class Gateway:
    def __init__(self, configuration, database, bridge, clock=time.time):
        self.devices = configuration.get("devices", {})
        if not self.devices or len(self.devices) > 32:
            raise ValueError("device scopes required")
        seen = set()
        for device, scope in self.devices.items():
            token = scope.get("token", "")
            if not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", device) or not isinstance(token, str) or len(token) < 32 or token in seen or any(c.isspace() for c in token):
                raise ValueError("invalid device scope")
            seen.add(token)
            for key in ("applications", "projects"):
                values = scope.get(key, [])
                if not isinstance(values, list) or len(values) > 16 or any(not isinstance(v, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", v) for v in values):
                    raise ValueError("invalid capability scope")
        self.database, self.bridge, self.clock = str(database), bridge, clock
        # Audit contains IDs, tool, argument digest and result; no tokens or raw arguments.
        descriptor = os.open(self.database, os.O_RDWR | os.O_CREAT, 0o600)
        os.close(descriptor)
        if os.stat(self.database).st_mode & 0o077:
            raise ValueError("audit database must be owner-only")
        with self.connect() as db:
            db.execute("CREATE TABLE IF NOT EXISTS requests(device TEXT, sequence INTEGER, digest TEXT, tool TEXT, at INTEGER, result TEXT, PRIMARY KEY(device, sequence))")

    def connect(self):
        return sqlite3.connect(self.database, timeout=5)

    def authenticate(self, token, device):
        scope = self.devices.get(device)
        if not scope or not hmac.compare_digest(token, scope["token"]):
            raise Denied("unauthorized", 401)
        return scope

    @staticmethod
    def bridge_id(device, sequence):
        return str(uuid.uuid5(NAMESPACE, device + ":" + str(sequence)))

    @staticmethod
    def normalized(sequence, bridge_id, value):
        if value.get("requestId") != bridge_id or value.get("state") not in ("succeeded", "failed"):
            return {"requestId": sequence, "state": "unknown"}
        state = value["state"]
        result = value.get("result", {})
        summary = "command accepted" if state == "succeeded" else "command failed"
        # Do not forward project paths, stdout, arbitrary error strings or private payloads.
        if isinstance(result, dict) and type(result.get("changedFiles")) is int:
            summary = f'changed files: {max(0, min(result["changedFiles"], 100000))}'
        return {"requestId": sequence, "state": state, "summary": summary}

    def execute(self, token, body):
        if not isinstance(body, dict) or set(body) != {"deviceId", "requestId", "toolId", "arguments", "approved", "expiresAt"}:
            raise Denied("invalid_request", 400)
        device, sequence, tool, arguments = (body[key] for key in ("deviceId", "requestId", "toolId", "arguments"))
        if not isinstance(device, str):
            raise Denied("invalid_device", 400)
        scope = self.authenticate(token, device)
        now = int(self.clock())
        if type(sequence) is not int or not 0 < sequence <= 9007199254740991 or tool not in FIELDS or not isinstance(arguments, dict) or type(body["approved"]) is not bool:
            raise Denied("invalid_request", 400)
        key = FIELDS[tool]
        if set(arguments) != ({key} if key else set()):
            raise Denied("invalid_arguments", 400)
        if key and (not isinstance(arguments[key], str) or arguments[key] not in scope.get("applications" if key == "applicationId" else "projects", [])):
            raise Denied("capability_not_allowed")
        if tool == "app.open" and not body["approved"]:
            raise Denied("local_confirmation_required")
        expires = body["expiresAt"]
        if type(expires) is not int or not now < expires <= now + 60:
            raise Denied("expired")
        digest = hashlib.sha256(canonical({"toolId": tool, "arguments": arguments}).encode()).hexdigest()
        initial = {"requestId": sequence, "state": "unknown"}
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            old = db.execute("SELECT digest,result FROM requests WHERE device=? AND sequence=?", (device, sequence)).fetchone()
            if old:
                if old[0] != digest:
                    raise Denied("idempotency_conflict", 409)
                return json.loads(old[1])
            if db.execute("SELECT COUNT(*) FROM requests WHERE device=? AND at>?", (device, now - 60)).fetchone()[0] >= 30:
                raise Denied("rate_limited", 429)
            # History retention cannot remove the device replay watermark.
            highest = db.execute("SELECT MAX(sequence) FROM requests WHERE device=?", (device,)).fetchone()[0]
            if highest is not None and sequence <= highest:
                raise Denied("stale_request", 409)
            db.execute("INSERT INTO requests VALUES(?,?,?,?,?,?)", (device, sequence, digest, tool, now, canonical(initial)))
            db.execute("DELETE FROM requests WHERE at<? AND (device,sequence) NOT IN (SELECT device,MAX(sequence) FROM requests GROUP BY device)", (now - 30 * 86400,))
        bridge_id = self.bridge_id(device, sequence)
        try:
            response = self.bridge.execute({"requestId": bridge_id, "toolId": tool, "arguments": arguments})
            result = self.normalized(sequence, bridge_id, response)
        except Exception:
            result = initial
        with self.connect() as db:
            db.execute("UPDATE requests SET result=? WHERE device=? AND sequence=?", (canonical(result), device, sequence))
        return result

    def lookup(self, token, device, sequence):
        self.authenticate(token, device)
        with self.connect() as db:
            row = db.execute("SELECT result FROM requests WHERE device=? AND sequence=?", (device, sequence)).fetchone()
        if not row:
            raise Denied("request_not_found", 404)
        old = json.loads(row[0])
        if old.get("state") != "unknown":
            return old
        bridge_id = self.bridge_id(device, sequence)
        try:
            result = self.normalized(sequence, bridge_id, self.bridge.lookup(bridge_id))
        except Exception:
            return old
        with self.connect() as db:
            db.execute("UPDATE requests SET result=? WHERE device=? AND sequence=?", (canonical(result), device, sequence))
        return result


def load_json(raw):
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError("duplicate key")
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=unique, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite")))


def create_server(gateway, port):
    slots = threading.BoundedSemaphore(8)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def setup(self):
            super().setup()
            self.connection.settimeout(5)

        def do_POST(self):
            if not slots.acquire(blocking=False):
                self.send_error(503)
                return
            try:
                if self.path != "/v1/execute" or self.headers.get("Origin") or self.headers.get("Transfer-Encoding"):
                    raise Denied("invalid_endpoint", 400)
                length = self.headers.get("Content-Length", "")
                if not length.isdecimal() or not 0 < int(length) <= 2048:
                    raise Denied("invalid_size", 413)
                auth = self.headers.get("Authorization", "")
                if not auth.startswith("Bearer "):
                    raise Denied("unauthorized", 401)
                result = gateway.execute(auth[7:], load_json(self.rfile.read(int(length))))
                self.respond(200, result)
            except Denied as error:
                self.respond(error.status, {"error": error.code})
            except (ValueError, TypeError, UnicodeError, RecursionError):
                self.respond(400, {"error": "invalid_request"})
            except Exception:
                self.respond(503, {"error": "unavailable"})
            finally:
                slots.release()

        def do_GET(self):
            try:
                match = re.fullmatch(r"/v1/results/([a-zA-Z0-9_-]{1,40})/([1-9][0-9]{0,15})", self.path)
                auth = self.headers.get("Authorization", "")
                if not match or not auth.startswith("Bearer ") or self.headers.get("Origin"):
                    raise Denied("invalid_request", 400)
                self.respond(200, gateway.lookup(auth[7:], match[1], int(match[2])))
            except Denied as error:
                self.respond(error.status, {"error": error.code})
            except Exception:
                self.respond(503, {"error": "unavailable"})

        def respond(self, status, value):
            data = canonical(value).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8092)
    args = parser.parse_args()
    if args.config.stat().st_mode & 0o077:
        parser.error("configuration file must have owner-only permissions")
    config = load_json(args.config.read_text())
    bridge = MacBridge(config.get("bridgeUrl", "http://127.0.0.1:8091"), config["bridgeToken"])
    gateway = Gateway(config, args.database, bridge)
    create_server(gateway, args.port).serve_forever()


if __name__ == "__main__":
    main()
