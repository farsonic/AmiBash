#!/usr/bin/env python3
"""Drive build/amibash interactively through a pty: pty_drive.py 'keys1' 'keys2' ...
Key strings support \\x escapes (e.g. '\\x1b[A' = Up, '\\t' = Tab, '\\r' = Enter)."""
import os, pty, sys, time, select
pid, fd = pty.fork()
if pid == 0:
    os.environ['PS1'] = '$ '
    os.environ['HOME'] = '/tmp/ptyhome'
    os.makedirs('/tmp/ptyhome', exist_ok=True)
    os.execv(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'amibash'), ['bash', '--norc', '-i'])
out = b''
def drain(t=0.4):
    global out
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 4096)
            except OSError: return
drain(0.5)
for k in sys.argv[1:]:
    os.write(fd, k.encode().decode('unicode_escape').encode('latin1'))
    drain(0.5)
os.write(fd, b'exit\r'); drain(0.5)
sys.stdout.write(out.decode('latin1').replace('\x1b', '<ESC>'))
