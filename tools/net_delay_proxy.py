"""A localhost TCP delay proxy for testing the multiplayer session under network conditions of two PCs.

  python tools/net_delay_proxy.py --listen 47625 --target 127.0.0.1:47624 [--delay 120] [--jitter 60] [--kbps 0]
                                  [--seed 1] [--log <file>] [--stats 30]

The client joins the proxy's port (`join <name> 127.0.0.1 47625`) instead of the host's (47624). Every chunk read from
one side is written to the other after `delay` ms plus a random jitter in [-jitter, +jitter] ms, in order (a TCP stream
never reorders: a chunk never leaves before the one ahead of it), and, with --kbps, no faster than that many kilobits
per second per direction (the battle files of a live battle are about 2 MB). Both directions are delayed alike, so a
request and its answer take twice the delay. It logs each connection, a statistics line (chunks, bytes, the largest
extra wait the bandwidth cap added, per direction) every --stats seconds while bytes flow and at each connection's
close; with --log also into that file. Ctrl+C or the parent's kill stops it. Test only: it listens on 127.0.0.1.
"""
import argparse
import random
import socket
import sys
import threading
import time


class Pipe(threading.Thread):
    """One direction of one connection: reads chunks, releases them in order at their due time."""

    def __init__(self, name, src, dst, a, rng, stats):
        super().__init__(daemon=True)
        self.name, self.src, self.dst, self.a, self.rng, self.stats = name, src, dst, a, rng, stats
        self.queue, self.cv, self.closed = [], threading.Condition(), False
        self.last_due = 0.0
        self.sender = threading.Thread(target=self.send_loop, daemon=True)

    def run(self):
        self.sender.start()
        try:
            while True:
                data = self.src.recv(65536)
                if not data:
                    break
                now = time.monotonic()
                jitter = self.rng.uniform(-self.a.jitter, self.a.jitter) if self.a.jitter else 0.0
                due = now + max(0.0, self.a.delay + jitter) / 1000.0
                if self.a.kbps > 0:  # the line's capacity: the chunk's bits after the previous chunk's
                    bw_due = self.last_due + len(data) * 8 / (self.a.kbps * 1000.0)
                    if bw_due > due:
                        self.stats['bw_wait'] = max(self.stats['bw_wait'], bw_due - due)
                        due = bw_due
                due = max(due, self.last_due)  # in order
                self.last_due = due
                with self.cv:
                    self.queue.append((due, data))
                    self.cv.notify()
                self.stats['chunks'] += 1
                self.stats['bytes'] += len(data)
        except OSError:
            pass
        with self.cv:
            self.closed = True
            self.cv.notify()
        self.sender.join(30)
        try:
            self.dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass

    def send_loop(self):
        while True:
            with self.cv:
                while not self.queue and not self.closed:
                    self.cv.wait()
                if not self.queue and self.closed:
                    return
                due, data = self.queue[0]
            wait = due - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            try:
                self.dst.sendall(data)
            except OSError:
                return
            with self.cv:
                self.queue.pop(0)


def log(a, *parts):
    line = time.strftime('%H:%M:%S ') + ' '.join(str(p) for p in parts)
    print(line, flush=True)
    if a.log:
        with open(a.log, 'a', encoding='utf-8') as f:
            f.write(line + '\n')


def serve(a):
    host, port = a.target.rsplit(':', 1)
    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(('127.0.0.1', a.listen))
    ls.listen(8)
    rng = random.Random(a.seed)
    log(a, f'proxy 127.0.0.1:{a.listen} -> {a.target}: delay {a.delay} ms, jitter +-{a.jitter} ms, '
           f'bandwidth {str(a.kbps) + " kbit/s" if a.kbps else "unlimited"} per direction')
    n = 0
    while True:
        c, addr = ls.accept()
        n += 1
        try:
            s = socket.create_connection((host, int(port)), timeout=10)
            s.settimeout(None)
        except OSError as e:
            log(a, f'connection {n}: the target refused ({e})')
            c.close()
            continue
        for sock in (c, s):
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        st = {d: dict(chunks=0, bytes=0, bw_wait=0.0) for d in ('up', 'down')}
        up = Pipe(f'{n} up', c, s, a, rng, st['up'])
        down = Pipe(f'{n} down', s, c, a, rng, st['down'])
        up.start()
        down.start()
        log(a, f'connection {n} from {addr[0]}:{addr[1]}')

        def stats_line(n, st):
            return (f'up {st["up"]["chunks"]} chunks {st["up"]["bytes"]} bytes (bandwidth wait max '
                    f'{st["up"]["bw_wait"]:.2f} s), down {st["down"]["chunks"]} chunks {st["down"]["bytes"]} bytes (max '
                    f'{st["down"]["bw_wait"]:.2f} s)')

        def done(n=n, up=up, down=down, st=st, c=c, s=s):
            last = None
            while up.is_alive() or down.is_alive():  # (a statistics line while the bytes change: the test kills it)
                up.join(a.stats if a.stats > 0 else None)
                now = stats_line(n, st)
                if a.stats > 0 and now != last and (up.is_alive() or down.is_alive()):
                    log(a, f'connection {n}: {now}')
                    last = now
            down.join()
            for sock in (c, s):
                try:
                    sock.close()
                except OSError:
                    pass
            log(a, f'connection {n} closed: {stats_line(n, st)}')

        threading.Thread(target=done, daemon=True).start()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--listen', type=int, required=True, help='the port the client joins (127.0.0.1)')
    ap.add_argument('--target', required=True, help='host:port of the session host')
    ap.add_argument('--delay', type=float, default=120, help='one-way delay in ms (default 120)')
    ap.add_argument('--jitter', type=float, default=0, help='+- jitter in ms (default 0)')
    ap.add_argument('--kbps', type=float, default=0, help='bandwidth cap per direction in kbit/s (0: none)')
    ap.add_argument('--seed', type=int, default=1, help='the jitter\'s random seed')
    ap.add_argument('--log', default='', help='also append the log lines to this file')
    ap.add_argument('--stats', type=float, default=30, help='a statistics line every N s while bytes flow (0: only at '
                                                           'the close)')
    a = ap.parse_args()
    try:
        serve(a)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
