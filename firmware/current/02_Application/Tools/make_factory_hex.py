"""Merge separately built BL and application HEX for one-time SWD install."""

from __future__ import annotations

import argparse
from pathlib import Path


BOOT_BASE, APP_BASE, FLASH_END = 0x08000000, 0x08008000, 0x08060000


def read_hex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    upper = 0
    eof = False
    for number, raw in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if eof or not line.startswith(":"):
            raise ValueError(f"{path.name}:{number}: invalid record")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError(f"{path.name}:{number}: checksum/length failed")
        length, high, low, kind = record[:4]
        offset = (high << 8) | low
        payload = record[4:4 + length]
        if kind == 0:
            for index, value in enumerate(payload):
                address = upper + offset + index
                if address in memory and memory[address] != value:
                    raise ValueError(f"{path.name}: overlapping data mismatch")
                memory[address] = value
        elif kind == 1:
            eof = True
        elif kind == 4 and length == 2:
            upper = int.from_bytes(payload, "big") << 16
        elif kind not in (3, 5):
            raise ValueError(f"{path.name}:{number}: unsupported record type {kind}")
    if not eof or not memory:
        raise ValueError(f"{path.name}: empty or missing EOF")
    return memory


def record(offset: int, kind: int, data: bytes) -> str:
    payload = bytes([len(data), offset >> 8, offset & 255, kind]) + data
    return ":" + (payload + bytes([(-sum(payload)) & 255])).hex().upper()


def merge(boot: Path, app: Path, output: Path) -> None:
    boot_data = read_hex(boot)
    app_data = read_hex(app)
    if min(boot_data) != BOOT_BASE or max(boot_data) >= APP_BASE:
        raise ValueError("BL image must occupy 0x08000000..0x08007FFF only")
    if min(app_data) != APP_BASE or max(app_data) >= FLASH_END:
        raise ValueError("APP image must occupy 0x08008000..0x0805FFFF only")
    combined = boot_data | app_data
    lines: list[str] = []
    upper = -1
    addresses = sorted(combined)
    position = 0
    while position < len(addresses):
        start = addresses[position]
        new_upper = start >> 16
        if new_upper != upper:
            upper = new_upper
            lines.append(record(0, 4, upper.to_bytes(2, "big")))
        chunk = bytearray([combined[start]])
        position += 1
        while (position < len(addresses) and len(chunk) < 16 and
               addresses[position] == start + len(chunk) and
               (addresses[position] >> 16) == upper):
            chunk.append(combined[addresses[position]])
            position += 1
        lines.append(record(start & 0xFFFF, 0, bytes(chunk)))
    lines.append(record(0, 1, b""))
    output.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("boot", type=Path, help="XIN_Power_BL.hex")
    parser.add_argument("app", type=Path, help="XIN_Power.hex")
    parser.add_argument("output", type=Path, help="factory.hex for initial SWD download")
    args = parser.parse_args()
    merge(args.boot, args.app, args.output)
    print(f"Factory HEX written to {args.output}")


if __name__ == "__main__":
    main()
