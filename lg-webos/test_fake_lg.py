#!/usr/bin/env python3
"""lg-webos against a fake webOS TV: wss on 127.0.0.1:3001 (register with prompt and client
key, signed manifest refused by "new firmware", pointer socket). Usage: test_fake_lg.py <bin> <work>"""
import json, os, queue, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
from fake_ws import FakeWsServer, Plugin, check, failures, wait_for  # noqa: E402

BIN, WORK = sys.argv[1], sys.argv[2]
KEY = 'c0ffee-key-1234'


class TV:
    reject_signed = True   # newer firmware: the signed manifest is refused
    deny = False
    pending = []
    registers = []         # register payloads seen
    reqs = queue.Queue()   # (uri, payload) of SSAP requests
    buttons = queue.Queue()

    @staticmethod
    def get(q, timeout=2.0):
        try:
            return q.get(timeout=timeout)
        except queue.Empty:
            return None


def on_text(c, text):
    if c.path.endswith('netinput.pointer.sock'):
        lines = dict(l.split(':', 1) for l in text.strip().split('\n') if ':' in l)
        TV.buttons.put(lines.get('name'))
        return
    m = json.loads(text)
    if m['type'] == 'register':
        pl = m['payload']
        TV.registers.append(pl)
        if TV.reject_signed and 'signatures' in pl['manifest']:
            c.send({'type': 'error', 'id': m['id'], 'error': '403 signature rejected'})
        elif pl.get('client-key') == KEY:
            c.send({'type': 'registered', 'id': m['id'], 'payload': {'client-key': KEY}})
        elif TV.deny:
            c.send({'type': 'error', 'id': m['id'], 'error': '403 cancelled'})
        else:
            c.send({'type': 'response', 'id': m['id'], 'payload': {'pairingType': 'PROMPT', 'returnValue': True}})
            TV.pending.append((c, m['id']))
    elif m['type'] == 'request':
        uri = m['uri']
        if uri.endswith('getPointerInputSocket'):
            c.send({'type': 'response', 'id': m['id'], 'payload': {
                'returnValue': True, 'socketPath': 'wss://192.168.99.99:3001/resources/abc/netinput.pointer.sock'}})
        else:
            TV.reqs.put((uri, m.get('payload')))
            c.send({'type': 'response', 'id': m['id'], 'payload': {'returnValue': True}})


def accept():
    for c, i in TV.pending:
        c.send({'type': 'registered', 'id': i, 'payload': {'client-key': KEY}})
    TV.pending.clear()


srv = FakeWsServer(3001, WORK, tls=True, on_text=on_text)
p = Plugin(BIN, WORK, 'lg-webos')
cfg = {'host': '127.0.0.1', 'mac': '00:11:22:33:44:55'}

try:
    p.clear_status()
    p.send('pair', 'start_pair', host='127.0.0.1', timeout_s=20)
    wait_for(lambda: TV.pending, 4)
    check('register: signed manifest refused, unsigned sent',
          len(TV.registers) == 2 and 'signatures' in TV.registers[0]['manifest']
          and 'signatures' not in TV.registers[1]['manifest']
          and 'CONTROL_INPUT_TEXT' in TV.registers[1]['manifest']['permissions'])
    check('register: no client key before pairing', 'client-key' not in TV.registers[1])
    check('pairing: no answer before Accept', p.status(0.3) is None)
    accept()
    st = p.status()
    check('pairing: done after Accept', st and st['ok'] and st['stage'] == 'done')
    check('pairing: client key saved', open(os.path.join(p.data, 'key-127.0.0.1')).read().strip() == KEY)

    p.send('Home', config=cfg)
    check('Home -> pointer button HOME (pointer socket on the TV address)', TV.get(TV.buttons) == 'HOME')
    p.send('Select', 'press', config=cfg)
    p.send('Select', 'repeat', config=cfg)
    p.send('Select', 'release', config=cfg)
    check('held Select -> one ENTER', TV.get(TV.buttons) == 'ENTER' and TV.get(TV.buttons, 0.5) is None)
    for a in ('press', 'repeat', 'repeat', 'release'):
        p.send('VolumeUp', a, config=cfg)
    got = [TV.get(TV.buttons) for _ in range(3)]
    check('held VolumeUp -> VOLUMEUP on press and each repeat', got == ['VOLUMEUP'] * 3)

    p.send('Volume', value=27, config=cfg)
    check('Volume 27 -> audio/setVolume', TV.get(TV.reqs) == ('ssap://audio/setVolume', {'volume': 27}))
    p.send('HDMI 2', config=cfg)
    check('HDMI 2 -> tv/switchInput HDMI_2', TV.get(TV.reqs) == ('ssap://tv/switchInput', {'inputId': 'HDMI_2'}))
    p.send('app:youtube', 'press', config=cfg)
    p.send('app:youtube', 'release', config=cfg)
    check('app:youtube -> launch youtube.leanback.v4 once',
          TV.get(TV.reqs) == ('ssap://system.launcher/launch', {'id': 'youtube.leanback.v4'}) and TV.get(TV.reqs, 0.5) is None)
    p.send('text:a "b"', config=cfg)
    check('text -> ime/insertText', TV.get(TV.reqs) == ('ssap://com.webos.service.ime/insertText', {'text': 'a "b"', 'replace': 0}))
    p.send('ssap:audio/setMute {"mute":true}', config=cfg)
    check('ssap: passthrough with payload', TV.get(TV.reqs) == ('ssap://audio/setMute', {'mute': True}))
    p.send('PowerOff', config=cfg)
    check('PowerOff -> system/turnOff', TV.get(TV.reqs) == ('ssap://system/turnOff', None))

    p.send('Disconnect', config=cfg)
    check('Disconnect closes both sockets', wait_for(lambda: not srv.open_conns()))
    n = len(TV.registers)
    p.send('Connect', config=cfg)
    check('Connect registers with the saved key, no prompt',
          wait_for(lambda: len(srv.open_conns()) >= 1) and TV.registers[-1].get('client-key') == KEY
          and not TV.pending and len(TV.registers) == n + 1)

    for c in srv.open_conns():
        c.close()
    time.sleep(0.3)
    p.send('Back', config=cfg)
    check('TV closed the connections -> next button reconnects', TV.get(TV.buttons, 4) == 'BACK')

    off = dict(cfg, host='127.0.0.2')
    p.send('PowerOn', config=off)
    check('PowerOn on a TV off the network -> Wake-on-LAN',
          wait_for(lambda: p.logged('Wake-on-LAN sent to 00:11:22:33:44:55'), 8))

    TV.deny = True
    os.unlink(os.path.join(p.data, 'key-127.0.0.1'))
    p.clear_status()
    p.send('pair', 'start_pair', host='127.0.0.1', timeout_s=20)
    st = p.status()
    check('pairing declined on the TV -> error', st and not st['ok'] and st['stage'] == 'error')
finally:
    rc = p.stop()
check('stops on SIGTERM', rc == 0)
print('lg-webos fake TV: %s' % ('all passed' if not failures else '%d FAILED' % len(failures)))
sys.exit(1 if failures else 0)
