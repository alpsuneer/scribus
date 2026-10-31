#!/usr/bin/env python3
"""Mock News API for tests/suneer/news_login.sh - nothing reaches a real server.

usage: news_login_mock.py <port> <mode>
  mode "normal": POST /api/v1/users/auth/login with password "S3cret-right" -> 200,
                 an access token and a refreshToken cookie; any other password
                 -> 401 {"message": "Invalid email or password"}.
                 /users/me, /users/auth/refresh, /users/auth/logout answer;
                 every other route -> 404.
  mode "hang":   accepts the connection and never answers (a dead server).
Each request is logged to stderr (path and status only, never the body).
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, MODE = int(sys.argv[1]), sys.argv[2]
USER = {"firstName": "Test", "lastName": "User", "editionGroups": [], "clientIp": "127.0.0.1", "ipAllowed": True}


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

    def body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(n) if n else b""

    def hang(self):
        time.sleep(120)

    def do_GET(self):
        if MODE == "hang":
            return self.hang()
        if self.path.startswith("/api/v1/users/me"):
            return self.reply(200, {"data": USER})
        self.reply(404, {"message": "Route not found"})

    def do_POST(self):
        if MODE == "hang":
            return self.hang()
        data = self.body()
        if self.path == "/api/v1/users/auth/login":
            try:
                req = json.loads(data or b"{}")
            except ValueError:
                req = {}
            if req.get("password") == "S3cret-right":
                return self.reply(200, {"data": {"accessToken": "AT-mock", "user": USER}},
                                  "refreshToken=RT-mock; Path=/; HttpOnly")
            return self.reply(401, {"message": "Invalid email or password"})
        if self.path == "/api/v1/users/auth/refresh":
            if "refreshToken=RT-mock" in (self.headers.get("Cookie") or ""):
                return self.reply(200, {"data": {"accessToken": "AT-mock2"}}, "refreshToken=RT-mock; Path=/; HttpOnly")
            return self.reply(401, {"message": "Invalid refresh token"})
        if self.path == "/api/v1/users/auth/logout":
            return self.reply(200, {"data": {}})
        self.reply(404, {"message": "Route not found"})


ThreadingHTTPServer.daemon_threads = True
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
