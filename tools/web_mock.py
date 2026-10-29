# Local stand-in for the Cardputer's web API, backed by the repo's sdcard/ folder (read-only).
#   python3 tools/web_mock.py sdcard web/index.html     then open http://127.0.0.1:8766 (any PIN works)
# /api/live serves a made-up fight and rolls so the Table page isn't empty.
import http.server, json, os, sys, urllib.parse
ROOT = sys.argv[1]; PAGE = sys.argv[2]
class H(http.server.BaseHTTPRequestHandler):
    def send(self, code, body, typ="text/plain"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header("Content-Type", typ); self.send_header("Content-Length", len(b)); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        if self.path.startswith("/api/login"): self.send(200, "tok")
        else: self.send(403, "read-only mock")
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = dict(urllib.parse.parse_qsl(u.query))
        if not u.path.startswith("/api"): return self.send(200, open(PAGE, "rb").read(), "text/html")
        p = os.path.join(ROOT, q.get("path", "/").lstrip("/"))
        if u.path == "/api/ls":
            return self.send(200, json.dumps([{"n": n, "d": int(os.path.isdir(os.path.join(p, n))), "s": os.path.getsize(os.path.join(p, n))} for n in os.listdir(p)]), "application/json")
        if u.path == "/api/rec":
            d = open(p, "rb").read()[int(q["off"]):]; e = d.find(b"\n\n"); return self.send(200, d[:e] if e >= 0 else d)
        if u.path == "/api/file": return self.send(200, open(p, "rb").read())
        if u.path == "/api/live": return self.send(200, '{"init": "2|1\\nAboleth|23|112|150||17|0|Aboleth\\nBrakka|18|31|36|Blessed:7|17|1|\\nChuul 1|14|52|76|Grappled:0|16|0|Chuul\\nMirela|13|9|14|Concentrating:0|12|1|\\nRichard|9|17|17||13|1|\\nChuul 2|6|76|76||16|0|Chuul\\n", "rolls": [{"id": 1, "title": "Aboleth", "label": "Tentacle", "expr": "1d20+9", "total": 21, "detail": "[12]+9", "nat": 12, "crit": false}, {"id": 2, "title": "Brakka", "label": "Greataxe attack", "expr": "1d20+5", "total": 25, "detail": "[20]+5", "nat": 20, "crit": true}, {"id": 3, "title": "Brakka", "label": "Greataxe damage", "expr": "2d12+3", "total": 19, "detail": "[9,7]+3", "nat": 0, "crit": false}, {"id": 4, "title": "Mirela", "label": "Fireball", "expr": "8d6", "total": 31, "detail": "[6,3,5,2,4,6,1,4]", "nat": 0, "crit": false}]}', "application/json")
        self.send(404, "")
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(("127.0.0.1", 8766), H).serve_forever()
