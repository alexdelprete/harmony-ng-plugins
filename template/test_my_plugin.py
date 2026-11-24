#!/usr/bin/env python3
"""Host test of the template: a fake device (HTTP on 127.0.0.1:18081) and the Hub's datagrams.
Copy it with the template; scripts/fake_ws.py has the Plugin helper and a fake WebSocket TV.
Usage: test_my_plugin.py <binary> <work dir>"""
import http.server, os, queue, sys, threading
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
from fake_ws import Plugin, check, failures  # noqa: E402

BIN, WORK = sys.argv[1], sys.argv[2]
keys = queue.Queue()


class Device(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        keys.put(self.path)
        self.send_response(200)
        self.send_header('Content-Length', '0')
        self.end_headers()

    do_GET = do_POST

    def log_message(self, *a):
        pass


def key(timeout=1.0):
    try:
        return keys.get(timeout=timeout)
    except queue.Empty:
        return None


srv = http.server.ThreadingHTTPServer(('127.0.0.1', 18081), Device)
threading.Thread(target=srv.serve_forever, daemon=True).start()
p = Plugin(BIN, WORK, 'my-plugin')
cfg = {'host': '127.0.0.1', 'port': 18081}
try:
    p.send('Home', config=cfg)
    check('tap Home -> POST /key/Home', key() == '/key/Home')
    for a in ('press', 'repeat', 'release'):
        p.send('Home', a, config=cfg)
    check('held Home -> once', key() == '/key/Home' and key(0.4) is None)
    for a in ('press', 'repeat', 'repeat', 'release'):
        p.send('VolumeUp', a, config=cfg)
    check('held VolumeUp -> on press and each repeat', [key() for _ in range(3)] == ['/key/VolumeUp'] * 3)
    p.clear_status()
    p.send('pair', 'start_pair', host='127.0.0.1', port=18081)
    st = p.status()
    check('pairing: device found', st and st['ok'] and st['stage'] == 'done')
    key()  # the pairing's GET /
finally:
    rc = p.stop()
check('stops on SIGTERM', rc == 0)
print('template: %s' % ('all passed' if not failures else '%d FAILED' % len(failures)))
sys.exit(1 if failures else 0)
