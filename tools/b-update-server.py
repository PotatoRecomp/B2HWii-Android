"""Stand-in for GitHub's releases/latest, for testing the Android updater.

Usage: python -I tools/b-update-server.py DIR APK VERSION [--port 8765] [--bad-digest]
Serves DIR, with /latest.json describing APK (a file in DIR) as release vVERSION.
On the device: adb reverse tcp:8765 tcp:8765
               adb shell setprop debug.basara.update_url http://localhost:8765/latest.json
               adb shell setprop debug.basara.beta_url http://localhost:8765/beta.json   (Beta)
"""
import argparse
import functools
import hashlib
import http.server
import json
import os

parser = argparse.ArgumentParser()
parser.add_argument("dir")
parser.add_argument("apk")
parser.add_argument("version")
parser.add_argument("--port", type=int, default=8765)
parser.add_argument("--bad-digest", action="store_true", help="list a wrong SHA-256")
parser.add_argument("--beta-off", metavar="MESSAGE", help="serve beta.json switching the Beta off")
args = parser.parse_args()

beta = os.path.join(args.dir, "beta.json")
if args.beta_off:
    with open(beta, "w") as f:
        json.dump({"enabled": False, "message": args.beta_off}, f)
elif os.path.exists(beta):
    os.remove(beta)  # no beta.json: the Beta stays open

path = os.path.join(args.dir, args.apk)
digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
if args.bad_digest:
    digest = "0" * 64
release = {
    "tag_name": "v" + args.version,
    "assets": [{
        "name": args.apk,
        "size": os.path.getsize(path),
        "digest": "sha256:" + digest,
        "browser_download_url": f"http://localhost:{args.port}/{args.apk}",
    }],
}
with open(os.path.join(args.dir, "latest.json"), "w") as f:
    json.dump(release, f, indent=1)
print("latest.json:", json.dumps(release), flush=True)

handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=args.dir)
http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()
