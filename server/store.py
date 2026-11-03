"""SQLite + filesystem storage for the Switch Cloud Sync server."""

from __future__ import annotations

import base64
import hashlib
import hmac
import os
import re
import secrets
import shutil
import sqlite3
import threading
import time

SCHEMA = """
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT
);

CREATE TABLE IF NOT EXISTS devices (
    id         TEXT PRIMARY KEY,
    name       TEXT,
    ip         TEXT,
    version    TEXT,
    first_seen INTEGER,
    last_seen  INTEGER
);

CREATE TABLE IF NOT EXISTS users (
    device TEXT,
    uid    TEXT,
    name   TEXT,
    PRIMARY KEY (device, uid)
);

CREATE TABLE IF NOT EXISTS saves (
    device      TEXT,
    title_id    TEXT,
    user_id     TEXT,
    title_name  TEXT,
    timestamp   INTEGER,
    size        INTEGER,
    sha256      TEXT,
    path        TEXT,
    updated_at  INTEGER,
    PRIMARY KEY (device, title_id, user_id)
);

CREATE TABLE IF NOT EXISTS save_versions (
    device     TEXT,
    title_id   TEXT,
    user_id    TEXT,
    timestamp  INTEGER,
    size       INTEGER,
    sha256     TEXT,
    path       TEXT,
    created_at INTEGER,
    PRIMARY KEY (device, title_id, user_id, timestamp)
);

CREATE TABLE IF NOT EXISTS album (
    device      TEXT,
    path        TEXT,
    kind        TEXT,
    size        INTEGER,
    mtime       INTEGER,
    sha256      TEXT,
    uploaded_at INTEGER,
    PRIMARY KEY (device, path)
);

CREATE TABLE IF NOT EXISTS playtime (
    device       TEXT,
    title_id     TEXT,
    user_id      TEXT,
    playtime_ns  INTEGER,
    launches     INTEGER,
    last_played  INTEGER,
    title_name   TEXT,
    updated_at   INTEGER,
    PRIMARY KEY (device, title_id, user_id)
);

CREATE TABLE IF NOT EXISTS playlog (
    device     TEXT PRIMARY KEY,
    json_path  TEXT,
    users      INTEGER,
    titles     INTEGER,
    updated_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_saves_device ON saves(device);
CREATE INDEX IF NOT EXISTS idx_album_device ON album(device);
CREATE INDEX IF NOT EXISTS idx_playtime_device ON playtime(device);
"""

HASH_ITERATIONS = 200_000

# Crockford-like alphabet: no 0/O/1/I/L, easy to type on the Switch keyboard.
TOKEN_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
DEFAULT_TOKEN_LENGTH = 8


def make_token(raw_length: int = DEFAULT_TOKEN_LENGTH, group: int = 4) -> str:
    raw_length = max(6, min(int(raw_length), 64))
    raw = "".join(secrets.choice(TOKEN_ALPHABET) for _ in range(raw_length))
    if group > 0:
        return "-".join(raw[i:i + group] for i in range(0, len(raw), group))
    return raw


def normalize_token(value: str) -> str:
    """Case-insensitive, ignore dashes/spaces (easier to type)."""
    return re.sub(r"[^A-Za-z0-9]", "", value or "").upper()


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, HASH_ITERATIONS)
    return "pbkdf2_sha256$%d$%s$%s" % (HASH_ITERATIONS, _b64(salt), _b64(digest))


def verify_password(stored: str, password: str) -> bool:
    try:
        algo, iterations, salt_b64, hash_b64 = stored.split("$", 3)
        if algo != "pbkdf2_sha256":
            return False
        salt = base64.b64decode(salt_b64)
        expected = base64.b64decode(hash_b64)
        got = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, int(iterations))
        return hmac.compare_digest(got, expected)
    except Exception:
        return False


def image_kind(path: str) -> str:
    lower = path.lower()
    if lower.endswith((".mp4", ".mov", ".webm")):
        return "video"
    if lower.endswith((".jpg", ".jpeg", ".png", ".gif", ".webp")):
        return "image"
    return "other"


class Store:
    def __init__(self, data_dir: str):
        self.data_dir = os.path.abspath(data_dir)
        os.makedirs(self.data_dir, exist_ok=True)
        for sub in ("saves", "album", "playlog"):
            os.makedirs(os.path.join(self.data_dir, sub), exist_ok=True)

        self.db_path = os.path.join(self.data_dir, "cloud.db")
        self.lock = threading.Lock()
        self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("PRAGMA synchronous=NORMAL")
        self.conn.execute("PRAGMA busy_timeout=5000")
        self.conn.executescript(SCHEMA)
        self.conn.commit()

    # -- helpers ---------------------------------------------------------
    def close(self):
        with self.lock:
            self.conn.close()

    def abs_path(self, rel: str) -> str:
        """Resolve a DB path, refusing anything outside the data directory."""
        root = os.path.realpath(self.data_dir)
        full = os.path.realpath(os.path.join(root, rel))
        if full != root and not full.startswith(root + os.sep):
            raise ValueError("path outside data dir")
        return full

    def _exec(self, sql: str, params=()):
        with self.lock:
            cur = self.conn.execute(sql, params)
            self.conn.commit()
            return cur

    def _query(self, sql: str, params=()):
        with self.lock:
            return self.conn.execute(sql, params).fetchall()

    def _one(self, sql: str, params=()):
        with self.lock:
            return self.conn.execute(sql, params).fetchone()

    # -- settings --------------------------------------------------------
    def get_setting(self, key: str, default=None):
        row = self._one("SELECT value FROM settings WHERE key=?", (key,))
        return row["value"] if row else default

    def set_setting(self, key: str, value: str):
        self._exec(
            "INSERT INTO settings(key,value) VALUES(?,?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            (key, str(value)),
        )

    def ensure_token(self) -> str:
        token = self.get_setting("api_token")
        if not token:
            token = self._new_token()
            self.set_setting("api_token", token)
        return token

    def _new_token(self) -> str:
        try:
            length = int(self.get_setting("token_length", str(DEFAULT_TOKEN_LENGTH)))
        except ValueError:
            length = DEFAULT_TOKEN_LENGTH
        return make_token(length)

    def set_token(self, token: str) -> str:
        token = (token or "").strip()
        self.set_setting("api_token", token)
        return token

    def ensure_server_id(self) -> str:
        sid = self.get_setting("server_id")
        if not sid:
            sid = secrets.token_hex(8)
            self.set_setting("server_id", sid)
        return sid

    def reset_token(self) -> str:
        token = self._new_token()
        self.set_setting("api_token", token)
        return token

    @property
    def token(self) -> str:
        return self.ensure_token()

    # -- admin -----------------------------------------------------------
    def admin_configured(self) -> bool:
        return bool(self.get_setting("admin_user")) and bool(self.get_setting("admin_hash"))

    def admin_user(self) -> str:
        return self.get_setting("admin_user", "")

    def set_admin(self, username: str, password: str):
        self.set_setting("admin_user", username)
        self.set_setting("admin_hash", hash_password(password))
        self.set_setting("admin_created_at", str(int(time.time())))

    def check_admin(self, username: str, password: str) -> bool:
        stored_user = self.get_setting("admin_user", "")
        stored_hash = self.get_setting("admin_hash", "")
        if not stored_user or not stored_hash:
            return False
        user_ok = hmac.compare_digest(username, stored_user)
        pass_ok = verify_password(stored_hash, password)
        return user_ok and pass_ok

    # -- setup / client contact -----------------------------------------
    def client_seen_at(self) -> int:
        try:
            return int(self.get_setting("client_seen_at", "0") or 0)
        except ValueError:
            return 0

    def mark_contact(self, device: str = "", name: str = "", ip: str = "", version: str = ""):
        now = int(time.time())
        if not self.client_seen_at():
            self.set_setting("client_seen_at", str(now))
        if not device:
            return
        device = device[:64]
        if not device:
            return
        self._exec(
            "INSERT INTO devices(id,name,ip,version,first_seen,last_seen) VALUES(?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET "
            "  name=COALESCE(NULLIF(excluded.name,''), devices.name),"
            "  ip=COALESCE(NULLIF(excluded.ip,''), devices.ip),"
            "  version=COALESCE(NULLIF(excluded.version,''), devices.version),"
            "  last_seen=excluded.last_seen",
            (device, name[:80], ip[:64], version[:64], now, now),
        )

    def first_device(self):
        return self._one("SELECT * FROM devices ORDER BY first_seen LIMIT 1")

    # -- devices ---------------------------------------------------------
    def list_devices(self):
        return self._query("SELECT * FROM devices ORDER BY last_seen DESC")

    def get_device(self, device: str):
        return self._one("SELECT * FROM devices WHERE id=?", (device,))

    # -- users (nicknames from playlog) ----------------------------------
    def put_user(self, device: str, uid: str, name: str):
        if not name:
            return
        self._exec(
            "INSERT INTO users(device,uid,name) VALUES(?,?,?) "
            "ON CONFLICT(device,uid) DO UPDATE SET name=excluded.name",
            (device, uid, name[:64]),
        )

    def user_names(self, device: str) -> dict:
        rows = self._query("SELECT uid,name FROM users WHERE device=?", (device,))
        return {r["uid"]: r["name"] for r in rows}

    # -- saves -----------------------------------------------------------
    def save_rel_path(self, device: str, title_id: str, user_id: str) -> str:
        return "saves/%s/%s/%s/latest.zip" % (device, title_id, user_id)

    def save_history_rel_path(self, device: str, title_id: str, user_id: str, ts: int) -> str:
        return "saves/%s/%s/%s/history/%d.zip" % (device, title_id, user_id, ts)

    def put_save(self, device, title_id, user_id, title_name, timestamp, size, sha, rel_latest,
                 rel_history):
        now = int(time.time())
        self._exec(
            "INSERT INTO saves(device,title_id,user_id,title_name,timestamp,size,sha256,path,updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(device,title_id,user_id) DO UPDATE SET "
            "  title_name=COALESCE(NULLIF(excluded.title_name,''), saves.title_name),"
            "  timestamp=excluded.timestamp, size=excluded.size, sha256=excluded.sha256,"
            "  path=excluded.path, updated_at=excluded.updated_at",
            (device, title_id, user_id, title_name[:120], int(timestamp), int(size), sha,
             rel_latest, now),
        )
        self._exec(
            "INSERT OR REPLACE INTO save_versions"
            "(device,title_id,user_id,timestamp,size,sha256,path,created_at) "
            "VALUES(?,?,?,?,?,?,?,?)",
            (device, title_id, user_id, int(timestamp), int(size), sha, rel_history, now),
        )

    def list_saves(self, device=None, limit=None):
        sql = (
            "SELECT s.*, u.name AS user_name FROM saves s "
            "LEFT JOIN users u ON u.device=s.device AND u.uid=s.user_id "
        )
        params = []
        if device:
            sql += "WHERE s.device=? "
            params.append(device)
        sql += "ORDER BY s.title_name COLLATE NOCASE"
        if limit:
            sql += " LIMIT %d" % int(limit)
        return self._query(sql, tuple(params))

    def get_save(self, device, title_id, user_id):
        return self._one(
            "SELECT * FROM saves WHERE device=? AND title_id=? AND user_id=?",
            (device, title_id, user_id),
        )

    def save_versions(self, device, title_id, user_id):
        return self._query(
            "SELECT * FROM save_versions WHERE device=? AND title_id=? AND user_id=? "
            "ORDER BY timestamp DESC",
            (device, title_id, user_id),
        )

    def save_version(self, device, title_id, user_id, timestamp):
        return self._one(
            "SELECT * FROM save_versions WHERE device=? AND title_id=? AND user_id=? AND timestamp=?",
            (device, title_id, user_id, int(timestamp)),
        )

    # -- album -----------------------------------------------------------
    def album_rel_path(self, device: str, path: str) -> str:
        return "album/%s/%s" % (device, path)

    def put_album(self, device, path, size, mtime, sha, rel_path):
        now = int(time.time())
        self._exec(
            "INSERT INTO album(device,path,kind,size,mtime,sha256,uploaded_at) VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(device,path) DO UPDATE SET "
            "  size=excluded.size, mtime=excluded.mtime, sha256=excluded.sha256,"
            "  kind=excluded.kind, uploaded_at=excluded.uploaded_at",
            (device, path, image_kind(path), int(size), int(mtime), sha, now),
        )

    def list_album(self, device=None, kind=None, limit=60, offset=0):
        where = []
        params = []
        if device:
            where.append("device=?")
            params.append(device)
        if kind and kind != "all":
            where.append("kind=?")
            params.append(kind)
        sql = "SELECT * FROM album"
        if where:
            sql += " WHERE " + " AND ".join(where)
        sql += " ORDER BY path DESC LIMIT ? OFFSET ?"
        params.extend([int(limit), int(offset)])
        return self._query(sql, tuple(params))

    def album_count(self, device=None, kind=None):
        where = []
        params = []
        if device:
            where.append("device=?")
            params.append(device)
        if kind and kind != "all":
            where.append("kind=?")
            params.append(kind)
        sql = "SELECT COUNT(*) AS c FROM album"
        if where:
            sql += " WHERE " + " AND ".join(where)
        row = self._one(sql, tuple(params))
        return row["c"] if row else 0

    def get_album(self, device, path):
        return self._one("SELECT * FROM album WHERE device=? AND path=?", (device, path))

    # -- playtime --------------------------------------------------------
    def put_playtime(self, device, rows):
        now = int(time.time())
        accepted = 0
        with self.lock:
            for (title_id, user_id, playtime_ns, launches, last_played, title_name) in rows:
                self.conn.execute(
                    "INSERT INTO playtime"
                    "(device,title_id,user_id,playtime_ns,launches,last_played,title_name,updated_at) "
                    "VALUES(?,?,?,?,?,?,?,?) "
                    "ON CONFLICT(device,title_id,user_id) DO UPDATE SET "
                    "  playtime_ns=MAX(playtime.playtime_ns, excluded.playtime_ns),"
                    "  launches=MAX(playtime.launches, excluded.launches),"
                    "  last_played=MAX(playtime.last_played, excluded.last_played),"
                    "  title_name=CASE WHEN excluded.title_name<>'' "
                    "                  THEN excluded.title_name ELSE playtime.title_name END,"
                    "  updated_at=excluded.updated_at",
                    (device, title_id, user_id, int(playtime_ns), int(launches),
                     int(last_played), title_name[:120], now),
                )
                accepted += 1
            self.conn.commit()
        return accepted

    def list_playtime(self, device=None, limit=None):
        sql = (
            "SELECT p.*, u.name AS user_name FROM playtime p "
            "LEFT JOIN users u ON u.device=p.device AND u.uid=p.user_id "
        )
        params = []
        if device:
            sql += "WHERE p.device=? "
            params.append(device)
        sql += "ORDER BY p.playtime_ns DESC"
        if limit:
            sql += " LIMIT %d" % int(limit)
        return self._query(sql, tuple(params))

    def playtime_total_ns(self, device=None):
        sql = "SELECT COALESCE(SUM(playtime_ns),0) AS s FROM playtime"
        params = []
        if device:
            sql += " WHERE device=?"
            params.append(device)
        row = self._one(sql, tuple(params))
        return int(row["s"]) if row else 0

    # -- playlog ---------------------------------------------------------
    def put_playlog(self, device, json_text, user_count, title_count):
        rel = "playlog/%s.json" % device
        path = self.abs_path(rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".part"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(json_text)
        os.replace(tmp, path)
        self._exec(
            "INSERT INTO playlog(device,json_path,users,titles,updated_at) VALUES(?,?,?,?,?) "
            "ON CONFLICT(device) DO UPDATE SET json_path=excluded.json_path,"
            " users=excluded.users, titles=excluded.titles, updated_at=excluded.updated_at",
            (device, rel, int(user_count), int(title_count), int(time.time())),
        )
        return rel

    def playlog_rows(self):
        return self._query("SELECT * FROM playlog ORDER BY updated_at DESC")

    def get_playlog(self, device):
        row = self._one("SELECT * FROM playlog WHERE device=?", (device,))
        if not row:
            return None, None
        try:
            with open(self.abs_path(row["json_path"]), "r", encoding="utf-8") as f:
                return row, f.read()
        except OSError:
            return row, None

    # -- stats -----------------------------------------------------------
    def stats(self):
        saves = self._one("SELECT COUNT(*) c, COALESCE(SUM(size),0) s FROM saves")
        album = self._one("SELECT COUNT(*) c, COALESCE(SUM(size),0) s FROM album")
        devices = self._one("SELECT COUNT(*) c FROM devices")
        versions = self._one("SELECT COUNT(*) c FROM save_versions")
        play = self._one("SELECT COALESCE(SUM(playtime_ns),0) s FROM playtime")

        last = 0
        for sql in (
            "SELECT COALESCE(MAX(last_seen),0) v FROM devices",
            "SELECT COALESCE(MAX(updated_at),0) v FROM saves",
            "SELECT COALESCE(MAX(uploaded_at),0) v FROM album",
            "SELECT COALESCE(MAX(updated_at),0) v FROM playtime",
            "SELECT COALESCE(MAX(updated_at),0) v FROM playlog",
        ):
            row = self._one(sql)
            if row and row["v"]:
                last = max(last, int(row["v"]))

        return {
            "devices": devices["c"] if devices else 0,
            "saves": saves["c"] if saves else 0,
            "saves_bytes": int(saves["s"]) if saves else 0,
            "save_versions": versions["c"] if versions else 0,
            "album": album["c"] if album else 0,
            "album_bytes": int(album["s"]) if album else 0,
            "playtime_ns": int(play["s"]) if play else 0,
            "last_activity": last,
        }

    # -- misc ------------------------------------------------------------
    def recent_activity(self, limit=10):
        saves = self._query(
            "SELECT 'save' AS kind, device, title_name AS name, title_id, user_id, size, "
            "updated_at AS ts FROM saves ORDER BY updated_at DESC LIMIT ?",
            (int(limit),),
        )
        album = self._query(
            "SELECT 'album' AS kind, device, path AS name, '' AS title_id, '' AS user_id, size, "
            "uploaded_at AS ts FROM album ORDER BY uploaded_at DESC LIMIT ?",
            (int(limit),),
        )
        items = [dict(r) for r in saves] + [dict(r) for r in album]
        items.sort(key=lambda x: x["ts"] or 0, reverse=True)
        return items[:limit]

    def disk_usage(self):
        total = 0
        for root, _dirs, files in os.walk(self.data_dir):
            for name in files:
                try:
                    total += os.path.getsize(os.path.join(root, name))
                except OSError:
                    pass
        return total

    def copy_to_history(self, src_abs: str, hist_rel: str) -> str:
        dst = self.abs_path(hist_rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src_abs, dst)
        return hist_rel
