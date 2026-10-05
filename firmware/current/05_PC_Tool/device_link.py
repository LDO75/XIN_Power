"""XIN Power 串口枚举与设备探测。

本模块不依赖 Tk，便于独立测试，也便于以后替换上位机界面。
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable, Iterable


@dataclass(frozen=True)
class PortDescriptor:
    """系统串口的稳定描述。"""

    device: str
    description: str
    hwid: str
    transport: str


@dataclass(frozen=True)
class ProbeResult:
    """对单个串口执行协议探测后的结果。"""

    device: str
    matched: bool
    firmware: str = ""
    protocol: str = ""
    hardware: str = ""
    error: str = ""


def classify_transport(description: str, hwid: str) -> str:
    """根据 Windows 串口描述区分蓝牙 SPP 与 USB/普通串口。"""

    text = f"{description} {hwid}".lower()
    bluetooth_tokens = ("bluetooth", "bth", "蓝牙", "standard serial over bluetooth")
    usb_tokens = ("usb", "ch340", "ch341", "cp210", "ftdi", "wch")
    if any(token in text for token in bluetooth_tokens):
        return "BLUETOOTH SPP"
    if any(token in text for token in usb_tokens):
        return "USB / UART"
    return "SERIAL"


def describe_ports(port_items: Iterable[object]) -> list[PortDescriptor]:
    """把 pyserial 的 ListPortInfo 转成上位机使用的稳定数据。"""

    result: list[PortDescriptor] = []
    for item in port_items:
        device = str(getattr(item, "device", ""))
        if not device:
            continue
        description = str(getattr(item, "description", "") or "串口设备")
        hwid = str(getattr(item, "hwid", "") or "")
        result.append(
            PortDescriptor(
                device=device,
                description=description,
                hwid=hwid,
                transport=classify_transport(description, hwid),
            )
        )
    return sorted(result, key=lambda item: (int(item.device[3:]) if item.device.upper().startswith("COM") and item.device[3:].isdigit() else 99999, item.device))


def _extract_field(line: str, key: str) -> str:
    prefix = key.lower() + "="
    for part in line.split(","):
        part = part.strip()
        if part.lower().startswith(prefix):
            return part.split("=", 1)[1].strip()
    return ""


def probe_xin_power(
    device: str,
    serial_factory: Callable[..., object],
    baud_rate: int = 115200,
    timeout_s: float = 1.0,
) -> ProbeResult:
    """发送 PING/INFO，判断该 COM 口是否为 XIN Power。

    serial_factory 由调用方传入，正式运行时使用 ``serial.Serial``，测试时
    可传入假的串口对象，避免测试依赖真实硬件。
    """

    port = None
    try:
        port = serial_factory(
            port=device,
            baudrate=baud_rate,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0.08,
            write_timeout=0.4,
        )
        if hasattr(port, "reset_input_buffer"):
            port.reset_input_buffer()
        port.write(b"PING\r\n")
        port.flush()
        deadline = time.monotonic() + timeout_s
        matched = False
        firmware = ""
        protocol = ""
        hardware = ""
        info_sent = False
        buffer = bytearray()

        while time.monotonic() < deadline:
            chunk = port.read(256)
            if chunk:
                buffer.extend(chunk)
            while b"\n" in buffer:
                raw, _, remainder = buffer.partition(b"\n")
                buffer = bytearray(remainder)
                line = raw.rstrip(b"\r").decode("utf-8", errors="replace")
                if line.startswith("@ACK,") and "CMD=PING" in line.upper():
                    matched = True
                if line.startswith("@INFO,"):
                    matched = True
                    firmware = _extract_field(line, "fw")
                    protocol = _extract_field(line, "proto")
                    hardware = _extract_field(line, "hw")
                if matched and not info_sent:
                    port.write(b"INFO\r\n")
                    port.flush()
                    info_sent = True
            if matched and info_sent and firmware:
                break

        return ProbeResult(device=device, matched=matched, firmware=firmware,
                           protocol=protocol, hardware=hardware)
    except Exception as exc:  # 串口被占用也属于正常探测结果，不让线程崩溃。
        return ProbeResult(device=device, matched=False, error=str(exc))
    finally:
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
