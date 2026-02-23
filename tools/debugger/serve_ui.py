#!/usr/bin/env python3
"""
Serve the Camel debugger Web UI (static files) and proxy /api/* to camel-db.

Usage:
  1. Start camel-db first (it listens on API port, default 8765).
  2. Run: python serve_ui.py [--port 8080] [--api-url http://127.0.0.1:8765]
  3. Open http://127.0.0.1:8080 in browser.

Options:
  --port PORT       Port for the UI server (default: 8080).
  --api-url URL     camel-db API base URL (default: http://127.0.0.1:8765).
  --ui-dir DIR      Directory containing index.html (default: script_dir/ui).
"""

import argparse
import os
import urllib.error
import urllib.request
from http.server import HTTPServer, SimpleHTTPRequestHandler


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description="Serve Camel debugger Web UI and proxy API to camel-db.")
    parser.add_argument("--port", type=int, default=8080, help="UI server port (default: 8080)")
    parser.add_argument("--api-url", default="http://127.0.0.1:8765", help="camel-db API base URL")
    parser.add_argument("--ui-dir", default=os.path.join(script_dir, "ui"), help="Directory with index.html")
    args = parser.parse_args()
    args.api_url = args.api_url.rstrip("/")

    ui_dir = os.path.abspath(args.ui_dir)
    if not os.path.isdir(ui_dir) or not os.path.isfile(os.path.join(ui_dir, "index.html")):
        print("Error: ui-dir must contain index.html. Current: %s" % ui_dir)
        return 1

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=ui_dir, **kw)

        def do_GET(self):
            if self.path.startswith("/api/"):
                self._proxy_to_api()
                return
            if self.path in ("", "/"):
                self.path = "/index.html"
            super().do_GET()

        def do_POST(self):
            if self.path.startswith("/api/"):
                self._proxy_to_api()
                return
            self.send_error(404)

        def _proxy_to_api(self):
            url = args.api_url + self.path
            try:
                req = urllib.request.Request(url, method=self.command)
                if self.command == "POST" and "Content-Length" in self.headers:
                    body = self.rfile.read(int(self.headers["Content-Length"]))
                    req.data = body
                    req.add_header("Content-Type", self.headers.get("Content-Type", "application/json"))
                with urllib.request.urlopen(req, timeout=5) as resp:
                    body = resp.read()
                    self.send_response(resp.status)
                    self.send_header("Content-Type", resp.headers.get("Content-type", "application/json"))
                    self.send_header("Content-Length", len(body))
                    self.end_headers()
                    self.wfile.write(body)
            except urllib.error.URLError as e:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                err = ('{"error":"API unreachable: %s"}' % str(e.reason)).encode()
                self.send_header("Content-Length", len(err))
                self.end_headers()
                self.wfile.write(err)
            except Exception as e:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                err = ('{"error":"%s"}' % str(e)).encode()
                self.send_header("Content-Length", len(err))
                self.end_headers()
                self.wfile.write(err)

        def log_message(self, format, *args):
            print("[serve_ui] %s" % (format % args))

    server = HTTPServer(("127.0.0.1", args.port), Handler)
    print("Camel debugger UI: http://127.0.0.1:%s (API proxy -> %s)" % (args.port, args.api_url))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    exit(main())
