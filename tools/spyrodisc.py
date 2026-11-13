"""Read-only PlayStation disc import and bounded Insomniac WAD indexing.

Only Python's standard library is required. Extracted bytes belong in data/,
never in the source tree. Unknown WAD slots retain their original numeric IDs.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct

SECTOR = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class FormatError(ValueError):
    pass


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise FormatError(f"u32 out of range: {offset:#x}/{len(data):#x}")
    return struct.unpack_from("<I", data, offset)[0]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@dataclass(frozen=True)
class DiscEntry:
    path: str
    lba: int
    size: int
    flags: int


class Disc:
    def __init__(self, path: Path):
        path = Path(path)
        if path.suffix.lower() == ".cue":
            cue = path.read_text(encoding="utf-8-sig")
            tracks, current_file, current = [], None, None
            for line in cue.splitlines():
                if match := re.match(r'^\s*FILE\s+"(.+)"\s+BINARY\s*$', line, re.I):
                    current_file = match[1]
                elif match := re.match(r'^\s*TRACK\s+\d+\s+(\S+)\s*$', line, re.I):
                    if current_file is None:
                        raise FormatError("CUE track has no binary file")
                    current = dict(file=current_file, mode=match[1].upper(), start=None)
                    tracks.append(current)
                elif match := re.match(r'^\s*INDEX\s+0?1\s+(\d+:\d+:\d+)\s*$', line, re.I):
                    if current is None:
                        raise FormatError("CUE index has no track")
                    current['start'] = match[1]
            data = [track for track in tracks if track['mode'] != 'AUDIO']
            if len(data) != 1 or data[0]['mode'] not in ('MODE2/2352', 'MODE1/2352', 'MODE1/2048'):
                raise FormatError("Expected one supported data track, optionally followed by CD audio")
            if data[0]['start'] != '00:00:00':
                raise FormatError("Nonzero data track start is not implemented")
            path = path.parent / data[0]['file']
        self.path = path.resolve()
        self.stream = self.path.open("rb")
        self.size = self.path.stat().st_size
        prefix = self.stream.read(16)
        self.stride = 2352 if prefix[:12] == SYNC else 2048
        if self.size % self.stride:
            self.close()
            raise FormatError("Partial disc sector")
        self.sectors = self.size // self.stride

    def close(self):
        self.stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def sector(self, lba: int, *, raw: bool = False) -> bytes:
        if not 0 <= lba < self.sectors:
            raise FormatError(f"LBA outside disc: {lba}")
        self.stream.seek(lba * self.stride)
        data = self.stream.read(self.stride)
        if len(data) != self.stride:
            raise FormatError("Truncated sector")
        if raw or self.stride == 2048:
            return data
        if data[:12] != SYNC:
            raise FormatError(f"Bad sector sync at {lba}")
        if data[15] == 1:
            return data[16:2064]
        if data[15] != 2 or data[16:20] != data[20:24]:
            raise FormatError(f"Bad Mode 2 sector at {lba}")
        if data[18] & 0x20:
            raise FormatError(f"XA Form 2 at LBA {lba}; use raw extraction to preserve streaming data")
        return data[24:2072]

    def read(self, lba: int, size: int) -> bytes:
        if size < 0 or lba + (size + SECTOR - 1) // SECTOR > self.sectors:
            raise FormatError("Extent outside disc")
        return b"".join(self.sector(lba+i) for i in range((size+SECTOR-1)//SECTOR))[:size]

    @staticmethod
    def record(data: bytes, offset: int):
        length = data[offset]
        if length < 34 or offset + length > len(data):
            raise FormatError("Invalid ISO directory record")
        record = data[offset:offset+length]
        lba, size = u32(record, 2), u32(record, 10)
        if lba != struct.unpack_from(">I", record, 6)[0] or size != struct.unpack_from(">I", record, 14)[0]:
            raise FormatError("ISO endian copies disagree")
        if 33 + record[32] > length or record[26] or record[27] or record[25] & 0x80:
            raise FormatError("Unsupported ISO interleaving/multi-extent or malformed name")
        return length, lba, size, record[25], record[33:33+record[32]]

    def entries(self) -> list[DiscEntry]:
        pvd = self.sector(16)
        if pvd[:7] != b"\x01CD001\x01" or struct.unpack_from("<H", pvd, 128)[0] != SECTOR:
            raise FormatError("Expected ISO9660 primary volume and 2048-byte logical blocks")
        _, root_lba, root_size, _, _ = self.record(pvd, 156)
        result: list[DiscEntry] = []
        visited: set[int] = set()

        def walk(lba: int, size: int, parent: str, depth: int):
            if depth > 16 or lba in visited or size > 16*1024*1024:
                raise FormatError("Invalid/cyclic directory tree")
            visited.add(lba)
            data, offset = self.read(lba, size), 0
            while offset < size:
                if data[offset] == 0:
                    offset = (offset // SECTOR + 1) * SECTOR
                    continue
                length, child_lba, child_size, flags, name = self.record(data, offset)
                offset += length
                if name in (b"\x00", b"\x01"):
                    continue
                clean = name.decode("ascii").split(";")[0]
                if clean in (".", "..", "") or any(c in clean for c in '/\\:'):
                    raise FormatError("Unsafe ISO path")
                child_path = parent + clean
                if child_lba + (child_size + SECTOR - 1)//SECTOR > self.sectors:
                    raise FormatError("File extent outside disc")
                if flags & 2:
                    walk(child_lba, child_size, child_path + "/", depth + 1)
                else:
                    result.append(DiscEntry(child_path, child_lba, child_size, flags))
        walk(root_lba, root_size, "", 0)
        return sorted(result, key=lambda e: e.path)

    def extract(self, entry: DiscEntry, output: Path, *, raw: bool = False):
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_suffix(output.suffix + ".partial")
        try:
            with temporary.open("wb") as dest:
                remaining = entry.size
                for i in range((remaining + SECTOR - 1) // SECTOR):
                    data = self.sector(entry.lba + i, raw=raw)
                    dest.write(data if raw else data[:min(remaining, SECTOR)])
                    remaining -= min(remaining, SECTOR)
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)


def exe_header(data: bytes) -> dict:
    if len(data) < 0x800 or data[:8] != b"PS-X EXE":
        raise FormatError("Not a PS-X EXE")
    fields = {name: u32(data, offset) for name, offset in (
        ("entry_pc", 0x10), ("initial_gp", 0x14), ("load_address", 0x18),
        ("load_size", 0x1c), ("bss_address", 0x28), ("bss_size", 0x2c),
        ("stack_base", 0x30), ("stack_offset", 0x34))}
    if fields["load_size"] > len(data) - 0x800 or fields["load_address"] + fields["load_size"] > 0x80200000:
        raise FormatError("PS-X EXE payload outside file/RAM")
    if not 0x80000000 <= fields["load_address"] <= fields["entry_pc"] < fields["load_address"] + fields["load_size"]:
        raise FormatError("PS-X EXE entry point outside payload")
    fields["payload_file_offset"] = 0x800
    fields["region_string"] = data[0x4c:0x800].split(b"\0")[0].decode("ascii", "replace").strip()
    return fields


@dataclass(frozen=True)
class WadEntry:
    index: int
    offset: int
    size: int


def wad_index(header: bytes, total_size: int) -> list[WadEntry]:
    """Offset/size pairs; first nonempty offset bounds the table, not a count.

    Preserves zero-length slots and refuses overlaps/truncation. A zero/zero
    pair terminates the directory: nested level headers have metadata after it.
    This structural
    recognizer does NOT assign content types or label every candidate as WAD.
    """
    if len(header) < 8:
        raise FormatError("WAD header too short")
    boundary = None
    for at in range(0, min(len(header), 0x10000)-7, 8):
        offset, size = struct.unpack_from("<II", header, at)
        if offset or size:
            if not offset or offset < at+8 or offset % 4 or offset > total_size:
                raise FormatError("Invalid first WAD extent")
            boundary = offset
            break
    if boundary is None or boundary > len(header) or boundary > 0x10000 or boundary % 8:
        raise FormatError("No bounded WAD directory")
    entries = []
    previous_end = boundary
    for at in range(0, boundary, 8):
        offset, size = struct.unpack_from("<II", header, at)
        if offset == size == 0:
            break
        if not offset or offset < previous_end or offset+size > total_size:
            raise FormatError(f"Invalid WAD extent in slot {at//8}")
        entries.append(WadEntry(at//8, offset, size))
        previous_end = offset+size
    if not entries:
        raise FormatError("Empty WAD directory")
    return entries


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def import_disc(source: Path, output: Path, all_files: bool):
    output = output.resolve()
    if output == source.resolve() or output in source.resolve().parents:
        raise FormatError("Output must be a separate generated directory")
    with Disc(source) as disc:
        entries = disc.entries()
        manifest = {"schema": 1, "source_name": disc.path.name, "source_sha256": digest(disc.path),
                    "sector_stride": disc.stride, "sector_count": disc.sectors, "files": []}
        for entry in entries:
            info = asdict(entry)
            selected = all_files or entry.path in ("SYSTEM.CNF", "SCUS_944.25", "WAD.WAD", "SPEECH.STR")
            if selected:
                # Streaming extents use 2048-byte ISO accounting but 2352-byte raw
                # sectors. Keep the entire sector, including subheaders and EDC.
                raw = entry.path.endswith((".STR", ".XA"))
                dest = output / "disc" / (entry.path + (".raw" if raw else ""))
                disc.extract(entry, dest, raw=raw)
                info.update(extracted_path=dest.relative_to(output).as_posix(), sha256=digest(dest),
                            representation="raw-sectors" if raw else "logical-2048")
                if entry.path == "SCUS_944.25":
                    exe = dest.read_bytes()
                    info["psx_exe"] = exe_header(exe)
                    payload = output / "exe" / "main.bin"
                    payload.parent.mkdir(parents=True, exist_ok=True)
                    payload.write_bytes(exe[0x800:0x800+info["psx_exe"]["load_size"]])
                    write_json(output / "exe" / "main.json", {**info["psx_exe"], "sha256": info["sha256"]})
                print(f"Extracted {entry.path}: {dest.stat().st_size:,} bytes")
            manifest["files"].append(info)
        write_json(output / "manifest.json", manifest)
    archive = output / "disc" / "WAD.WAD"
    if archive.exists():
        with archive.open("rb") as stream:
            index = wad_index(stream.read(0x10000), archive.stat().st_size)
            records = []
            for entry in index:
                stream.seek(entry.offset)
                head = stream.read(min(entry.size, 0x10000))
                info = asdict(entry)
                info["first_words"] = [u32(head, at) for at in range(0, min(len(head)//4*4, 32), 4)]
                try:
                    info["nested_candidate"] = [asdict(e) for e in wad_index(head, entry.size)]
                except FormatError:
                    pass
                records.append(info)
        write_json(output / "wad" / "index.json", {"schema": 1, "archive_sha256": digest(archive), "entries": records})
        print(f"Indexed {len(index)} WAD slots ({sum(e.size == 0 for e in index)} zero-length placeholders)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    imp = sub.add_parser("import")
    imp.add_argument("source", type=Path)
    imp.add_argument("--output", type=Path, default=Path("data"))
    imp.add_argument("--all", action="store_true", help="also extract the bundled demo (game music and speech are included by default)")
    ext = sub.add_parser("wad-extract")
    ext.add_argument("archive", type=Path)
    ext.add_argument("index", type=int)
    ext.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "import":
            import_disc(args.source, args.output, args.all)
        else:
            if args.output.resolve() == args.archive.resolve():
                raise FormatError("Cannot overwrite source archive")
            with args.archive.open("rb") as stream:
                entries = wad_index(stream.read(0x10000), args.archive.stat().st_size)
                entry = next((e for e in entries if e.index == args.index), None)
                if entry is None:
                    raise FormatError(f"No nonempty WAD slot {args.index}")
                stream.seek(entry.offset)
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_bytes(stream.read(entry.size))
    except (FormatError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    main()
