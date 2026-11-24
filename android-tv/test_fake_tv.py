#!/usr/bin/env python3
"""End-to-end check of the android-tv plugin against a fake TV (TLS on 127.0.0.1:6466).

Build the plugin for the host with a short liveness timeout and a scratch data dir, then run:
  gcc -DATV_RX_STALE_MS=2500 -DATV_DATA_DIR='"<dir>"' -I<bearssl>/inc android_tv.c <host libbearssl.a> -o <bin>
  python3 test_fake_tv.py <bin> <dir>
Needs openssl on PATH (makes the fake TV's certificate).
"""
import os, socket, ssl, subprocess, sys, threading, time, json, queue

BIN, WORK = sys.argv[1], sys.argv[2]
os.makedirs(WORK, exist_ok=True)
SOCK = os.path.join(WORK, 'atv.sock')
CERT, KEY = os.path.join(WORK, 'tv.pem'), os.path.join(WORK, 'tv.key')
if not os.path.exists(CERT):
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', KEY, '-out', CERT,
                    '-days', '2', '-subj', '/CN=faketv'], check=True, capture_output=True)


def varint(n):
    out = b''
    while n >= 0x80:
        out += bytes([(n & 0x7F) | 0x80]); n >>= 7
    return out + bytes([n])


def framed(payload):
    return varint(len(payload)) + payload


def read_varint(f):
    n = shift = 0
    while True:
        b = f.read(1)
        if not b:
            raise EOFError
        n |= (b[0] & 0x7F) << shift
        if not b[0] & 0x80:
            return n
        shift += 7


CONFIGURE = framed(b'\x0a\x03\x08\xee\x04')
SET_ACTIVE = framed(b'\x12\x03\x08\xee\x04')
PING = framed(b'\x42\x02\x08\x07')
ERROR = framed(b'\x1a\x02\x08\x01')
VOLUME = framed(b'\xa2\x02' + bytes([60]) + b'\x0a' + bytes([58]) + b'x' * 58)  # unrelated message, 62+ bytes
BIG = framed(b'\xa2\x02\x83\x10' + b'y' * 2051)                             # larger than the plugin buffer


class FakeTV:
    """Accepts sessions, answers the handshake, records every message the plugin sends."""

    def __init__(self):
        self.msgs = queue.Queue()       # (session number, top field, payload)
        self.sessions = 0
        self.ended = 0                  # number of the last session the plugin closed
        self.conn = None
        self.silent = False             # stop pinging
        self.error_on_key = None        # keycode that makes the TV answer with RemoteError
        self.ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.ctx.load_cert_chain(CERT, KEY)
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(('127.0.0.1', 6466))
        self.srv.listen(4)
        threading.Thread(target=self.accept_loop, daemon=True).start()
        threading.Thread(target=self.ping_loop, daemon=True).start()

    def accept_loop(self):
        while True:
            raw, _ = self.srv.accept()
            try:
                conn = self.ctx.wrap_socket(raw, server_side=True)
            except ssl.SSLError:
                continue
            self.sessions += 1
            threading.Thread(target=self.serve, args=(conn, self.sessions), daemon=True).start()

    def serve(self, conn, n):
        f = conn.makefile('rb')
        try:
            conn.sendall(CONFIGURE)
            f.read(read_varint(f))          # client configure
            conn.sendall(SET_ACTIVE)
            f.read(read_varint(f))          # client set_active
            self.conn = conn
            while True:
                body = f.read(read_varint(f))
                field = body[0] >> 3 if body[0] < 0x80 else ((body[0] & 0x7F) | (body[1] << 7)) >> 3
                self.msgs.put((n, field, body))
                if field == 10 and self.error_on_key is not None and body[3] == self.error_on_key:
                    conn.sendall(ERROR)
        except (EOFError, OSError, ssl.SSLError, IndexError):
            pass
        self.ended = n

    def ping_loop(self):
        while True:
            time.sleep(1)
            c = self.conn
            if c and not self.silent:
                try:
                    c.sendall(PING)
                except OSError:
                    pass

    def next(self, timeout=3.0, want=None):
        """Next message (skipping ping responses unless asked for), or None"""
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = self.msgs.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                return None
            if want is None and m[1] == 9:
                continue
            if want is not None and m[1] != want:
                continue
            return m
        return None

    def conn_closed(self):
        return self.ended == self.sessions

    def drain(self):
        while not self.msgs.empty():
            self.msgs.get_nowait()


def mono_ms():
    return int(time.clock_gettime(time.CLOCK_MONOTONIC) * 1000)


def send(cmd, action='tap', ts=None):
    msg = {'plugin_id': 'android-tv', 'command': cmd, 'action': action,
           'ts': mono_ms() if ts is None else ts, 'config': {'host': '127.0.0.1'}}
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.sendto(json.dumps(msg).encode(), SOCK)
    s.close()


def key(m):
    """(keycode, direction) of a RemoteKeyInject message, None for anything else"""
    if m is None or m[1] != 10:
        return None
    b = m[2]
    return b[3], b[5]


tv = FakeTV()
log = open(os.path.join(WORK, 'plugin.log'), 'w')
plugin = subprocess.Popen([BIN, SOCK], stdout=log, stderr=subprocess.STDOUT)
failures = []


def check(name, cond):
    print(('  PASS ' if cond else '  FAIL ') + name)
    if not cond:
        failures.append(name)


try:
    for _ in range(600):                     # first start generates the client key
        if os.path.exists(SOCK):
            break
        time.sleep(0.1)
    time.sleep(0.3)

    send('Home')
    check('tap Home -> SHORT keycode 3', key(tv.next()) == (3, 3))

    send('app:netflix')
    m = tv.next()
    check('app:netflix -> https://www.netflix.com/title',
          m is not None and m[1] == 90 and b'https://www.netflix.com/title' in m[2])
    send('app:netflix', 'release')
    check('release of an app command launches nothing', tv.next(0.8) is None)

    send('NoSuchCommand')
    send('Back')
    check('unknown command is skipped, next command goes through', key(tv.next()) == (4, 3))

    send('VolumeUp', 'press')
    check('press VolumeUp -> START_LONG', key(tv.next()) == (24, 1))
    t0 = time.time()
    m = tv.next(2.0)
    check('lost release -> END_LONG sent by the plugin within 1 s', key(m) == (24, 2) and time.time() - t0 < 1.0)

    send('VolumeUp', 'repeat', ts=mono_ms() - 5000)
    check('repeat queued 5 s ago is dropped', tv.next(0.8) is None)

    # a ping in the same TLS record as another message: the plugin must still answer it
    tv.drain()
    tv.conn.sendall(VOLUME + PING)
    check('ping sharing a TLS record is answered', tv.next(1.5, want=9) is not None)

    tv.conn.sendall(BIG)
    time.sleep(0.3)
    sessions = tv.sessions
    send('Home')
    m = tv.next()
    check('message larger than the buffer is dropped, session kept', key(m) == (3, 3) and m[0] == sessions)

    tv.error_on_key = 20                      # DPAD_DOWN
    send('Down')
    key(tv.next())
    time.sleep(0.5)
    sessions = tv.sessions
    send('Up')
    m = tv.next()
    check('RemoteError -> new session, next command delivered', key(m) == (19, 3) and m[0] == sessions + 1)
    tv.error_on_key = None

    tv.silent = True                         # TV stops pinging: the session is stale after 2.5 s
    time.sleep(3.2)
    sessions = tv.sessions
    send('Left')
    m = tv.next()
    check('stale session -> reconnect before the send, command delivered', key(m) == (21, 3) and m[0] == sessions + 1)
    tv.silent = False

    # the plugin socket never blocks the sender: a burst while the plugin is busy is absorbed
    t0 = time.time()
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            s.setblocking(False)
            s.sendto(b'{"command":"Home","action":"repeat","ts":1}', SOCK)
            s.close()
        except BlockingIOError:
            pass
    check('burst of 50 sends returns at once', time.time() - t0 < 0.5)
    time.sleep(1.0)
    tv.drain()

    # Connect / Disconnect: the control session alone, no key sent
    send('Home')
    tv.next()
    send('Disconnect')
    time.sleep(0.5)
    check('Disconnect closes the session', tv.conn_closed())
    sessions = tv.sessions
    send('Connect')
    time.sleep(0.8)
    check('Connect opens a session and sends no key', tv.sessions == sessions + 1 and tv.next(0.5) is None)
    send('Connect', 'release')
    send('Connect')
    time.sleep(0.5)
    check('Connect while connected keeps the session', tv.sessions == sessions + 1)
    send('Home')
    m = tv.next()
    check('the next key uses the session Connect opened', key(m) == (3, 3) and m[0] == sessions + 1)
    send('PowerOn', 'press')
    send('PowerOn', 'release')
    m = tv.next()
    # keycode 224 is a two-byte varint (e0 01): key() reads one-byte codes only
    check('PowerOn on a TV that answers: one wake key (224)',
          m is not None and m[1] == 10 and m[2][2:7] == b'\x08\xe0\x01\x10\x03' and tv.next(0.5) is None)
    tv.drain()

    # another device answering on the TV's address with a different key is refused
    cert2, key2 = os.path.join(WORK, 'tv2.pem'), os.path.join(WORK, 'tv2.key')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', key2, '-out', cert2,
                    '-days', '2', '-subj', '/CN=impostor'], check=True, capture_output=True)
    ctx2 = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx2.load_cert_chain(cert2, key2)
    tv.ctx = ctx2
    tv.conn.shutdown(socket.SHUT_RDWR)
    time.sleep(0.5)
    send('Home')
    check('TV with a different key: no command delivered', tv.next(2.0) is None)
    log.flush()
    check('TV with a different key: refusal logged', 'different key' in open(os.path.join(WORK, 'plugin.log')).read())
finally:
    plugin.terminate()
    plugin.wait(5)
    log.close()

print('android-tv fake TV: %s' % ('all passed' if not failures else '%d FAILED' % len(failures)))
sys.exit(1 if failures else 0)
