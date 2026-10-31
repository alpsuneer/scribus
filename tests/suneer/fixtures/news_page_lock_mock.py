#!/usr/bin/env python3
"""Mock workflow server for tests/suneer/news_page_lock.sh - two users and the
workflow's page lock, ported from dbi-workflow backend/src/services/news.service.ts
(getLayoutPagesWithStatus, updateLayoutPageLockStatus, lockLayoutPages,
unlockLayoutPages, toggleSingleLayoutPageLockStatus). Nothing reaches a real server.

usage: news_page_lock_mock.py <port>
  users: a@test.local / S3cret-right -> token AT-A (id u-A, username "suneer")
         b@test.local / S3cret-right -> token AT-B (id u-B, username "otheruser")
  editions KNR (Page 1..8) and TVM (Page 1..3); A has both, B has KNR.
  test-only: POST /api/v1/mock/hang {"on": true} makes GET /news/layout/pages hang;
             GET /api/v1/mock/state lists the locks.
"""
import json
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

PORT = int(sys.argv[1])
USERS = {
    "AT-A": {"id": "u-A", "username": "suneer", "firstName": "Suneer", "lastName": "Test", "email": "a@test.local",
             "groups": [("eg-A-knr", "ed-knr"), ("eg-A-tvm", "ed-tvm")]},
    "AT-B": {"id": "u-B", "username": "otheruser", "firstName": "Other", "lastName": "User", "email": "b@test.local",
             "groups": [("eg-B-knr", "ed-knr")]},
}
EDITIONS = {"ed-knr": ("KNR", "Kannur", 8), "ed-tvm": ("TVM", "Thiruvananthapuram", 3)}
PAGES = {ed: [{"id": "p-%s-%d" % (sn.lower(), i), "name": "Page %d" % i, "displayOrder": i}
              for i in range(1, n + 1)] for ed, (sn, _, n) in EDITIONS.items()}
PAGE_EDITION = {p["id"]: ed for ed, ps in PAGES.items() for p in ps}
# LayoutPageStatus rows: (pageId, publishDate) -> {"lockedAt", "lockedById"}
STATUS = {}
LOCK = threading.Lock()
HANG = {"on": False}


def now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def user_json(u):
    return {"id": u["id"], "username": u["username"], "firstName": u["firstName"], "lastName": u["lastName"],
            "email": u["email"],
            "editionGroups": [{"id": gid, "editionId": ed,
                               "edition": {"id": ed, "name": EDITIONS[ed][1], "shortName": EDITIONS[ed][0]}}
                              for gid, ed in u["groups"]]}


def status_json(page_id, date):
    s = STATUS.get((page_id, date))
    if s is None:
        return None
    locker = next((u for u in USERS.values() if u["id"] == s["lockedById"]), None)
    return {"id": "st-%s-%s" % (page_id, date), "pageId": page_id, "publishDate": date + "T00:00:00.000Z",
            "lockedAt": s["lockedAt"], "lockedById": s["lockedById"], "isCompleted": False,
            "lockedBy": {"id": locker["id"], "username": locker["username"]} if locker else None,
            "page": {"id": page_id, "name": next(p["name"] for p in PAGES[PAGE_EDITION[page_id]] if p["id"] == page_id)}}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("mock %s\n" % (fmt % args))

    def reply(self, status, obj, cookie=None):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        self.wfile.write(body)

    def ok(self, data, cookie=None):
        self.reply(200, {"success": True, "data": data}, cookie)

    def bad(self, status, msg):
        self.reply(status, {"success": False, "status": status, "message": msg, "error": {"message": msg}})

    def body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        try:
            return json.loads(self.rfile.read(n) or b"{}") if n else {}
        except ValueError:
            return {}

    def user(self):
        auth = self.headers.get("Authorization") or ""
        return USERS.get(auth[7:]) if auth.startswith("Bearer ") else None

    def route(self, method):
        url = urlparse(self.path)
        path, q = url.path, parse_qs(url.query)
        if not path.startswith("/api/v1"):
            return self.bad(404, "Route not found")
        path = path[7:]
        if method == "POST" and path == "/users/auth/login":
            b = self.body()
            u = next((u for t, u in USERS.items() if u["email"] == b.get("email")), None)
            if not u or b.get("password") != "S3cret-right":
                return self.bad(401, "Invalid email or password")
            tok = next(t for t, x in USERS.items() if x is u)
            return self.ok({"accessToken": tok, "user": user_json(u)}, "refreshToken=RT-%s; Path=/; HttpOnly" % tok)
        if method == "POST" and path == "/users/auth/refresh":
            ck = self.headers.get("Cookie") or ""
            tok = next((t for t in USERS if "refreshToken=RT-%s" % t in ck), None)
            return self.ok({"accessToken": tok}, "refreshToken=RT-%s; Path=/; HttpOnly" % tok) if tok else self.bad(401, "Invalid refresh token")
        if method == "GET" and path == "/common/web/initial-data/static":
            return self.ok({"editions": [{"id": ed, "shortName": sn, "name": nm, "isPrintEdition": True}
                                         for ed, (sn, nm, _) in EDITIONS.items()]})
        if path == "/mock/state":
            with LOCK:
                return self.ok([{"pageId": k[0], "date": k[1], **v} for k, v in sorted(STATUS.items())])
        u = self.user()
        if u is None:
            return self.bad(401, "Unauthorized")
        if method == "POST" and path == "/mock/hang":
            HANG["on"] = bool(self.body().get("on"))
            return self.ok({"hang": HANG["on"]})
        if method == "GET" and path == "/users/me":
            return self.ok(user_json(u))
        if method == "POST" and path == "/users/auth/logout":
            return self.ok({})
        if method == "GET" and path == "/news/layout/pages":
            if HANG["on"]:
                time.sleep(30)
                return
            date = (q.get("publishDate") or [""])[0]
            gid = (q.get("editionGroup") or [""])[0]
            if not date:
                return self.bad(400, "Publish date is required!")
            ed = next((e for g, e in u["groups"] if g == gid), None)
            if ed is None:
                return self.bad(400, "Invalid edition group id provided!")
            with LOCK:
                return self.ok([{"id": p["id"], "name": p["name"], "status": status_json(p["id"], date)} for p in PAGES[ed]])
        if method == "PATCH" and path == "/news/layout/pages/lock":
            b = self.body()
            page_ids, date = b.get("pageIds") or [], (b.get("publishDate") or "")[:10]
            if not date:
                return self.bad(400, "publishDate is required")
            with LOCK:
                current = [k[0] for k, s in STATUS.items() if k[1] == date and s["lockedById"] == u["id"] and s["lockedAt"]]
                to_unlock = [p for p in current if p not in page_ids]
                to_lock = [p for p in page_ids if p not in current]
                if any(STATUS.get((p, date), {}).get("lockedAt") and STATUS[(p, date)]["lockedById"] != u["id"] for p in to_lock):
                    return self.bad(400, "Some selected pages are locked by others")
                for p in to_unlock:
                    STATUS[(p, date)] = {"lockedAt": None, "lockedById": None}
                for p in dict.fromkeys(to_lock):
                    s = STATUS.get((p, date))
                    if s is None or not s["lockedAt"]:
                        STATUS[(p, date)] = {"lockedAt": now(), "lockedById": u["id"]}
                mine = [status_json(k[0], date) for k, s in STATUS.items() if k[1] == date and s["lockedById"] == u["id"] and s["lockedAt"]]
            return self.ok({"lockedPages": mine, "success": True})
        if method == "PATCH" and path.startswith("/news/layout/pages/") and path.endswith("/toggle-lock"):
            pid = path[len("/news/layout/pages/"):-len("/toggle-lock")]
            b = self.body()
            date = (b.get("publishDate") or "")[:10]
            if pid not in PAGE_EDITION:
                return self.bad(400, "Page not found!")
            with LOCK:
                locked = bool(b.get("isLocked"))
                STATUS[(pid, date)] = {"lockedAt": now() if locked else None, "lockedById": u["id"] if locked else None}
                return self.ok(status_json(pid, date))
        return self.bad(404, "Route not found")

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PATCH(self):
        self.route("PATCH")


ThreadingHTTPServer.daemon_threads = True
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
