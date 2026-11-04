#!/usr/bin/env python3
"""Integration checks using tiny synthetic discs, never game data."""
import pathlib
import struct
import subprocess
import sys
import tempfile

SECTOR = 2048

def record(name, lba, size, directory=False):
    name = name.encode("ascii")
    n = 33 + len(name) + (len(name) % 2 == 0)
    r = bytearray(n)
    r[0] = n
    struct.pack_into("<I", r, 2, lba)
    struct.pack_into(">I", r, 6, lba)
    struct.pack_into("<I", r, 10, size)
    struct.pack_into(">I", r, 14, size)
    r[25] = 2 if directory else 0
    r[28], r[31] = 1, 1
    r[32] = len(name)
    r[33:33 + len(name)] = name
    return r

def sfo(disc_id, version):
    keys = b"DISC_ID\0DISC_VERSION\0"
    values = disc_id.encode() + b"\0" + version.encode() + b"\0"
    head = struct.pack("<5I", 0x46535000, 0x101, 52, 52 + len(keys), 2)
    entries = struct.pack("<HHIII", 0, 0x204, len(disc_id) + 1, len(disc_id) + 1, 0)
    entries += struct.pack("<HHIII", 8, 0x204, len(version) + 1, len(version) + 1, len(disc_id) + 1)
    return head + entries + keys + values

def disc(disc_id="ULES01213", version="1.01", include_data=True):
    result = bytearray(27 * SECTOR)
    result[16 * SECTOR:16 * SECTOR + 7] = b"\x01CD001\x01"
    root = record("\0", 20, SECTOR, True)
    result[16 * SECTOR + 156:16 * SECTOR + 156 + len(root)] = root
    metadata = sfo(disc_id, version)
    sectors = {
        20: record("PSP_GAME", 21, SECTOR, True),
        21: record("PARAM.SFO;1", 24, len(metadata)) +
            record("SYSDIR", 22, SECTOR, True) + record("USRDIR", 23, SECTOR, True),
        22: record("EBOOT.BIN;1", 25, 4),
        23: record("DATA.BIN;1", 26, 4) if include_data else b"",
        24: metadata, 25: b"TEST", 26: b"TEST",
    }
    for sector, contents in sectors.items():
        result[sector * SECTOR:sector * SECTOR + len(contents)] = contents
    return result

def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    cases = [
        ("eu", disc(), 0, "structure recognized"),
        ("us", disc("ULUS10391"), 0, "structure recognized"),
        ("jp", disc("ULJM05500"), 0, "structure recognized"),
        ("other", disc("TEST00000"), 1, "not in upstream"),
        ("version", disc(version="9.99"), 1, "not in upstream"),
        ("missing-data", disc(include_data=False), 1, "missing executable"),
        ("truncated", b"not an iso", 1, "DISC:"),
        ("absent", None, 1, "cannot open image"),
    ]
    with tempfile.TemporaryDirectory() as directory:
        for name, content, expected_code, expected_text in cases:
            path = pathlib.Path(directory) / (name + ".iso")
            if content is not None:
                path.write_bytes(content)
            run = subprocess.run([str(executable), str(path)], capture_output=True, text=True, timeout=15)
            assert run.returncode == expected_code, (name, run.returncode, run.stdout, run.stderr)
            assert expected_text in run.stdout, (name, run.stdout)
            assert "PASS: 32 MiB" in run.stdout, (name, run.stdout)
            assert "PASS: fibers, 129 cycles" in run.stdout, (name, run.stdout)
            assert "PASS: recompiled Allegrex + delay import + PSP Kernel" in run.stdout, (name, run.stdout)
            print("PASS", name)
    print("8/8 synthetic-disc integration checks passed")

if __name__ == "__main__":
    main()
