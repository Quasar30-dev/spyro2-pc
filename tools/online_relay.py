"""Small HTTPS relay for the optional four-player exploration prototype.

Only fixed, validated player snapshots pass through it. No game assets or files
are transferred. Run behind HTTPS or supply --cert and --key directly.
"""
import argparse
import http.server
import ipaddress
import math
import re
import ssl
import threading
import time

TOKEN = re.compile(r"[0-9a-fA-F]{32}\Z")
MAX_ROOMS = 64
TTL = 12.0


def validate_state(line):
    fields = line.split()
    if len(fields) != 20:
        raise ValueError("snapshot length")
    values = [int(x) if i not in (7, 8, 9, 10, 11, 12, 14) else float(x) for i, x in enumerate(fields)]
    peer, seq, slot, universe, seed, world, space, x, y, z, yaw, pitch, roll, clip, frame, appearance, combo, combo_tick, form, element = values
    if not (0 <= peer < 4 and 0 <= seq < 2**64 and 15 <= slot <= 95 and slot % 2 and universe in (0, 1) and space in (0, 1) and (not space or universe)):
        raise ValueError("snapshot identity")
    if not (0 <= seed < 2**64 and 0 <= world < 2**64 and all(math.isfinite(v) for v in (x, y, z, yaw, pitch, roll, frame))):
        raise ValueError("snapshot finite values")
    if not (max(abs(x), abs(y), abs(z)) < 100000 and abs(yaw) < 1000 and abs(pitch) < 10 and abs(roll) < 10 and 0 <= clip < 100 and 0 <= frame < 512 and appearance in (0, 1, 2) and 0 <= combo < 3 and 0 <= combo_tick < 30 and form in (0, 1) and 0 <= element < 5):
        raise ValueError("snapshot range")
    return fields


class Relay:
    def __init__(self):
        self.rooms = {}
        self.lock = threading.Lock()

    def poll(self, body, now=None):
        now = time.monotonic() if now is None else now
        try:
            lines = body.decode("ascii").splitlines()
            if len(lines) != 2:
                raise ValueError("packet length")
            magic, version, create, room, client = lines[0].split()
            if magic != "S2NET" or version != "2" or create not in ("0", "1") or not TOKEN.fullmatch(room) or not TOKEN.fullmatch(client):
                raise ValueError("packet header")
            state = validate_state(lines[1])
            room, client = room.lower(), client.lower()
        except (ValueError, UnicodeError):
            return 400, b"Invalid packet\n"
        with self.lock:
            for code, entry in list(self.rooms.items()):
                if now - entry["players"][entry["host"]]["time"] > TTL:
                    del self.rooms[code]
                else:
                    entry["players"] = {key: p for key, p in entry["players"].items() if now - p["time"] <= TTL}
            if room not in self.rooms:
                if create != "1":
                    return 404, b"Room not found\n"
                if len(self.rooms) >= MAX_ROOMS:
                    return 429, b"Relay full\n"
                self.rooms[room] = {"host": client, "players": {}, "revision": 0}
            entry = self.rooms[room]
            if create == "1" and entry["host"] != client:
                return 409, b"Room occupied\n"
            players = entry["players"]
            if client not in players:
                if len(players) >= 4:
                    return 409, b"Room full\n"
                free = next(i for i in range(4) if i not in {p["id"] for p in players.values()})
                players[client] = {"id": free, "time": now - 1, "sequence": -1, "state": state}
            player = players[client]
            if now - player["time"] < 0.02:
                return 429, b"Too many updates\n"
            if int(state[1]) <= player["sequence"]:
                return 400, b"Out of order update\n"
            sequence = int(state[1])
            entry["revision"] += 1
            state[0], state[1] = str(player["id"]), str(entry["revision"])
            player.update(time=now, sequence=sequence, state=state)
            owner = players[entry["host"]]["id"]
            reply = f"S2NET 2 {player['id']} {owner} {len(players)}\n"
            reply += "".join(" ".join(p["state"]) + "\n" for p in sorted(players.values(), key=lambda p: p["id"]))
            return 200, reply.encode("ascii")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if self.path != "/v1/poll" or self.headers.get("Transfer-Encoding") or not 1 <= length <= 2048:
                self.send_error(400)
                return
            body = self.rfile.read(length)
            if len(body) != length:
                self.close_connection = True
                return
            status, response = self.server.relay.poll(body)
            self.send_response(status)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)
        except (OSError, ValueError):
            self.close_connection = True

    def do_GET(self):
        if self.path not in ("/", "/health"):
            self.send_error(404)
            return
        response = b"Spyro 2 PC exploration relay is ready. Connect through the workbench Online menu.\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, *_):
        pass  # Room credentials and player locations aren't written to logs.


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 64

    def __init__(self, address):
        self.relay = Relay()
        self.capacity = threading.BoundedSemaphore(128)
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.capacity.acquire(False):
            self.shutdown_request(request)
            return
        super().process_request(request, address)

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.capacity.release()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=27820)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    if bool(args.cert) != bool(args.key):
        parser.error("Supply both --cert and --key")
    if not ipaddress.ip_address(args.bind).is_loopback and not args.cert:
        parser.error("A public listener needs --cert and --key; a local HTTPS reverse proxy can use the default loopback listener")
    server = Server((args.bind, args.port))
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"Spyro exploration relay listening on {args.bind}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
