import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from spyrodisc import Disc, FormatError, SYNC, exe_header, wad_index


def both32(buffer, at, value):
    struct.pack_into("<I", buffer, at, value)
    struct.pack_into(">I", buffer, at + 4, value)


def record(name, lba, size, flags=0):
    length = 33 + len(name) + (len(name) % 2 == 0)
    result = bytearray(length)
    result[0] = length
    both32(result, 2, lba)
    both32(result, 10, size)
    result[25] = flags
    result[28:32] = b"\x01\x00\x00\x01"
    result[32] = len(name)
    result[33:33 + len(name)] = name
    return result


def fixture_iso():
    image = bytearray(27 * 2048)
    pvd = bytearray(2048)
    pvd[:7] = b"\x01CD001\x01"
    pvd[128:132] = b"\x00\x08\x08\x00"
    pvd[156:190] = record(b"\x00", 20, 2048, 2)
    image[16 * 2048:17 * 2048] = pvd
    directory = b"".join((record(b"\x00",20,2048,2),record(b"\x01",20,2048,2),
                          record(b"WAD.WAD;1",21,64),record(b"SCUS_944.25;1",22,4096)))
    image[20*2048:20*2048+len(directory)] = directory
    wad = bytearray(64)
    struct.pack_into("<6I",wad,0,24,16,40,0,40,24)
    wad[24:] = bytes(range(40))
    image[21*2048:21*2048+64] = wad
    exe = bytearray(4096)
    exe[:8] = b"PS-X EXE"
    struct.pack_into("<I",exe,16,0x80010000)
    struct.pack_into("<II",exe,24,0x80010000,2048)
    struct.pack_into("<I",exe,48,0x801ffff0)
    image[22*2048:24*2048] = exe
    return bytes(image),bytes(wad),bytes(exe)


def raw_image(iso, mode=2):
    result = bytearray()
    for at in range(0,len(iso),2048):
        sector = bytearray(2352)
        sector[:12] = SYNC
        sector[15] = mode
        offset = 24 if mode == 2 else 16
        if mode == 2:
            sector[16:24] = b"\x00\x00\x08\x00" * 2
        sector[offset:offset+2048] = iso[at:at+2048]
        result += sector
    return bytes(result)


class DiscTests(unittest.TestCase):
    def test_all_representations_and_cross_sector_native_reads(self):
        iso, wad, exe = fixture_iso()
        with tempfile.TemporaryDirectory(prefix="spyro_disc_") as name:
            root = Path(name)
            (root/"game.iso").write_bytes(iso)
            (root/"game's.bin").write_bytes(raw_image(iso))
            (root/"mode1.bin").write_bytes(raw_image(iso,1))
            (root/"game.cue").write_text('FILE "game\'s.bin" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n')
            extracted = root/"extracted"
            extracted.mkdir()
            (extracted/"wad.wad").write_bytes(wad)
            (extracted/"scus_944.25").write_bytes(exe)
            for file in ("game.iso","game's.bin","mode1.bin","game.cue"):
                with self.subTest(file=file), Disc(root/file) as disc:
                    entries = {e.path:e for e in disc.entries()}
                    self.assertEqual(len(entries),2)
                    self.assertEqual(disc.read(entries['WAD.WAD'].lba,64),wad)
                    self.assertEqual(disc.read(entries['SCUS_944.25'].lba,4096),exe)
            native = os.environ.get("SPYRO_INSPECT")
            if native:
                for file in ("game.iso","game's.bin","mode1.bin","game.cue","extracted"):
                    with self.subTest(native=file):
                        run = subprocess.run([native,str(root/file)],text=True,capture_output=True)
                        self.assertEqual(run.returncode,0,run.stderr)
                        self.assertIn("WAD slots: 3  zero length: 1",run.stdout)

    def test_bad_raw_subheader_and_form2(self):
        iso,_,_ = fixture_iso()
        for form2 in (False,True):
            raw = bytearray(raw_image(iso))
            raw[16*2352+18] = 0x28
            if form2: raw[16*2352+22] = 0x28
            with tempfile.TemporaryDirectory(prefix="spyro_disc_") as name:
                path=Path(name)/"bad.bin";path.write_bytes(raw)
                with Disc(path) as disc, self.assertRaises(FormatError):disc.entries()

    def test_invalid_iso_path_and_endian_copies(self):
        iso,_,_ = fixture_iso()
        for malicious in (False,True):
            data=bytearray(iso)
            if malicious:
                unsafe=record(b"../oops",21,64)
                data[20*2048:20*2048+len(unsafe)]=unsafe
            else: data[16*2048+162]^=1
            with tempfile.TemporaryDirectory(prefix="spyro_disc_") as name:
                path=Path(name)/"bad.iso";path.write_bytes(data)
                with Disc(path) as disc, self.assertRaises(FormatError):disc.entries()

    def test_archive_slots_and_nested_metadata(self):
        header=bytearray(64)
        struct.pack_into("<6I",header,0,32,8,40,0,40,24)
        self.assertEqual([e.index for e in wad_index(header,64)],[0,1,2])
        struct.pack_into("<II",header,24,0,0)
        # Metadata after the terminator is allowed before the first data extent.
        struct.pack_into("<I",header,28,0)
        self.assertEqual(len(wad_index(header,64)),3)
        struct.pack_into("<I",header,16,36)
        with self.assertRaises(FormatError):wad_index(header,64)

    def test_executable_bounds(self):
        _,_,exe=fixture_iso()
        self.assertEqual(exe_header(exe)['entry_pc'],0x80010000)
        data=bytearray(exe);struct.pack_into("<I",data,28,8192)
        with self.assertRaises(FormatError):exe_header(data)


if __name__ == "__main__":unittest.main()
