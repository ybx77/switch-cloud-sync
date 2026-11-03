#!/usr/bin/env python3
"""
Smoke test for tools/dev_server.py (and for the API in docs/API.md).

    python tools/dev_smoke_test.py --base http://127.0.0.1:8420 --token test-token-123

It exercises every endpoint with a generated ZIP archive and prints PASS/FAIL.
"""

import argparse
import hashlib
import io
import json
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

TITLE = "0100000000010000"
USER = "0"


def build_zip_archive(title_id, user_id, title_name, files):
    meta = (
        "format=switchcloud-zip1\n"
        "title_id=%s\n"
        "user_id=%s\n"
        "title_name=%s\n"
        "save_data_type=1\n"
        "file_count=%d\n"
    ) % (title_id, user_id, title_name, len(files))
    index = "".join("%s\t0\n" % path for path, _ in sorted(files))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(".__scs__/meta.txt", meta)
        zf.writestr(".__scs__/index.txt", index)
        for path, data in sorted(files):
            zf.writestr(path, data)
    return buf.getvalue()


def call(method, url, token, data=None, headers=None, timeout=20):
    hdrs = {"User-Agent": "smoke-test"}
    if token:
        hdrs["Authorization"] = "Bearer " + token
    if headers:
        hdrs.update(headers)
    req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8420")
    parser.add_argument("--token", default="")
    parser.add_argument("--device", default="switch-devtest")
    args = parser.parse_args()

    base = args.base.rstrip("/")
    failures = []

    def check(name, ok, detail=""):
        print(("PASS  " if ok else "FAIL  ") + name + (("  " + detail) if detail else ""))
        if not ok:
            failures.append(name)

    # 1) ping
    st, body = call("GET", base + "/api/v1/ping", args.token)
    check("GET /ping", st == 200 and b"ok" in body, "HTTP %s" % st)

    # 2) auth failure must be rejected when a token is configured
    if args.token:
        st, _ = call("GET", base + "/api/v1/saves?device=" + args.device, "")
        check("auth required", st == 401, "HTTP %s" % st)

    # 3) playtime
    playtime = (
        "#switchcloud-playtime v1\n"
        "#title_id\tuser_id\tplaytime_ns\tlaunches\tlast_played\ttitle_name\n"
        + "%s\t%s\t3600000000000\t12\t1700000000\tSmoke Test Game\n" % (TITLE, USER)
    ).encode()
    st, body = call(
        "PUT",
        base + "/api/v1/playtime?device=" + urllib.parse.quote(args.device),
        args.token,
        data=playtime,
        headers={"Content-Type": "text/tab-separated-values; charset=utf-8"},
    )
    check("PUT /playtime", st == 200 and b"accepted=1" in body, "HTTP %s" % st)

    # 4) save upload
    archive = build_zip_archive(TITLE, USER, "Smoke Test Game", [("data.bin", b"hello save"), ("sub/a.bin", b"abc")])
    digest = hashlib.sha256(archive).hexdigest()
    query = urllib.parse.urlencode(
        {
            "device": args.device,
            "title_id": TITLE,
            "user_id": USER,
            "timestamp": 1700000000,
            "title_name": "Smoke Test Game",
        }
    )
    st, body = call(
        "PUT",
        base + "/api/v1/saves?" + query,
        args.token,
        data=archive,
        headers={
            "Content-Type": "application/octet-stream",
            "X-Content-SHA256": digest,
        },
    )
    check("PUT /saves", st == 200, "HTTP %s" % st)

    # 5) list
    st, body = call(
        "GET", base + "/api/v1/saves?device=" + urllib.parse.quote(args.device), args.token
    )
    rows = [r for r in body.decode("utf-8", "replace").splitlines() if r and not r.startswith("#")]
    ok = st == 200 and len(rows) >= 1 and rows[0].split("\t")[4] == digest
    check("GET /saves", ok, "HTTP %s, %d row(s)" % (st, len(rows)))

    # 6) download latest
    query = urllib.parse.urlencode({"device": args.device, "title_id": TITLE, "user_id": USER})
    st, body = call("GET", base + "/api/v1/saves/latest?" + query, args.token)
    check(
        "GET /saves/latest",
        st == 200 and body == archive,
        "HTTP %s, %d bytes" % (st, len(body)),
    )

    # 7) album upload
    mp4 = b"\x00\x00\x00\x18ftypmp42" + b"smoke-test-video" * 100
    query = urllib.parse.urlencode(
        {"device": args.device, "path": "2024/05/01/2024050112304567.mp4"}
    )
    st, body = call(
        "PUT",
        base + "/api/v1/album?" + query,
        args.token,
        data=mp4,
        headers={"Content-Type": "video/mp4", "X-File-Size": str(len(mp4))},
    )
    check("PUT /album", st == 200, "HTTP %s" % st)

    # 8) detailed play log
    playlog = {
        "exportTimestamp": 1700000000,
        "exportVersion": "1.1.0",
        "users": [
            {
                "id": USER.upper().zfill(32),
                "name": "tester",
                "titles": [
                    {
                        "id": TITLE.upper(),
                        "name": "Smoke Test Game",
                        "summary": {
                            "firstPlayed": 1699000000,
                            "lastPlayed": 1700000000,
                            "launches": 3,
                            "playtime": 5400,
                        },
                        "events": [
                            {"clockTimestamp": 1699000000, "steadyTimestamp": 1000, "type": "Launched"},
                            {"clockTimestamp": 1699000005, "steadyTimestamp": 1005, "type": "Gained Focus"},
                            {"clockTimestamp": 1700000000, "steadyTimestamp": 2000, "type": "Closed"},
                        ],
                    }
                ],
            }
        ],
    }
    st, body = call(
        "PUT",
        base + "/api/v1/playlog?device=" + urllib.parse.quote(args.device),
        args.token,
        data=json.dumps(playlog).encode(),
        headers={"Content-Type": "application/json; charset=utf-8"},
    )
    check("PUT /playlog", st == 200, "HTTP %s" % st)

    st, body = call(
        "GET", base + "/api/v1/playlog?device=" + urllib.parse.quote(args.device), args.token
    )
    ok = False
    try:
        echoed = json.loads(body.decode("utf-8"))
        ok = st == 200 and echoed["users"][0]["titles"][0]["id"] == TITLE.upper()
    except Exception:
        pass
    check("GET /playlog", ok, "HTTP %s" % st)

    print()
    if failures:
        print("FAILED: " + ", ".join(failures))
        return 1
    print("All checks passed against " + base)
    print("Device used by this test: " + args.device)
    return 0


if __name__ == "__main__":
    sys.exit(main())
