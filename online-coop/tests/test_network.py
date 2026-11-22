import os
from pathlib import Path
import socket
import struct
import subprocess
import time
import unittest

PEER = str(Path(os.environ.get('COOP_PEER', '../build/online-coop/coop-peer')).resolve())
IDENTITY = b''.join(s.ljust(64, b'\0') for s in (b'0.0.1', b'standalone', b'none'))


def frame(kind, body=b'', version=1):
    return struct.pack('!4sHHI', b'XCXC', version, kind, len(body)) + body


def exact(sock, count):
    data = b''
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise EOFError('peer closed')
        data += part
    return data


class Networking(unittest.TestCase):
    def setUp(self):
        self.children = []
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            self.port = s.getsockname()[1]

    def tearDown(self):
        for p in self.children:
            if p.poll() is None:
                p.terminate()
            p.communicate(timeout=4)

    def peer(self, mode, seconds=10, count=0):
        p = subprocess.Popen([PEER, mode, str(self.port), str(seconds), str(count)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.children.append(p)
        return p

    def host(self, seconds=10):
        p = self.peer('host', seconds=seconds)
        # Probe through the socket rather than block indefinitely on a log read.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                s = self.connect()
                s.close()
                return p
            except ConnectionRefusedError:
                if p.poll() is not None:
                    self.fail(p.communicate()[0])
                time.sleep(.01)
        self.fail('host did not listen')

    def connect(self):
        return socket.create_connection(('127.0.0.1', self.port), timeout=2)

    def hello(self, s):
        s.sendall(frame(1, IDENTITY))
        self.assertEqual(exact(s, 204), frame(2, IDENTITY))

    def test_10000_messages_and_clean_disconnect(self):
        host = self.host()
        client = self.peer('client', count=10000)
        output = client.communicate(timeout=12)[0]
        self.assertEqual(client.returncode, 0, output)
        self.assertIn('received=10000', output)
        time.sleep(.05)
        with self.connect() as s:
            self.hello(s)
            s.sendall(frame(5))
        self.assertIsNone(host.poll())

    def test_fragmented_and_coalesced_frames(self):
        self.host()
        with self.connect() as s:
            data = frame(1, IDENTITY)
            for byte in data:
                s.sendall(bytes([byte]))
            self.assertEqual(exact(s, 204), frame(2, IDENTITY))
            data = b''.join(frame(3, struct.pack('!I', i)) for i in range(100))
            s.sendall(data)
            expected = b''.join(frame(4, struct.pack('!I', i)) for i in range(100))
            self.assertEqual(exact(s, len(expected)), expected)

    def test_reject_malformed_and_incompatible_then_recover(self):
        self.host()
        bad = [frame(1, IDENTITY, version=2), frame(1, b'x' * 192),
               struct.pack('!4sHHI', b'XCXC', 1, 1, 0xffffffff),
               frame(9), frame(3, b'1234'), b'WRONG HEADER']
        for data in bad:
            with self.connect() as s:
                s.sendall(data)
                try:
                    self.assertEqual(s.recv(1), b'')
                except ConnectionResetError:
                    pass
        with self.connect() as s:
            self.hello(s)
            s.sendall(frame(1, IDENTITY))
            self.assertEqual(s.recv(1), b'')

    def test_unexpected_client_termination_and_host_exit(self):
        self.host()
        client = self.peer('client')
        time.sleep(.15)
        client.kill()
        client.communicate(timeout=2)
        with self.connect() as s:
            self.hello(s)
        # A standalone host exits through coop_stop after its configured duration.
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            self.port = reserve.getsockname()[1]
        host = self.host(seconds=1)
        client = self.peer('client', seconds=2, count=0)
        output = client.communicate(timeout=4)[0]
        self.assertEqual(host.wait(timeout=3), 0)
        self.assertIn('peer disconnected', output)

    def test_off_and_bind_conflict(self):
        host = self.host()
        off = self.peer('off', seconds=1)
        self.assertEqual(off.wait(timeout=3), 0)
        conflict = self.peer('host', seconds=1)
        output = conflict.communicate(timeout=3)[0]
        self.assertNotEqual(conflict.returncode, 0, output)
        self.assertIsNone(host.poll())

    def test_partial_handshake_timeout(self):
        self.host()
        with self.connect() as s:
            s.settimeout(7)
            s.sendall(b'X')
            self.assertEqual(s.recv(1), b'')

    @unittest.skipUnless(Path('/proc/self/fdinfo').is_dir() and hasattr(os, 'O_CLOEXEC'),
                         'Linux descriptor flags required')
    def test_session_sockets_close_on_exec(self):
        host = self.host()
        with self.connect() as peer:
            self.hello(peer)
            sockets = 0
            for fd in Path(f'/proc/{host.pid}/fd').iterdir():
                try:
                    if not os.readlink(fd).startswith('socket:'):
                        continue
                    info = Path(f'/proc/{host.pid}/fdinfo/{fd.name}').read_text()
                except FileNotFoundError:
                    continue
                flags = next(line.split()[1] for line in info.splitlines()
                             if line.startswith('flags:'))
                self.assertTrue(int(flags, 8) & os.O_CLOEXEC, f'socket {fd.name} is inheritable')
                sockets += 1
            self.assertEqual(sockets, 2, 'expected listener and accepted peer')

    def test_refused_connection(self):
        client = self.peer('client', seconds=1, count=1)
        output = client.communicate(timeout=3)[0]
        self.assertNotEqual(client.returncode, 0, output)


if __name__ == '__main__':
    unittest.main()
