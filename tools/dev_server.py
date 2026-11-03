#!/usr/bin/env python3
"""
Temporary / development backend for Switch Cloud Sync.

Implements the protocol described in docs/API.md using only the Python
standard library (no pip install needed).

    python tools/dev_server.py --host 0.0.0.0 --port 8420 --token test-token-123

Data is written under tools/dev_server_data/ by default.
This server is for testing on a home network. It is NOT hardened for the
public internet: use a real implementation with HTTPS and proper auth there.
"""

import argparse
import hashlib
import hmac
import json
import os
import re
import shutil
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(BASE_DIR, "dev_server_data")
TOKEN = ""
MAX_BODY = 4 * 1024 * 1024 * 1024  # 4 GiB hard stop

_lock = threading.Lock()
_hex_title = re.compile(r"^[0-9a-f]{1,16}$")
_hex_user = re.compile(r"^[0-9a-f]{1,32}$")


def log(msg):
    print(time.strftime("[%H:%M:%S] ") + str(msg), flush=True)


# --------------------------------------------------------------------------
# storage helpers
# --------------------------------------------------------------------------

def device_root(device):
    return os.path.join(DATA_DIR, "devices", device)


def state_path(device):
    return os.path.join(device_root(device), "state.json")


def load_state(device):
    try:
        with open(state_path(device), "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            raise ValueError("bad state")
        data.setdefault("saves", {})
        data.setdefault("playtime", {})
        return data
    except FileNotFoundError:
        return {"saves": {}, "playtime": {}}
    except Exception as exc:  # corrupt index: start over, files stay on disk
        log("warning: cannot read %s (%s), starting with an empty index" % (state_path(device), exc))
        return {"saves": {}, "playtime": {}}


def save_state(device, state):
    path = state_path(device)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def write_atomic(dst, src_tmp):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    os.replace(src_tmp, dst)


def safe_device(value):
    if not value or len(value) > 64:
        return None
    if value in (".", "..") or value.startswith("."):
        return None
    if any(c in value for c in ("/", "\\", "\x00")):
        return None
    return value


def norm_title(value):
    v = (value or "").strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if not _hex_title.match(v):
        return None
    return v.zfill(16)


def norm_user(value):
    v = (value or "").strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if v in ("", "0"):
        return "0"
    if not _hex_user.match(v):
        return None
    return v.zfill(32)


def safe_album_path(value):
    """Reject traversal, absolute paths and empty segments."""
    if not value or len(value) > 1024:
        return None
    if value.startswith("/") or "\\" in value or "\x00" in value:
        return None
    parts = value.split("/")
    if any(p in ("", ".", "..") for p in parts):
        return None
    return "/".join(parts)


def one_line(value, limit=200):
    s = " ".join((value or "").replace("\t", " ").split())
    return s[:limit]


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


# --------------------------------------------------------------------------
# HTTP handler
# --------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "SwitchCloudSyncDev/1.0"

    def log_request(self, code="-", size="-"):
        log("%s %s -> %s" % (self.command, self.path, code))

    # -- helpers ---------------------------------------------------------
    def send_text(self, code, text, ctype="text/plain; charset=utf-8"):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def send_file(self, path):
        try:
            size = os.path.getsize(path)
            f = open(path, "rb")
        except OSError:
            return self.send_text(404, "error=no_save\n")
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.end_headers()
        try:
            with f:
                while True:
                    chunk = f.read(1 << 16)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def content_length(self):
        raw = self.headers.get("Content-Length")
        if raw is None:
            return None
        try:
            n = int(raw)
        except ValueError:
            return None
        if n < 0 or n > MAX_BODY:
            return None
        return n

    def authorized(self):
        if not TOKEN:
            return True
        got = (self.headers.get("Authorization") or "").strip()
        return hmac.compare_digest(got, "Bearer " + TOKEN)

    def query(self):
        return parse_qs(urlsplit(self.path).query, keep_blank_values=True)

    @staticmethod
    def param(q, key, default=""):
        vals = q.get(key)
        return vals[0] if vals else default

    def recv_to_file(self, path, length):
        digest = hashlib.sha256()
        remaining = length
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            while remaining > 0:
                chunk = self.rfile.read(min(1 << 16, remaining))
                if not chunk:
                    raise IOError("connection closed while reading the body")
                f.write(chunk)
                digest.update(chunk)
                remaining -= len(chunk)
        return digest.hexdigest()

    def recv_all(self, length):
        parts = []
        remaining = length
        while remaining > 0:
            chunk = self.rfile.read(min(1 << 16, remaining))
            if not chunk:
                break
            parts.append(chunk)
            remaining -= len(chunk)
        return b"".join(parts)

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True

    # -- routing ---------------------------------------------------------
    def do_GET(self):
        u = urlsplit(self.path)
        q = self.query()

        if u.path == "/api/v1/ping":
            return self.send_text(200, "ok dev-server v1\n")

        if u.path == "/":
            return self.send_text(
                200,
                "Switch Cloud Sync dev server\n"
                "data: %s\n"
                "token: %s\n\n"
                "GET  /api/v1/ping\n"
                "PUT  /api/v1/saves?device=&title_id=&user_id=&timestamp=&title_name=\n"
                "GET  /api/v1/saves?device=\n"
                "GET  /api/v1/saves/latest?device=&title_id=&user_id=\n"
                "PUT  /api/v1/album?device=&path=\n"
                "PUT  /api/v1/playtime?device=\n"
                "PUT  /api/v1/playlog?device=\n"
                "GET  /api/v1/playlog?device=\n"
                % (DATA_DIR, "required" if TOKEN else "disabled"),
            )

        if not self.authorized():
            return self.send_text(401, "error=unauthorized\nmessage=bad or missing token\n")

        if u.path == "/api/v1/saves":
            return self.handle_list_saves(q)
        if u.path == "/api/v1/saves/latest":
            return self.handle_get_save(q)
        if u.path == "/api/v1/playlog":
            return self.handle_get_playlog(q)

        return self.send_text(404, "error=not_found\n")

    def do_PUT(self):
        u = urlsplit(self.path)
        q = self.query()

        if not self.authorized():
            return self.send_text(401, "error=unauthorized\nmessage=bad or missing token\n")

        if u.path == "/api/v1/saves":
            return self.handle_put_save(q)
        if u.path == "/api/v1/album":
            return self.handle_put_album(q)
        if u.path == "/api/v1/playtime":
            return self.handle_put_playtime(q)
        if u.path == "/api/v1/playlog":
            return self.handle_put_playlog(q)

        return self.send_text(404, "error=not_found\n")

    # -- saves -----------------------------------------------------------
    def handle_put_save(self, q):
        device = safe_device(self.param(q, "device"))
        title = norm_title(self.param(q, "title_id"))
        user = norm_user(self.param(q, "user_id"))
        if not device or not title or not user:
            return self.send_text(400, "error=bad_request\nmessage=device/title_id/user_id\n")

        length = self.content_length()
        if not length:
            return self.send_text(411, "error=length_required\n")

        try:
            timestamp = int(self.param(q, "timestamp") or time.time())
        except ValueError:
            timestamp = int(time.time())
        name = one_line(self.param(q, "title_name"))

        base = os.path.join(device_root(device), "saves", title, user)
        os.makedirs(base, exist_ok=True)
        tmp = os.path.join(base, ".upload-%d-%d.part" % (os.getpid(), threading.get_ident()))

        try:
            sha = self.recv_to_file(tmp, length)
        except Exception as exc:
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self.send_text(400, "error=body\nmessage=%s\n" % one_line(str(exc)))

        want = (self.headers.get("X-Content-SHA256") or "").strip().lower()
        if want and not hmac.compare_digest(want, sha):
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self.send_text(400, "error=checksum\nmessage=sha256 mismatch\n")

        latest = os.path.join(base, "latest.scsa")
        write_atomic(latest, tmp)

        history = os.path.join(base, "history")
        os.makedirs(history, exist_ok=True)
        try:
            shutil.copyfile(latest, os.path.join(history, "%d.scsa" % timestamp))
        except OSError as exc:
            log("warning: cannot write history (%s)" % exc)

        with _lock:
            state = load_state(device)
            state["saves"]["%s/%s" % (title, user)] = {
                "timestamp": timestamp,
                "size": length,
                "sha256": sha,
                "title_name": name,
            }
            save_state(device, state)

        log("stored save %s/%s/%s (%d bytes, %s)" % (device, title, user, length, sha[:12]))
        return self.send_text(200, "stored=ok\nsha256=%s\nbytes=%d\n" % (sha, length))

    def handle_list_saves(self, q):
        device = safe_device(self.param(q, "device"))
        if not device:
            return self.send_text(400, "error=bad_request\nmessage=device\n")

        state = load_state(device)
        rows = ["#title_id\tuser_id\ttimestamp\tsize\tsha256\ttitle_name"]
        items = sorted(
            state["saves"].items(),
            key=lambda kv: (str(kv[1].get("title_name", "")).lower(), kv[0]),
        )
        for key, entry in items:
            title, _, user = key.partition("/")
            try:
                rows.append(
                    "%s\t%s\t%d\t%d\t%s\t%s"
                    % (
                        title,
                        user or "0",
                        int(entry.get("timestamp", 0)),
                        int(entry.get("size", 0)),
                        entry.get("sha256", ""),
                        one_line(entry.get("title_name", "")),
                    )
                )
            except Exception:
                continue

        return self.send_text(
            200, "\n".join(rows) + "\n", "text/tab-separated-values; charset=utf-8"
        )

    def handle_get_save(self, q):
        device = safe_device(self.param(q, "device"))
        title = norm_title(self.param(q, "title_id"))
        user = norm_user(self.param(q, "user_id"))
        if not device or not title or not user:
            return self.send_text(400, "error=bad_request\n")
        return self.send_file(os.path.join(device_root(device), "saves", title, user, "latest.scsa"))

    # -- album -----------------------------------------------------------
    def handle_put_album(self, q):
        device = safe_device(self.param(q, "device"))
        rel = safe_album_path(self.param(q, "path"))
        if not device or not rel:
            return self.send_text(400, "error=bad_request\nmessage=device/path\n")

        length = self.content_length()
        if not length:
            return self.send_text(411, "error=length_required\n")

        dst = os.path.join(device_root(device), "album", *rel.split("/"))
        tmp = dst + ".part"
        try:
            sha = self.recv_to_file(tmp, length)
        except Exception as exc:
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self.send_text(400, "error=body\nmessage=%s\n" % one_line(str(exc)))

        write_atomic(dst, tmp)
        log("stored album %s/%s (%d bytes)" % (device, rel, length))
        return self.send_text(200, "stored=ok\nsha256=%s\nbytes=%d\n" % (sha, length))

    # -- playtime --------------------------------------------------------
    def handle_put_playtime(self, q):
        device = safe_device(self.param(q, "device"))
        if not device:
            return self.send_text(400, "error=bad_request\nmessage=device\n")

        length = self.content_length()
        if length is None:
            return self.send_text(411, "error=length_required\n")

        body = self.recv_all(length).decode("utf-8", "replace")
        accepted = 0

        with _lock:
            state = load_state(device)
            for line in body.splitlines():
                line = line.strip("\r")
                if not line or line.startswith("#"):
                    continue
                parts = line.split("\t")
                if len(parts) < 6:
                    continue
                title = norm_title(parts[0])
                user = norm_user(parts[1])
                if not title or not user:
                    continue
                try:
                    playtime_ns = int(parts[2])
                    launches = int(parts[3])
                    last_played = int(parts[4])
                except ValueError:
                    continue
                name = one_line("\t".join(parts[5:]))
                key = "%s/%s" % (title, user)
                old = state["playtime"].get(key) or {}
                state["playtime"][key] = {
                    "playtime_ns": max(int(old.get("playtime_ns", 0)), playtime_ns),
                    "launches": max(int(old.get("launches", 0)), launches),
                    "last_played": max(int(old.get("last_played", 0)), last_played),
                    "title_name": name or old.get("title_name", ""),
                }
                accepted += 1
            save_state(device, state)

        log("stored playtime %s (%d rows)" % (device, accepted))
        return self.send_text(200, "accepted=%d\n" % accepted)

    # -- detailed play log (JSON, same shape as NX-Activity-Log export) ----
    def handle_put_playlog(self, q):
        device = safe_device(self.param(q, "device"))
        if not device:
            return self.send_text(400, "error=bad_request\nmessage=device\n")

        length = self.content_length()
        if length is None or length <= 0:
            return self.send_text(411, "error=length_required\n")

        body = self.recv_all(length)
        try:
            data = json.loads(body.decode("utf-8"))
            if not isinstance(data, dict) or not isinstance(data.get("users"), list):
                raise ValueError("missing users[]")
        except Exception as exc:
            return self.send_text(400, "error=json\nmessage=%s\n" % one_line(str(exc)))

        root = device_root(device)
        os.makedirs(root, exist_ok=True)
        dst = os.path.join(root, "playlog.json")
        tmp = dst + ".part"
        with open(tmp, "wb") as f:
            f.write(body)
        os.replace(tmp, dst)

        accepted = 0
        with _lock:
            state = load_state(device)
            for user in data.get("users", []):
                uid = str(user.get("id", "")).lower().zfill(32)
                for title in user.get("titles", []):
                    tid = str(title.get("id", "")).lower().zfill(16)
                    summary = title.get("summary") or {}
                    try:
                        playtime_ns = int(summary.get("playtime", 0)) * 1000000000
                        launches = int(summary.get("launches", 0))
                        last_played = int(summary.get("lastPlayed", 0))
                    except (TypeError, ValueError):
                        continue
                    key = "%s/%s" % (tid, uid)
                    old = state["playtime"].get(key) or {}
                    state["playtime"][key] = {
                        "playtime_ns": max(int(old.get("playtime_ns", 0)), playtime_ns),
                        "launches": max(int(old.get("launches", 0)), launches),
                        "last_played": max(int(old.get("last_played", 0)), last_played),
                        "title_name": one_line(title.get("name")) or old.get("title_name", ""),
                    }
                    accepted += 1
            save_state(device, state)

        log(
            "stored playlog %s (%d titles, %d bytes)"
            % (device, accepted, len(body))
        )
        return self.send_text(200, "accepted=%d\n" % accepted)

    def handle_get_playlog(self, q):
        device = safe_device(self.param(q, "device"))
        if not device:
            return self.send_text(400, "error=bad_request\nmessage=device\n")
        path = os.path.join(device_root(device), "playlog.json")
        try:
            with open(path, "rb") as f:
                body = f.read()
        except OSError:
            return self.send_text(404, "error=not_found\n")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    global DATA_DIR, TOKEN

    parser = argparse.ArgumentParser(description="Switch Cloud Sync development server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8420)
    parser.add_argument("--token", default="", help="Bearer token; empty disables auth")
    parser.add_argument("--data-dir", default=DATA_DIR)
    args = parser.parse_args()

    DATA_DIR = os.path.abspath(args.data_dir)
    TOKEN = args.token
    os.makedirs(DATA_DIR, exist_ok=True)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True

    ip = lan_ip()
    log("Switch Cloud Sync dev server")
    log("  listening : %s:%d" % (args.host, args.port))
    log("  for Switch: http://%s:%d" % (ip, args.port))
    log("  local     : http://127.0.0.1:%d" % args.port)
    log("  token     : %s" % (TOKEN or "(disabled)"))
    log("  data dir  : %s" % DATA_DIR)
    log("Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("stopping")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
