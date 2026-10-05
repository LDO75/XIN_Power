"""Validate and load a V5.9 application image for USB updating."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import hashlib
import re
import zlib


APP_BASE = 0x08008000
APP_SIZE = 0x00058000
RAM_BASE = 0x20000000
RAM_END = 0x20010000
HARDWARE_ID = "PY32F403_V2"


@dataclass(frozen=True)
class FirmwareImage:
    path: Path
    data: bytes
    version: str
    version_code: int
    crc32: int
    sha256: str

    @property
    def size(self) -> int:
        return len(self.data)


def parse_version(value: str) -> tuple[str, int]:
    match = re.fullmatch(r"\s*[vV]?(\d+)\.(\d+)\.(\d+)\s*", value)
    if not match:
        raise ValueError("版本格式应为 6.2.0")
    major, minor, patch = map(int, match.groups())
    if major > 99 or minor > 99 or patch > 99:
        raise ValueError("版本号每段必须为 0～99")
    return f"{major}.{minor}.{patch}", major * 10000 + minor * 100 + patch


def decode_intel_hex(text: str) -> bytes:
    memory: dict[int, int] = {}
    upper = 0
    ended = False
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if ended or not line.startswith(":") or len(line[1:]) % 2:
            raise ValueError(f"HEX 第 {line_number} 行格式错误")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise ValueError(f"HEX 第 {line_number} 行含非十六进制字符") from exc
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise ValueError(f"HEX 第 {line_number} 行长度或校验和错误")
        count, address_hi, address_lo, record_type = record[:4]
        address = (address_hi << 8) | address_lo
        payload = record[4:4 + count]
        if record_type == 0:
            absolute = upper + address
            if absolute < APP_BASE or absolute + count > APP_BASE + APP_SIZE:
                raise ValueError("HEX 含应用区以外的地址，不能用于固件升级")
            for index, byte in enumerate(payload):
                previous = memory.setdefault(absolute + index, byte)
                if previous != byte:
                    raise ValueError("HEX 同一地址存在不一致的数据")
        elif record_type == 1:
            if count != 0:
                raise ValueError("HEX 结束记录无效")
            ended = True
        elif record_type == 4:
            if count != 2:
                raise ValueError("HEX 扩展地址记录无效")
            upper = int.from_bytes(payload, "big") << 16
        elif record_type in (2, 3, 5):
            # Type 2 changes the addressing mode and is not emitted by this
            # Keil project; reject rather than silently misplace its records.
            if record_type == 2:
                raise ValueError("不支持 HEX 段地址记录，请导出线性地址 HEX")
        else:
            raise ValueError(f"HEX 记录类型 {record_type} 不受支持")
    if not ended or APP_BASE not in memory:
        raise ValueError("HEX 缺少结束记录或应用向量表")
    size = max(memory) - APP_BASE + 1
    image = bytearray(b"\xFF" * size)
    for address, byte in memory.items():
        image[address - APP_BASE] = byte
    return bytes(image)


def _validate_vectors(data: bytes) -> None:
    if len(data) < 8 or len(data) > APP_SIZE:
        raise ValueError("固件长度超出 5.9 应用区")
    stack = int.from_bytes(data[:4], "little")
    entry = int.from_bytes(data[4:8], "little")
    if not (RAM_BASE <= stack <= RAM_END and stack % 8 == 0):
        raise ValueError("固件栈地址无效；可能选错了 MCU 或旧版固件")
    if not (entry & 1 and APP_BASE <= (entry & ~1) < APP_BASE + len(data)):
        raise ValueError("固件入口不在实际镜像范围内；请确认 Keil 链接地址")


def _validate_embedded_version(data: bytes, expected: str) -> None:
    match = re.search(rb"@INFO,fw=(\d+\.\d+\.\d+),proto=[45]", data)
    if not match:
        raise ValueError("固件缺少受支持的协议和版本标记，禁止升级")
    actual = match.group(1).decode("ascii")
    if actual != expected:
        raise ValueError(f"固件内版本 {actual} 与填写的 {expected} 不一致")


def load_firmware(path: str | Path, version: str) -> FirmwareImage:
    source = Path(path)
    normalized, code = parse_version(version)
    if source.suffix.lower() == ".hex":
        data = decode_intel_hex(source.read_text(encoding="ascii"))
    elif source.suffix.lower() == ".bin":
        data = source.read_bytes()
    else:
        raise ValueError("请选择 Keil 生成的 .hex 或应用区 .bin")
    _validate_vectors(data)
    _validate_embedded_version(data, normalized)
    return FirmwareImage(
        path=source,
        data=data,
        version=normalized,
        version_code=code,
        crc32=zlib.crc32(data) & 0xFFFFFFFF,
        sha256=hashlib.sha256(data).hexdigest(),
    )
