#!/usr/bin/env python3
"""
Switch Cloud Sync — self-hosted server.

Standard library only (Python 3.10+). Implements:

  * the /api/v1/* protocol used by the Switch homebrew client
  * a first-run wizard that shows the API address + token, waits for the
    client to connect, then creates the admin account
  * an admin dashboard for saves, album media, playtime and play activity

Run:
    python server/switchcloud_server.py --host 0.0.0.0 --port 8420
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import logging
import mimetypes
import os
import re
import secrets
import socket
import sys
import threading
import time
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, unquote, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import webui  # noqa: E402
from store import Store, normalize_token  # noqa: E402

APP = "Switch Cloud Sync Server"
VERSION = "1.1.0"

STORE: Store = None  # type: ignore
PUBLIC_URL = ""
MAX_BODY = 4 * 1024 * 1024 * 1024  # 4 GiB
DATA_DIR = ""

SESSIONS = {}
SESSIONS_LOCK = threading.Lock()
SESSION_TTL = 12 * 3600

LOGIN_FAILS = {}
LOGIN_LOCK = threading.Lock()

AUTH_FAILS = {}
AUTH_LOCK = threading.Lock()


def auth_allowed(ip: str) -> bool:
    now = time.time()
    with AUTH_LOCK:
        count, start = AUTH_FAILS.get(ip, (0, now))
        if now - start > 60:
            AUTH_FAILS[ip] = (0, now)
            return True
        return count < 30


def auth_failed(ip: str):
    now = time.time()
    with AUTH_LOCK:
        count, start = AUTH_FAILS.get(ip, (0, now))
        if now - start > 60:
            count, start = 0, now
        AUTH_FAILS[ip] = (count + 1, start)


def auth_reset(ip: str):
    with AUTH_LOCK:
        AUTH_FAILS.pop(ip, None)

log = logging.getLogger("scs")


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------

def lan_ips():
    ips = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127."):
                ips.add(ip)
    except OSError:
        pass
    return sorted(ips)


def safe_device(value: str):
    if not value or len(value) > 64:
        return ""
    if value in (".", "..") or value.startswith("."):
        return ""
    if any(c in value for c in ("/", "\\", "\x00")):
        return ""
    return value


def norm_title(value: str):
    v = (value or "").strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if not re.fullmatch(r"[0-9a-f]{1,16}", v):
        return ""
    return v.zfill(16)


def norm_user(value: str):
    v = (value or "").strip().lower()
    if v.startswith("0x"):
        v = v[2:]
    if v in ("", "0"):
        return "0"
    if not re.fullmatch(r"[0-9a-f]{1,32}", v):
        return ""
    return v.zfill(32)


def safe_album_path(value: str):
    if not value or len(value) > 1024:
        return ""
    if value.startswith("/") or "\\" in value or "\x00" in value:
        return ""
    parts = value.split("/")
    if any(p in ("", ".", "..") for p in parts):
        return ""
    return "/".join(parts)


def one_line(value: str, limit=120):
    return " ".join((value or "").replace("\t", " ").replace("\r", " ").split())[:limit]


def fmt_size(n):
    return webui.fmt_size(n)


def client_version(ua: str):
    if "SwitchCloudSync/" in ua:
        tail = ua.split("SwitchCloudSync/", 1)[1]
        return tail.split()[0][:32]
    return ""


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "SwitchCloudSync/%s" % VERSION

    # -- logging ---------------------------------------------------------
    def log_message(self, fmt, *args):
        log.info("%s %s", self.address_string(), fmt % args)

    # -- low level responses --------------------------------------------
    def _respond(self, code, body: bytes, ctype="text/html; charset=utf-8", headers=None,
                 cookie=None):
        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            if cookie:
                self.send_header(
                    "Set-Cookie",
                    "scs_session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d"
                    % (cookie, SESSION_TTL),
                )
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True

    def _html(self, text: str, code=200, headers=None, cookie=None):
        self._respond(code, text.encode("utf-8"), "text/html; charset=utf-8", headers, cookie)

    def _text(self, code, text, headers=None):
        self._respond(code, text.encode("utf-8"), "text/plain; charset=utf-8", headers)

    def _json(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self._respond(code, body, "application/json; charset=utf-8")

    def _redirect(self, location, cookie=None, code=303):
        self.send_response(code)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        if cookie:
            self.send_header(
                "Set-Cookie",
                "scs_session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d"
                % (cookie, SESSION_TTL),
            )
        self.end_headers()

    def _error(self, code, message):
        self._html(webui.error_page(code, message), code)

    # -- request helpers -------------------------------------------------
    def _query(self):
        return parse_qs(urlsplit(self.path).query, keep_blank_values=True)

    @staticmethod
    def _param(q, key, default=""):
        vals = q.get(key)
        return vals[0] if vals else default

    def _content_length(self):
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

    def _read_all(self, length):
        parts = []
        remaining = length
        while remaining > 0:
            chunk = self.rfile.read(min(1 << 16, remaining))
            if not chunk:
                break
            parts.append(chunk)
            remaining -= len(chunk)
        return b"".join(parts)

    def _stream_body(self, dst, length):
        digest = hashlib.sha256()
        remaining = length
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            while remaining > 0:
                chunk = self.rfile.read(min(1 << 16, remaining))
                if not chunk:
                    raise IOError("connection closed while reading the body")
                f.write(chunk)
                digest.update(chunk)
                remaining -= len(chunk)
        return digest.hexdigest()

    def _read_form(self, limit=1 << 20):
        length = self._content_length()
        if not length:
            return {}
        body = self._read_all(min(length, limit))
        try:
            parsed = parse_qs(body.decode("utf-8", "replace"), keep_blank_values=True)
        except Exception:
            return {}
        return {k: v[0] for k, v in parsed.items()}

    def _same_origin(self):
        origin = self.headers.get("Origin")
        if not origin:
            return True
        host = self.headers.get("Host", "")
        return origin.split("://")[-1] == host

    # -- sessions --------------------------------------------------------
    def _session(self):
        raw = self.headers.get("Cookie")
        if not raw:
            return None
        cookie = SimpleCookie()
        try:
            cookie.load(raw)
        except Exception:
            return None
        morsel = cookie.get("scs_session")
        if not morsel:
            return None
        sid = morsel.value
        with SESSIONS_LOCK:
            session = SESSIONS.get(sid)
            if not session or session["expires"] < time.time():
                SESSIONS.pop(sid, None)
                return None
            return session

    def _new_session(self, user):
        sid = secrets.token_urlsafe(32)
        with SESSIONS_LOCK:
            SESSIONS[sid] = {
                "user": user,
                "csrf": secrets.token_urlsafe(24),
                "expires": time.time() + SESSION_TTL,
            }
        return sid

    def _drop_session(self):
        raw = self.headers.get("Cookie")
        if not raw:
            return
        cookie = SimpleCookie()
        try:
            cookie.load(raw)
        except Exception:
            return
        morsel = cookie.get("scs_session")
        if morsel:
            with SESSIONS_LOCK:
                SESSIONS.pop(morsel.value, None)

    def _csrf_ok(self, session, form):
        return hmac.compare_digest(form.get("csrf", ""), session["csrf"])

    def _require_admin(self):
        if not STORE.admin_configured():
            self._redirect("/setup")
            return None
        session = self._session()
        if not session:
            self._redirect("/login")
            return None
        return session

    # -- API auth --------------------------------------------------------
    def _api_auth(self, u):
        ip = self.client_address[0]
        if not auth_allowed(ip):
            self._text(429, "error=too_many_auth_failures\nmessage=try again later\n")
            return False

        token = STORE.token
        got = (self.headers.get("Authorization") or "").strip()
        if got.lower().startswith("bearer "):
            got = got[7:].strip()

        # Case-insensitive and dashes/spaces are optional, so the token is easy
        # to type on the Switch keyboard.
        ok = bool(token) and hmac.compare_digest(normalize_token(got), normalize_token(token))
        if not ok:
            auth_failed(ip)
            self._text(401, "error=unauthorized\nmessage=bad or missing token\n")
            return False
        auth_reset(ip)

        q = parse_qs(u.query)
        device = self._param(q, "device")
        name = unquote(self.headers.get("X-Device-Name") or "")
        STORE.mark_contact(device, name, self.client_address[0],
                           client_version(self.headers.get("User-Agent") or ""))
        return True

    # -- file serving ----------------------------------------------------
    def _serve_file(self, path, ctype, download=False, filename=None, allow_range=True):
        try:
            size = os.path.getsize(path)
            f = open(path, "rb")
        except OSError:
            return self._error(404, "文件不存在")

        start, end, code = 0, max(0, size - 1), 200
        extra = {}
        rng = self.headers.get("Range")
        if allow_range and rng and size > 0 and rng.startswith("bytes="):
            try:
                spec = rng[6:].split(",", 1)[0]
                a, b = spec.split("-", 1)
                start = int(a) if a else 0
                end = int(b) if b else size - 1
                if end >= size:
                    end = size - 1
                if start > end or start >= size:
                    f.close()
                    self.send_response(416)
                    self.send_header("Content-Range", "bytes */%d" % size)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                code = 206
                extra["Content-Range"] = "bytes %d-%d/%d" % (start, end, size)
            except (ValueError, IndexError):
                start, end, code = 0, max(0, size - 1), 200

        length = max(0, end - start + 1)
        disp = "attachment" if download else "inline"
        if filename:
            disp += '; filename="%s"' % filename.replace('"', "")
        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Disposition", disp)
            self.send_header("Cache-Control", "private, max-age=300")
            for k, v in extra.items():
                self.send_header(k, v)
            self.end_headers()
            f.seek(start)
            if self.command == "HEAD":
                return
            remaining = length
            while remaining > 0:
                chunk = f.read(min(1 << 16, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        finally:
            f.close()

    def _base_urls(self):
        port = self.server.server_address[1]
        urls = []
        if PUBLIC_URL:
            urls.append(PUBLIC_URL.rstrip("/"))
        host = self.headers.get("Host", "")
        if host and not host.startswith(("127.", "localhost", "[::1]")):
            u = "http://" + host
            if u not in urls:
                urls.insert(0, u)
        for ip in lan_ips():
            u = "http://%s:%d" % (ip, port)
            if u not in urls:
                urls.append(u)
        if not urls:
            urls.append("http://127.0.0.1:%d" % port)
        return urls

    def _localhost_warning(self):
        host = self.headers.get("Host", "")
        if host.startswith(("127.", "localhost", "[::1]")):
            return ("你现在是通过本机地址打开的。Switch 无法访问 127.0.0.1，"
                    "请改用下面的局域网地址（同一 Wi-Fi 下）。")
        return ""

    # ---------------------------------------------------------------------
    # GET routing
    # ---------------------------------------------------------------------
    def do_GET(self):
        try:
            u = urlsplit(self.path)
            path = u.path
            if path.startswith("/api/v1/"):
                return self.api_get(u)
            if path == "/favicon.svg":
                return self._respond(200, webui.FAVICON.encode("utf-8"), "image/svg+xml")
            if path in ("/", "/index.html"):
                return self.route_root()
            if path == "/setup":
                return self.page_setup()
            if path == "/setup/status":
                return self.page_setup_status()
            if path == "/login":
                return self.page_login()
            if path == "/logout":
                return self._redirect("/login")

            session = self._require_admin()
            if not session:
                return

            if path == "/dashboard":
                return self.page_dashboard(session)
            if path == "/devices":
                return self.page_devices(session)
            if path == "/saves":
                return self.page_saves(session, u)
            if path == "/saves/history":
                return self.page_save_history(session, u)
            if path == "/saves/download":
                return self.page_save_download(u)
            if path == "/album":
                return self.page_album(session, u)
            if path == "/album/view":
                return self.page_album_view(session, u)
            if path == "/album/file":
                return self.page_album_file(u)
            if path == "/playtime":
                return self.page_playtime(session, u)
            if path == "/playlog":
                return self.page_playlog(session, u)
            if path == "/playlog/title":
                return self.page_playlog_title(session, u)
            if path == "/settings":
                return self.page_settings(session, u)

            return self._error(404, "页面不存在")
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except Exception:
            log.exception("GET %s failed", self.path)
            try:
                self._error(500, "服务器内部错误，请查看日志")
            except Exception:
                pass

    # ---------------------------------------------------------------------
    # POST routing
    # ---------------------------------------------------------------------
    def do_POST(self):
        try:
            u = urlsplit(self.path)
            path = u.path
            if path == "/login":
                return self.do_login()
            if path == "/setup/admin":
                return self.do_setup_admin()
            if path == "/logout":
                session = self._require_admin()
                if not session:
                    return
                form = self._read_form()
                if not self._csrf_ok(session, form):
                    return self._error(403, "CSRF 校验失败")
                self._drop_session()
                return self._redirect("/login", cookie="")
            if path == "/settings/token":
                return self.do_reset_token()
            if path == "/settings/password":
                return self.do_change_password()
            return self._error(404, "页面不存在")
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except Exception:
            log.exception("POST %s failed", self.path)
            try:
                self._error(500, "服务器内部错误，请查看日志")
            except Exception:
                pass

    # ---------------------------------------------------------------------
    # UI pages
    # ---------------------------------------------------------------------
    def route_root(self):
        if not STORE.admin_configured():
            return self._redirect("/setup")
        if self._session():
            return self._redirect("/dashboard")
        return self._redirect("/login")

    def page_setup(self):
        if STORE.admin_configured():
            return self._redirect("/login")
        dev = STORE.first_device()
        html = webui.setup_page(
            self._base_urls(),
            STORE.token,
            STORE.client_seen_at() > 0,
            dev,
            False,
            self._localhost_warning(),
        )
        return self._html(html)

    def page_setup_status(self):
        dev = STORE.first_device()
        return self._json({
            "connected": STORE.client_seen_at() > 0,
            "admin": STORE.admin_configured(),
            "device_id": dev["id"] if dev else "",
            "device_name": (dev["name"] or "") if dev else "",
            "last_seen": dev["last_seen"] if dev else 0,
        })

    def page_login(self):
        if not STORE.admin_configured():
            return self._redirect("/setup")
        if self._session():
            return self._redirect("/dashboard")
        return self._html(webui.login_page(""))

    def page_dashboard(self, session):
        stats = STORE.stats()
        devices = STORE.list_devices()
        activity = STORE.recent_activity(10)
        top = STORE.list_playtime(limit=5)
        return self._html(webui.dashboard_page(stats, devices, activity, top, session["csrf"]))

    def page_devices(self, session):
        return self._html(webui.devices_page(STORE.list_devices(), session["csrf"]))

    def page_saves(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        saves = STORE.list_saves(device or None)
        return self._html(webui.saves_page(saves, STORE.list_devices(), device, session["csrf"]))

    def page_save_history(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        title = norm_title(self._param(q, "title_id"))
        user = norm_user(self._param(q, "user_id"))
        save = STORE.get_save(device, title, user) if device and title else None
        if not save:
            return self._error(404, "找不到该存档")
        versions = STORE.save_versions(device, title, user)
        return self._html(webui.history_page(save, versions, session["csrf"]))

    def page_save_download(self, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        title = norm_title(self._param(q, "title_id"))
        user = norm_user(self._param(q, "user_id"))
        version = self._param(q, "version", "latest")
        if not device or not title:
            return self._error(400, "参数不完整")

        row = None
        if version == "latest":
            row = STORE.get_save(device, title, user)
        else:
            try:
                row = STORE.save_version(device, title, user, int(version))
            except ValueError:
                row = None
        if not row:
            return self._error(404, "找不到该版本")

        try:
            path = STORE.abs_path(row["path"])
        except ValueError:
            return self._error(400, "非法路径")
        return self._serve_file(path, "application/zip", download=True,
                                filename="%s-%s.zip" % (title, user),
                                allow_range=False)

    def page_album(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        kind = self._param(q, "kind", "all")
        try:
            page_no = max(1, int(self._param(q, "page", "1")))
        except ValueError:
            page_no = 1
        per_page = 48
        total = STORE.album_count(device or None, kind)
        pages = max(1, (total + per_page - 1) // per_page)
        page_no = min(page_no, pages)
        items = STORE.list_album(device or None, kind, per_page, (page_no - 1) * per_page)
        return self._html(webui.album_page(items, STORE.list_devices(), device, kind,
                                           page_no, pages, total, session["csrf"]))

    def page_album_view(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        path = self._param(q, "path")
        item = STORE.get_album(device, path)
        if not item:
            return self._error(404, "找不到该文件")
        return self._html(webui.album_view_page(item, session["csrf"]))

    def page_album_file(self, u):
        q = parse_qs(u.query)
        device = safe_device(self._param(q, "device"))
        path = safe_album_path(self._param(q, "path"))
        if not device or not path:
            return self._error(400, "参数不合法")
        item = STORE.get_album(device, path)
        if not item:
            return self._error(404, "找不到该文件")
        try:
            full = STORE.abs_path(STORE.album_rel_path(device, path))
        except ValueError:
            return self._error(400, "非法路径")
        ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
        download = self._param(q, "download") == "1"
        return self._serve_file(full, ctype, download=download,
                                filename=os.path.basename(path))

    def page_playtime(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        rows = STORE.list_playtime(device or None)
        total = STORE.playtime_total_ns(device or None)
        return self._html(webui.playtime_page(rows, STORE.list_devices(), device, total,
                                              session["csrf"]))

    def page_playlog(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        log_rows = STORE.playlog_rows()
        if not device and log_rows:
            device = log_rows[0]["device"]
        data = None
        if device:
            _row, text = STORE.get_playlog(device)
            if text:
                try:
                    data = json.loads(text)
                except Exception:
                    data = None
        return self._html(webui.playlog_page(data, STORE.list_devices(), device,
                                             session["csrf"]))

    def page_playlog_title(self, session, u):
        q = parse_qs(u.query)
        device = self._param(q, "device")
        uid = norm_user(self._param(q, "uid"))
        title_id = norm_title(self._param(q, "title"))
        _row, text = STORE.get_playlog(device)
        if not text:
            return self._error(404, "找不到游玩记录")
        try:
            data = json.loads(text)
        except Exception:
            return self._error(500, "游玩记录 JSON 损坏")

        for user in data.get("users", []):
            if norm_user(user.get("id", "")) != uid:
                continue
            for title in user.get("titles", []):
                if norm_title(title.get("id", "")) != title_id:
                    continue
                return self._html(webui.playlog_title_page(
                    device, user.get("name") or uid, title.get("name") or title_id,
                    title.get("summary") or {}, title.get("events") or [], session["csrf"]))
        return self._error(404, "找不到该游戏")

    def page_settings(self, session, u):
        q = parse_qs(u.query)
        msg_code = self._param(q, "msg")
        messages = {
            "token": ("访问令牌已重置，请在所有客户端更新令牌。", "ok"),
            "password": ("管理员密码已更新。", "ok"),
            "tokenerr": ("重置令牌失败。", "bad"),
            "pwdbad": ("当前密码不正确。", "bad"),
            "pwdmismatch": ("两次输入的新密码不一致。", "bad"),
            "pwdshort": ("新密码至少 8 位。", "bad"),
        }
        msg, kind = messages.get(msg_code, ("", "ok"))
        return self._html(webui.settings_page(
            self._base_urls(), STORE.token, STORE.list_devices(),
            session["user"], session["csrf"], msg, kind))

    # ---------------------------------------------------------------------
    # auth actions
    # ---------------------------------------------------------------------
    def _login_allowed(self, ip):
        now = time.time()
        with LOGIN_LOCK:
            count, start = LOGIN_FAILS.get(ip, (0, now))
            if now - start > 300:
                LOGIN_FAILS[ip] = (0, now)
                return True
            return count < 6

    def _login_failed(self, ip):
        now = time.time()
        with LOGIN_LOCK:
            count, start = LOGIN_FAILS.get(ip, (0, now))
            if now - start > 300:
                count, start = 0, now
            LOGIN_FAILS[ip] = (count + 1, start)

    def _login_ok(self, ip):
        with LOGIN_LOCK:
            LOGIN_FAILS.pop(ip, None)

    def do_login(self):
        if not STORE.admin_configured():
            return self._redirect("/setup")
        form = self._read_form()
        ip = self.client_address[0]
        if not self._login_allowed(ip):
            return self._html(webui.login_page("尝试次数过多，请 5 分钟后再试"), 429)

        username = (form.get("username") or "").strip()
        password = form.get("password") or ""
        if STORE.check_admin(username, password):
            self._login_ok(ip)
            sid = self._new_session(username)
            log.info("admin login from %s", ip)
            return self._redirect("/dashboard", cookie=sid)

        self._login_failed(ip)
        log.warning("failed admin login from %s", ip)
        return self._html(webui.login_page("用户名或密码错误"))

    def do_setup_admin(self):
        if STORE.admin_configured():
            return self._redirect("/login")
        if not self._same_origin():
            return self._error(403, "来源校验失败")
        if not STORE.client_seen_at():
            return self._html(webui.setup_page(
                self._base_urls(), STORE.token, False, STORE.first_device(), False,
                "还没有检测到客户端连接，请先在 Switch 上点一次「测试服务器连接」。"), 400)

        form = self._read_form()
        username = (form.get("username") or "").strip()
        password = form.get("password") or ""
        password2 = form.get("password2") or ""

        error = ""
        if not re.fullmatch(r"[A-Za-z0-9_\-\.]{3,32}", username):
            error = "用户名需要 3-32 位字母、数字、下划线、点或横线"
        elif len(password) < 8:
            error = "密码至少 8 位"
        elif password != password2:
            error = "两次输入的密码不一致"
        if error:
            return self._html(webui.setup_page(
                self._base_urls(), STORE.token, True, STORE.first_device(), False, error), 400)

        STORE.set_admin(username, password)
        log.info("admin account created: %s", username)
        sid = self._new_session(username)
        return self._redirect("/dashboard", cookie=sid)

    def do_reset_token(self):
        session = self._require_admin()
        if not session:
            return
        form = self._read_form()
        if not self._csrf_ok(session, form):
            return self._error(403, "CSRF 校验失败")
        STORE.reset_token()
        log.info("api token reset by %s", session["user"])
        return self._redirect("/settings?msg=token")

    def do_change_password(self):
        session = self._require_admin()
        if not session:
            return
        form = self._read_form()
        if not self._csrf_ok(session, form):
            return self._error(403, "CSRF 校验失败")

        old = form.get("old") or ""
        new = form.get("new") or ""
        new2 = form.get("new2") or ""
        if not STORE.check_admin(session["user"], old):
            return self._redirect("/settings?msg=pwdbad")
        if len(new) < 8:
            return self._redirect("/settings?msg=pwdshort")
        if new != new2:
            return self._redirect("/settings?msg=pwdmismatch")
        STORE.set_admin(session["user"], new)
        log.info("admin password changed by %s", session["user"])
        return self._redirect("/settings?msg=password")

    # ---------------------------------------------------------------------
    # API: GET
    # ---------------------------------------------------------------------
    def api_get(self, u):
        if not self._api_auth(u):
            return

        if u.path == "/api/v1/ping":
            return self._text(200, "ok switchcloud v1 %s\n" % STORE.ensure_server_id())

        q = parse_qs(u.query)
        device = safe_device(self._param(q, "device"))
        if not device:
            return self._text(400, "error=bad_request\nmessage=device\n")

        if u.path == "/api/v1/saves":
            rows = STORE.list_saves(device)
            lines = ["#title_id\tuser_id\ttimestamp\tsize\tsha256\ttitle_name"]
            for r in rows:
                name = one_line(r["title_name"] or "")
                lines.append("%s\t%s\t%d\t%d\t%s\t%s" % (
                    r["title_id"], r["user_id"], int(r["timestamp"]), int(r["size"]),
                    r["sha256"] or "", name))
            return self._respond(200, ("\n".join(lines) + "\n").encode("utf-8"),
                                 "text/tab-separated-values; charset=utf-8")

        if u.path == "/api/v1/saves/latest":
            title = norm_title(self._param(q, "title_id"))
            raw_user = self._param(q, "user_id")
            user = norm_user(raw_user) if raw_user != "" else ""
            if not title or not user:
                return self._text(400, "error=bad_request\nmessage=title_id/user_id\n")
            row = STORE.get_save(device, title, user)
            if not row:
                return self._text(404, "error=not_found\n")
            try:
                path = STORE.abs_path(row["path"])
            except ValueError:
                return self._text(400, "error=bad_path\n")
            return self._serve_file(path, "application/zip", allow_range=False)

        if u.path == "/api/v1/playlog":
            _row, text = STORE.get_playlog(device)
            if not text:
                return self._text(404, "error=not_found\n")
            return self._respond(200, text.encode("utf-8"),
                                 "application/json; charset=utf-8")

        return self._text(404, "error=not_found\n")

    # ---------------------------------------------------------------------
    # API: PUT
    # ---------------------------------------------------------------------
    def do_PUT(self):
        try:
            u = urlsplit(self.path)
            if not u.path.startswith("/api/v1/"):
                return self._text(404, "error=not_found\n")
            if not self._api_auth(u):
                return

            length = self._content_length()
            if length is None:
                return self._text(411, "error=length_required\n")

            q = parse_qs(u.query)
            if u.path == "/api/v1/saves":
                return self.api_put_save(q, length)
            if u.path == "/api/v1/album":
                return self.api_put_album(q, length)
            if u.path == "/api/v1/playtime":
                return self.api_put_playtime(q, length)
            if u.path == "/api/v1/playlog":
                return self.api_put_playlog(q, length)
            return self._text(404, "error=not_found\n")
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except Exception:
            log.exception("PUT %s failed", self.path)
            try:
                self._text(500, "error=internal\nmessage=see server log\n")
            except Exception:
                pass

    def api_put_save(self, q, length):
        device = safe_device(self._param(q, "device"))
        raw_user = self._param(q, "user_id")
        title = norm_title(self._param(q, "title_id"))
        user = norm_user(raw_user) if raw_user != "" else ""
        if not device or not title or not user:
            return self._text(400, "error=bad_request\nmessage=device/title_id/user_id\n")
        if length <= 0:
            return self._text(411, "error=length_required\n")

        try:
            ts = int(self._param(q, "timestamp") or time.time())
        except ValueError:
            ts = int(time.time())
        name = one_line(self._param(q, "title_name"))

        rel_latest = STORE.save_rel_path(device, title, user)
        try:
            dst = STORE.abs_path(rel_latest)
        except ValueError:
            return self._text(400, "error=bad_path\n")
        tmp = dst + ".part-%d-%d" % (os.getpid(), threading.get_ident())

        try:
            sha = self._stream_body(tmp, length)
        except Exception as exc:
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self._text(400, "error=body\nmessage=%s\n" % one_line(str(exc), 200))

        want = (self.headers.get("X-Content-SHA256") or "").strip().lower()
        if want and not hmac.compare_digest(want, sha):
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self._text(400, "error=checksum\nmessage=sha256 mismatch\n")

        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.replace(tmp, dst)

        rel_hist = STORE.save_history_rel_path(device, title, user, ts)
        try:
            STORE.copy_to_history(dst, rel_hist)
        except OSError:
            log.warning("cannot write save history for %s/%s/%s", device, title, user)

        size = os.path.getsize(dst)
        STORE.put_save(device, title, user, name, ts, size, sha, rel_latest, rel_hist)
        log.info("save  %s/%s/%s  %s  %s", device, title, user, fmt_size(size), sha[:12])
        return self._text(200, "stored=ok\nsha256=%s\nbytes=%d\n" % (sha, size))

    def api_put_album(self, q, length):
        device = safe_device(self._param(q, "device"))
        rel_album = safe_album_path(self._param(q, "path"))
        if not device or not rel_album:
            return self._text(400, "error=bad_request\nmessage=device/path\n")
        if length <= 0:
            return self._text(411, "error=length_required\n")

        rel = STORE.album_rel_path(device, rel_album)
        try:
            dst = STORE.abs_path(rel)
        except ValueError:
            return self._text(400, "error=bad_path\n")
        tmp = dst + ".part"
        try:
            sha = self._stream_body(tmp, length)
        except Exception as exc:
            try:
                os.remove(tmp)
            except OSError:
                pass
            return self._text(400, "error=body\nmessage=%s\n" % one_line(str(exc), 200))

        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.replace(tmp, dst)

        try:
            mtime = int(self.headers.get("X-File-Mtime") or 0)
        except ValueError:
            mtime = 0
        size = os.path.getsize(dst)
        STORE.put_album(device, rel_album, size, mtime, sha, rel)
        log.info("album %s/%s  %s", device, rel_album, fmt_size(size))
        return self._text(200, "stored=ok\nsha256=%s\nbytes=%d\n" % (sha, size))

    def api_put_playtime(self, q, length):
        device = safe_device(self._param(q, "device"))
        if not device:
            return self._text(400, "error=bad_request\nmessage=device\n")

        body = self._read_all(length).decode("utf-8", "replace")
        rows = []
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
            rows.append((title, user, playtime_ns, launches, last_played, name))

        accepted = STORE.put_playtime(device, rows)
        log.info("playtime %s  %d rows", device, accepted)
        return self._text(200, "accepted=%d\n" % accepted)

    def api_put_playlog(self, q, length):
        device = safe_device(self._param(q, "device"))
        if not device:
            return self._text(400, "error=bad_request\nmessage=device\n")

        raw = self._read_all(length)
        try:
            data = json.loads(raw.decode("utf-8"))
            if not isinstance(data, dict) or not isinstance(data.get("users"), list):
                raise ValueError("missing users[]")
        except Exception as exc:
            return self._text(400, "error=json\nmessage=%s\n" % one_line(str(exc), 200))

        titles = 0
        summary_rows = []
        for user in data.get("users", []):
            uid = norm_user(user.get("id", ""))
            name = one_line(user.get("name") or "", 64)
            if uid and uid != "0":
                STORE.put_user(device, uid, name)
            for title in user.get("titles", []):
                tid = norm_title(title.get("id", ""))
                if not tid:
                    continue
                titles += 1
                summary = title.get("summary") or {}
                try:
                    playtime_ns = int(summary.get("playtime", 0)) * 1_000_000_000
                    launches = int(summary.get("launches", 0))
                    last_played = int(summary.get("lastPlayed", 0))
                except (TypeError, ValueError):
                    continue
                if uid:
                    summary_rows.append((tid, uid, playtime_ns, launches, last_played,
                                         one_line(title.get("name") or "")))

        if summary_rows:
            STORE.put_playtime(device, summary_rows)
        STORE.put_playlog(device, raw.decode("utf-8", "replace"),
                          len(data.get("users", [])), titles)
        log.info("playlog %s  %d titles  %s", device, titles, fmt_size(len(raw)))
        return self._text(200, "accepted=%d\n" % titles)

    # HEAD for simple checks (and media players)
    def do_HEAD(self):
        self.do_GET()


# ---------------------------------------------------------------------------
# startup
# ---------------------------------------------------------------------------

def write_info(path, port, token, urls):
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write("Switch Cloud Sync Server %s\n\n" % VERSION)
            f.write("管理页面: http://127.0.0.1:%d/\n" % port)
            for u in urls:
                f.write("对接地址: %s\n" % u)
            f.write("访问令牌: %s\n" % token)
            f.write("\n首次使用: 打开管理页面，按向导先把上面的地址和令牌填进 Switch 应用。\n")
    except OSError:
        pass


def main():
    global STORE, PUBLIC_URL, DATA_DIR, MAX_BODY

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=APP)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8420)
    parser.add_argument("--data", default=os.path.join(here, "data"))
    parser.add_argument("--public-url", default="",
                        help="external base URL when behind a reverse proxy")
    parser.add_argument("--set-admin", default="", help="user:password (emergency setup)")
    parser.add_argument("--reset-token", action="store_true")
    parser.add_argument("--token", default="", help="set a custom access token")
    parser.add_argument("--token-length", type=int, default=0,
                        help="characters for generated tokens (default 8, grouped 4-4)")
    parser.add_argument("--max-body-gb", type=float, default=4.0)
    args = parser.parse_args()

    DATA_DIR = os.path.abspath(args.data)
    PUBLIC_URL = args.public_url.strip()
    MAX_BODY = int(args.max_body_gb * 1024 ** 3) or (4 * 1024 ** 3)

    os.makedirs(DATA_DIR, exist_ok=True)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[
            logging.FileHandler(os.path.join(DATA_DIR, "server.log"), encoding="utf-8"),
            logging.StreamHandler(sys.stdout),
        ],
    )

    STORE = Store(DATA_DIR)
    if args.token_length:
        STORE.set_setting("token_length", str(max(6, min(args.token_length, 64))))
    if args.reset_token:
        STORE.reset_token()
    if args.token:
        STORE.set_token(args.token)
    if args.set_admin:
        user, _, password = args.set_admin.partition(":")
        if user and len(password) >= 8:
            STORE.set_admin(user, password)
            log.info("admin account set from CLI: %s", user)
        else:
            log.error("--set-admin needs user:password with an 8+ char password")

    token = STORE.token
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True

    urls = []
    if PUBLIC_URL:
        urls.append(PUBLIC_URL.rstrip("/"))
    urls.extend("http://%s:%d" % (ip, args.port) for ip in lan_ips())

    print("=" * 66)
    print(" %s %s" % (APP, VERSION))
    print(" 管理页面 : http://127.0.0.1:%d/" % args.port)
    for u in urls:
        print(" 对接地址 : %s" % u)
    print(" 访问令牌 : %s" % token)
    print(" 数据目录 : %s" % DATA_DIR)
    print("=" * 66)
    print(" 首次使用：浏览器打开管理页面，按向导对接客户端并创建管理员。")
    print(" Ctrl+C 停止" )
    sys.stdout.flush()

    write_info(os.path.join(DATA_DIR, "INFO.txt"), args.port, token, urls)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.server_close()
        STORE.close()


if __name__ == "__main__":
    main()
