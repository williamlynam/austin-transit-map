#!/usr/bin/env python3
"""Serve the Live Preview page and relay CapMetro's realtime feed to it.

Browsers won't let a web page read CapMetro's feed directly (no CORS headers),
so this script fetches it and hands it to the page from the same address.
Standard library only: install Python 3 and run

    python serve.py                 (Windows: py serve.py, or double-click run-preview.bat)

then open http://localhost:8737 (it opens by itself).

Options:
    --port 8737          port to listen on (localhost only)
    --feed URL           VehiclePositions URL (protobuf or JSON)
    --record DIR         also save every new feed to DIR, for Replay in the page
    --no-browser         don't open a browser tab
"""
import argparse
import http.server
import os
import ssl
import sys
import threading
import time
import urllib.error
import urllib.request
import webbrowser

DEFAULT_FEED = "https://data.texas.gov/download/eiei-9rpf/application%2Foctet-stream"
JSON_FEED = "https://data.texas.gov/download/cuc7-ywmd/text%2Fplain"
CACHE_S = 10  # CapMetro updates about every 15 s; don't refetch more often than this
HERE = os.path.dirname(os.path.abspath(__file__))


class FeedCache:
    def __init__(self, record_dir):
        self.lock = threading.Lock()
        self.store = {}  # url -> (fetched_at, body, content_type)
        self.record_dir = record_dir
        self.last_body = None

    def get(self, url):
        with self.lock:
            hit = self.store.get(url)
            if hit and time.time() - hit[0] < CACHE_S:
                return hit[1], hit[2]
            req = urllib.request.Request(url, headers={"User-Agent": "austin-transit-map-live-preview/1"})
            ctx = ssl.create_default_context()
            with urllib.request.urlopen(req, timeout=20, context=ctx) as res:  # follows redirects
                body = res.read()
                ctype = res.headers.get("Content-Type", "application/octet-stream")
            self.store[url] = (time.time(), body, ctype)
            if self.record_dir and body != self.last_body:
                ext = "json" if body.lstrip()[:1] == b"{" else "pb"
                name = time.strftime("vehiclepositions-%Y%m%d-%H%M%S.") + ext
                with open(os.path.join(self.record_dir, name), "wb") as f:
                    f.write(body)
            self.last_body = body
            return body, ctype


def make_handler(cache, feed_url):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=HERE, **kw)

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path in ("/feed", "/feed.json"):
                url = JSON_FEED if path == "/feed.json" else feed_url
                try:
                    body, ctype = cache.get(url)
                except urllib.error.HTTPError as e:
                    return self.fail(502, f"CapMetro answered HTTP {e.code} for {url}")
                except Exception as e:  # network, DNS, TLS, timeout
                    return self.fail(502, f"Couldn't reach {url}: {e}")
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(body)
                return
            return super().do_GET()

        def fail(self, code, msg):
            sys.stderr.write(msg + "\n")
            data = msg.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, fmt, *args):
            if "/feed" in (args[0] if args else ""):
                sys.stderr.write("%s  %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8737)
    ap.add_argument("--feed", default=DEFAULT_FEED)
    ap.add_argument("--record", metavar="DIR")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    if args.record:
        os.makedirs(args.record, exist_ok=True)

    cache = FeedCache(args.record)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(cache, args.feed))
    url = f"http://localhost:{args.port}/"
    print(f"Live Preview: {url}")
    print(f"Feed: {args.feed}" + (f"\nRecording new feeds to {os.path.abspath(args.record)}" if args.record else ""))
    print("Press Ctrl+C to stop.")
    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
