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
  Open page from workflow (tests/suneer/news_open_page.sh), files from $WFOPEN_FIXTURES:
    GET /news-layout-settings/page-context?pageId&publishDate  (the workflow's getPageLayoutContext)
    GET /sla-templates/<id>/download?date&pageId  -> page.sla (octet-stream) or obit.zip (zip)
  KNR Page 1 tpl-p1 (.sla), Page 3 tpl-obit (zip with images/), Page 4 "no template" chosen,
  Page 5 only the page's last-used tpl-p5, Page 6 -> 500, Page 7 -> never answers, Page 8 -> 400.
  Page sync (tests/suneer/news_page_sync.sh): KNR Page 2 tpl-sync (sync.sla from $WFOPEN_FIXTURES),
  stories n-1..n-8 with news_pages statuses (news.service.ts getLayoutNews = UNUSED of the page,
  /news/used = USED of the page, updateNewsDtpStatus + the Joi rule of newsDtpStatusUpdateSchema),
  GET /news-layout-settings (toWireRow, page-local pt) for Page 2;
  test-only: GET /mock/news (statuses + every PATCH), POST /mock/fail-patch {"on"} -> PATCH 503,
             POST /mock/set {"news", "page", "status"} (another PC changed a status).
"""
import os
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
FIX = os.environ.get("WFOPEN_FIXTURES", "")
# pageId -> (day's slaTemplateId, templateChosen, page's last-used template)
CONTEXT = {"p-knr-1": ("tpl-p1", True, "tpl-p1"), "p-knr-3": ("tpl-obit", True, "tpl-obit"),
           "p-knr-4": (None, True, "tpl-p1"), "p-knr-5": (None, False, "tpl-p5"),
           "p-knr-6": ("tpl-500", True, None), "p-knr-7": ("tpl-hang", True, None), "p-knr-8": ("tpl-gone", True, None)}
CONTEXT["p-knr-2"] = ("tpl-sync", True, "tpl-sync")
TEMPLATE_FILE = {"tpl-p1": ("page.sla", False), "tpl-p5": ("page.sla", False), "tpl-obit": ("obit.zip", True),
                 "tpl-sync": ("sync.sla", False)}
BODY = "ജില്ലയിൽ ഇന്നലെ നടന്ന പരിപാടിയിൽ നിരവധി പേർ പങ്കെടുത്തു. വിശദമായ റിപ്പോർട്ട് താഴെ."
# news id -> title, news_pages {pageId: dtpStatus} (no date: news_pages has none)
NEWS = {
    "n-1": ("തിരുവനന്തപുരത്ത് കനത്ത മഴ", {"p-knr-2": "UNUSED"}),
    "n-2": ("കണ്ണൂരിൽ പുതിയ പാലം തുറന്നു", {"p-knr-2": "USED"}),
    "n-3": ("കൊച്ചി മെട്രോ രണ്ടാം ഘട്ടം തുടങ്ങി", {"p-knr-2": "USED"}),
    "n-4": ("കോഴിക്കോട് പുസ്തകോത്സവം തുടങ്ങി", {"p-knr-2": "UNUSED"}),
    "n-5": ("സ്കൂൾ കലോത്സവം ഇന്ന് സമാപിക്കും", {"p-knr-2": "UNUSED"}),
    "n-6": ("നെല്ല് സംഭരണം വൈകുന്നു", {"p-knr-2": "UNUSED"}),
    "n-7": ("വിമാനത്താവളത്തിൽ പുതിയ ടെർമിനൽ", {"p-knr-2": "UNUSED"}),
    "n-8": ("മറ്റൊരു പേജിലെ വാർത്ത", {"p-knr-1": "USED"}),
}
# NewsLayoutSetting rows of Page 2 (page-local pt; the .sla frame = row + the page's position)
LAYOUT_ROWS = {"p-knr-2": [
    {"id": "ls-1", "news_id": "n-1", "frame_type": "news", "x_pos": 30, "y_pos": 40, "frame_width": 200, "height": 300, "hidden": False},
    {"id": "ls-2", "news_id": "n-2", "frame_type": "news", "x_pos": 250, "y_pos": 40, "frame_width": 200, "height": 150, "hidden": False},
    {"id": "ls-3", "news_id": "n-3", "frame_type": "news", "x_pos": 250, "y_pos": 600, "frame_width": 200, "height": 100, "hidden": True},
]}
PATCHES = []
FAIL_PATCH = {"on": False}


def page_name(pid):
    return next(p["name"] for p in PAGES[PAGE_EDITION[pid]] if p["id"] == pid)


def news_json(nid, page_id=None):
    title, pages = NEWS[nid]
    n = {"id": nid, "refId": "R" + nid, "title": title, "content": "<p>%s</p>" % BODY, "kicker": None, "byline": None,
         "createdAt": "2026-11-01T08:00:00.000Z", "status": "PUBLISHED", "newsMedia": [],
         "newsPages": [{"pageId": p, "page": {"id": p, "name": page_name(p)}, "dtpStatus": st} for p, st in pages.items()]}
    if page_id:
        n["page"] = {"pageId": page_id, "page": {"id": page_id, "name": page_name(page_id)}, "dtpStatus": pages.get(page_id, "UNUSED")}
    return n


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
        if method == "GET" and path.startswith("/external/edition-pages/") and path.endswith("/news"):
            pid = path[len("/external/edition-pages/"):-len("/news")]
            with LOCK:
                return self.ok([news_json(n, pid) for n, (_, pg) in NEWS.items() if pg.get(pid) == "UNUSED"])
        if path == "/mock/news":
            with LOCK:
                return self.ok({"news": {n: dict(pg) for n, (_, pg) in NEWS.items()}, "patches": PATCHES})
        if path == "/mock/state":
            with LOCK:
                return self.ok([{"pageId": k[0], "date": k[1], **v} for k, v in sorted(STATUS.items())])
        u = self.user()
        if u is None:
            return self.bad(401, "Unauthorized")
        if method == "POST" and path == "/mock/set":
            b = self.body()
            with LOCK:
                NEWS[b["news"]][1][b["page"]] = b["status"]
            return self.ok({})
        if method == "POST" and path == "/mock/fail-patch":
            FAIL_PATCH["on"] = bool(self.body().get("on"))
            return self.ok({"failPatch": FAIL_PATCH["on"]})
        if method == "GET" and path == "/news-layout-settings":
            pid, date = (q.get("pageId") or [""])[0], (q.get("publishDate") or [""])[0]
            if not pid or not date:
                return self.bad(400, "pageId and publishDate are required")
            return self.ok(LAYOUT_ROWS.get(pid, []))
        if method == "GET" and path == "/news/layout":
            pid = (q.get("pageId") or [""])[0]
            with LOCK:
                rows = [news_json(n, pid) for n, (_, pg) in NEWS.items() if pg.get(pid) == "UNUSED"]
            return self.ok({"page": 1, "pageSize": 100, "totalNewses": len(rows), "newses": rows})
        if method == "GET" and path in ("/news/used", "/news/balanced"):
            pid = (q.get("editionPage") or [""])[0]
            want = "USED" if path == "/news/used" else "BALANCED"
            with LOCK:
                rows = [{"id": n, "refId": "R" + n, "title": t, "createdAt": "2026-11-01T08:00:00.000Z",
                         "newsPages": news_json(n)["newsPages"]}
                        for n, (t, pg) in NEWS.items() if (pg.get(pid) == want if pid else want in pg.values())]
            key = "usedNews" if want == "USED" else "balancedNews"
            return self.ok({key: rows, "total" + key[0].upper() + key[1:]: len(rows)})
        if method == "PATCH" and path == "/news/dtp-status":
            b = self.body()
            st, ids, pid, frm = b.get("dtpStatus"), b.get("newsIds") or [], b.get("pageId"), b.get("fromStatus")
            with LOCK:
                PATCHES.append({"user": u["id"], "newsIds": ids, "dtpStatus": st, "pageId": pid, "fromStatus": frm,
                                "refused": FAIL_PATCH["on"]})
            if FAIL_PATCH["on"]:
                return self.bad(503, "Service unavailable (test)")
            # newsDtpStatusUpdateSchema
            if st not in ("USED", "BALANCED", "UNUSED") or not ids:
                return self.bad(400, '"dtpStatus" must be one of [USED, BALANCED, UNUSED]')
            if st != "UNUSED" and not pid:
                return self.bad(400, '"pageId" is required')
            if st == "UNUSED" and frm not in ("USED", "BALANCED"):
                return self.bad(400, '"fromStatus" is required')
            if st != "UNUSED" and "fromStatus" in b:
                return self.bad(400, '"fromStatus" is not allowed')
            missing = [i for i in ids if i not in NEWS]
            if missing:
                return self.bad(400, "Unable to find %d news item(s)" % len(missing))
            n = 0
            with LOCK:
                for i in ids:
                    for p, cur in NEWS[i][1].items():
                        if st == "UNUSED":
                            if cur == frm and (not pid or p == pid):
                                NEWS[i][1][p] = "UNUSED"; n += 1
                        elif p == pid:
                            NEWS[i][1][p] = st; n += 1
            return self.ok({"updatedCount": n, "status": st})
            HANG["on"] = bool(self.body().get("on"))
            return self.ok({"hang": HANG["on"]})
        if method == "GET" and path == "/users/me":
            return self.ok(user_json(u))
        if method == "POST" and path == "/users/auth/logout":
            return self.ok({})
        if method == "GET" and path == "/news-layout-settings/page-context":
            pid = (q.get("pageId") or [""])[0]
            if pid not in PAGE_EDITION:
                return self.bad(400, "Page not found")
            day, chosen, last = CONTEXT.get(pid, (None, False, None))
            return self.ok({"pageId": pid, "pageName": next(p["name"] for p in PAGES[PAGE_EDITION[pid]] if p["id"] == pid),
                            "editionId": PAGE_EDITION[pid], "pageType": "obituary" if pid == "p-knr-3" else None,
                            "slaTemplateId": day, "defaultSlaTemplateId": last, "sourcePageId": None, "rowCount": 3,
                            "templateChosen": chosen, "needsTemplateChoice": False})
        if method == "GET" and path.startswith("/sla-templates/") and path.endswith("/download"):
            tid = path[len("/sla-templates/"):-len("/download")]
            if not q.get("date"):
                return self.bad(400, "date is required")
            if tid == "tpl-500":
                return self.bad(500, "Internal server error")
            if tid == "tpl-hang":
                time.sleep(30)
                return self.bad(500, "too late")
            if tid not in TEMPLATE_FILE:
                return self.bad(400, "Template not found!")
            name, is_zip = TEMPLATE_FILE[tid]
            data = open(os.path.join(FIX, name), "rb").read()
            page = (q.get("pageId") or ["layout"])[0]
            self.send_response(200)
            self.send_header("Content-Type", "application/zip" if is_zip else "application/octet-stream")
            self.send_header("Content-Disposition", 'attachment; filename="%s_%s.%s"' % (page, q["date"][0], "zip" if is_zip else "sla"))
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
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
        if method == "GET" and path.startswith("/news/") and path.count("/") == 2 and path[6:] in NEWS:
            with LOCK:
                return self.ok(news_json(path[6:]))
        return self.bad(404, "Route not found")

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PATCH(self):
        self.route("PATCH")


ThreadingHTTPServer.daemon_threads = True
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
