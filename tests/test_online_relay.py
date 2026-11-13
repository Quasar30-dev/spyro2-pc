import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("online_relay", Path(__file__).parents[1] / "tools" / "online_relay.py")
relay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(relay)
ROOM = "a" * 32


def packet(client=0, sequence=1, create=1, state=None):
    if state is None:
        state = f"0 {sequence} 17 0 0 0 0 1 2 3 0 0 0 2 1.5 0 0 0 0 0"
    return f"S2NET 2 {create} {ROOM} {client:032x}\n{state}\n".encode()


class RelayTests(unittest.TestCase):
    def test_supercharge_wing_variants(self):
        for appearance in (0, 1, 2):
            state = f"0 1 17 0 0 0 0 1 2 3 0 0 0 61 3.5 {appearance} 0 0 0 0"
            status, body = relay.Relay().poll(packet(state=state), 1)
            self.assertEqual(status, 200)
            self.assertEqual(int(body.splitlines()[-1].split()[15]), appearance)
        for appearance in (-1, 3):
            state = f"0 1 17 0 0 0 0 1 2 3 0 0 0 61 3.5 {appearance} 0 0 0 0"
            self.assertEqual(relay.Relay().poll(packet(state=state), 1)[0], 400)

    def test_dark_form_and_element(self):
        for form in (0, 1):
            for element in range(5):
                state = f"0 1 17 0 0 0 0 1 2 3 0 0 0 61 3.5 0 0 0 {form} {element}"
                status, body = relay.Relay().poll(packet(state=state), 1)
                self.assertEqual(status, 200)
                fields = body.splitlines()[-1].split()
                self.assertEqual((int(fields[18]), int(fields[19])), (form, element))
        for form, element in ((2, 0), (-1, 0), (0, 5), (0, -1)):
            state = f"0 1 17 0 0 0 0 1 2 3 0 0 0 61 3.5 0 0 0 {form} {element}"
            self.assertEqual(relay.Relay().poll(packet(state=state), 1)[0], 400)
        old = f"S2NET 1 1 {ROOM} {0:032x}\n0 1 17 0 0 0 0 1 2 3 0 0 0 2 1.5 0 0 0\n".encode()
        self.assertEqual(relay.Relay().poll(old, 1)[0], 400)

    def test_room_isolation_and_authority(self):
        server = relay.Relay()
        self.assertEqual(server.poll(packet(create=0), 1)[0], 404)
        self.assertEqual(server.poll(packet(), 1)[0], 200)
        self.assertEqual(server.poll(packet(client=1), 2)[0], 409)
        status, body = server.poll(packet(client=1, create=0), 2)
        self.assertEqual(status, 200)
        self.assertEqual(body.splitlines()[0], b"S2NET 2 1 0 2")
        self.assertEqual(server.poll(packet(client=1, create=0).replace(ROOM.encode(), b"b" * 32), 3)[0], 404)

    def test_capacity_replay_rate_and_expiry(self):
        server = relay.Relay()
        for i in range(4):
            self.assertEqual(server.poll(packet(client=i, create=int(i == 0)), 1 + i)[0], 200)
        self.assertEqual(server.poll(packet(client=4, create=0), 5)[0], 409)
        self.assertEqual(server.poll(packet(sequence=1), 6)[0], 400)
        self.assertEqual(server.poll(packet(sequence=2), 6)[0], 200)
        self.assertEqual(server.poll(packet(sequence=3), 6.001)[0], 429)
        self.assertEqual(server.poll(packet(client=1, sequence=2, create=0), 20)[0], 404)

    def test_reused_peer_has_new_revision(self):
        server = relay.Relay()
        server.poll(packet(), 1)
        server.poll(packet(client=1, create=0), 2)
        server.poll(packet(sequence=2), 10)
        status, body = server.poll(packet(client=2, create=0), 15)
        self.assertEqual(status, 200)
        fields = body.splitlines()[-1].split()
        self.assertEqual(fields[0], b"1")
        self.assertGreater(int(fields[1]), 2)

    def test_malformed_packets(self):
        for state in ("", "0 1 17 0 0 0 0 nan 2 3 0 0 0 2 1.5 0 0 0 0 0", "0 1 17 0 0 0 0 1 2 3 0 0 0 999 1.5 0 0 0 0 0", "0 1 17 0 0 0 1 1 2 3 0 0 0 2 1.5 0 0 0 0 0", "0 1 17 0 0 0 0 1 2 3 0 0 0 2 1.5 0 0 0"):
            self.assertEqual(relay.Relay().poll(packet(state=state), 1)[0], 400)
        self.assertEqual(relay.Relay().poll(b"\xff", 1)[0], 400)


if __name__ == "__main__":
    unittest.main()
