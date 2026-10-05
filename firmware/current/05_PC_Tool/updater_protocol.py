"""Stop-and-wait firmware upgrade over USB or classic Bluetooth SPP."""

from __future__ import annotations

from dataclasses import dataclass
import time
from typing import Callable

from firmware_image import FirmwareImage, HARDWARE_ID
from protocol import ParsedFrame, parse_line


CHUNK_SIZE = 192
BLUETOOTH_CHUNK_SIZE = 96


class UpgradeError(RuntimeError):
    pass


@dataclass(frozen=True)
class UpgradeResult:
    old_version: str
    new_version: str
    bytes_sent: int


def _request(port: object, command: str, response_kind: str,
             response_command: str = "", timeout_s: float = 5.0) -> ParsedFrame:
    port.write((command + "\r\n").encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            time.sleep(0.01)
            continue
        frame = parse_line(raw.decode("ascii", errors="replace"))
        if frame.kind == "error":
            if not response_command or frame.values.get("cmd", "").upper() == response_command:
                raise UpgradeError(f"设备拒绝 {command.split()[0]}：{frame.values.get('reason', frame.raw)}")
        if frame.kind != response_kind:
            continue
        if response_command and frame.values.get("cmd", "").upper() != response_command:
            continue
        return frame
    raise TimeoutError(f"等待设备响应超时：{response_command or response_kind}")


def _status(port: object) -> tuple[int, int, bool]:
    frame = _request(port, "FW STATUS", "ack", "FW_STATUS")
    fields = frame.values.get("detail", "").split("/")
    if len(fields) != 3:
        raise UpgradeError("设备返回的升级状态无效")
    return int(fields[0]), int(fields[1]), fields[2] == "1"


def _wait_output_off(port: object) -> None:
    _request(port, "OUT OFF", "ack", "OUT")
    for _ in range(20):
        frame = _request(port, "STATUS", "telemetry", timeout_s=2.0)
        if frame.values.get("out") == "0" and frame.values.get("req") == "0":
            return
        time.sleep(0.1)
    raise UpgradeError("输出未确认关闭，升级已停止")


def _send_image(port: object, image: FirmwareImage,
                progress: Callable[[int, str], None],
                cancelled: Callable[[], bool],
                chunk_size: int,
                response_timeout_s: float) -> None:
    offset = 0
    while offset < image.size:
        if cancelled():
            _request(port, "FW ABORT", "ack", "FW_ABORT")
            raise UpgradeError("用户取消了升级")
        part = image.data[offset:offset + chunk_size]
        command = f"FW DATA {offset} {part.hex().upper()}"
        for attempt in range(3):
            try:
                reply = _request(port, command, "ack", "FW_DATA",
                                 timeout_s=response_timeout_s)
                received = int(reply.values.get("detail", "-1"))
            except (TimeoutError, UpgradeError, ValueError):
                received, total, _ = _status(port)
                if total != image.size:
                    raise UpgradeError("设备端镜像长度与本机不一致")
            if received >= offset + len(part):
                offset += len(part)
                progress(8 + int(offset * 72 / image.size),
                         f"上传镜像 {offset:,} / {image.size:,} 字节")
                break
            if received != offset or attempt == 2:
                raise UpgradeError(f"传输位置不一致：PC={offset}，设备={received}")
        else:
            raise UpgradeError("固件数据传输失败")


def perform_upgrade(
    port_name: str,
    image: FirmwareImage,
    serial_factory: Callable[..., object],
    progress: Callable[[int, str], None],
    cancelled: Callable[[], bool] = lambda: False,
    transport: str = "usb",
    initial_port: object | None = None,
) -> UpgradeResult:
    """Stage and apply an image, optionally reusing an already-connected port.

    A borrowed port stays open on pre-apply failure, allowing the normal
    monitor to resume without tearing down a Bluetooth SPP connection.
    Once APPLY is sent the device resets, so that port must be closed.
    """

    if transport not in {"usb", "bluetooth"}:
        raise ValueError("升级链路必须是 USB 或已配对的蓝牙 SPP 串口")
    wireless = transport == "bluetooth"
    chunk_size = BLUETOOTH_CHUNK_SIZE if wireless else CHUNK_SIZE
    response_timeout_s = 12.0 if wireless else 7.0

    progress(0, "正在使用已连接的设备检查型号" if initial_port is not None
             else "正在连接设备并检查型号")
    owns_port = initial_port is None
    port = (serial_factory(port=port_name, baudrate=115200, timeout=0.15,
                           write_timeout=3.0) if owns_port else initial_port)
    apply_started = False
    old_version = ""
    try:
        if hasattr(port, "reset_input_buffer"):
            port.reset_input_buffer()
        info = _request(port, "INFO", "info", timeout_s=4.0)
        if info.values.get("hw") != HARDWARE_ID or int(info.values.get("proto", "0")) < 4:
            raise UpgradeError("设备尚未通过 SWD 安装 5.9 BL/应用，或硬件型号不匹配")
        if info.values.get("flashok") != "1":
            raise UpgradeError("设备未检测到 W25Q256，禁止升级")
        if info.values.get("bl") != "1":
            raise UpgradeError("设备未检测到 5.9 BL 标记，请先通过 SWD 完整安装")
        old_version = info.values.get("fw", "")
        if wireless and old_version == "5.9.0":
            raise UpgradeError("设备 5.9.0 尚不接收蓝牙升级；请先用 USB 升到 5.9.1")
        progress(2, "正在确认输出关闭")
        _wait_output_off(port)
        progress(4, "正在准备 W25Q256 暂存区")
        begin = f"FW BEGIN {image.size} {image.crc32:08X} {image.version_code}"
        try:
            _request(port, begin, "ack", "FW_BEGIN", timeout_s=90.0)
        except UpgradeError as exc:
            if "SESSION_ACTIVE" not in str(exc):
                raise
            # The same COM channel owns an interrupted upload. Cancel that
            # incomplete session before starting this explicitly requested one.
            _request(port, "FW ABORT", "ack", "FW_ABORT")
            _request(port, begin, "ack", "FW_BEGIN", timeout_s=90.0)
        except TimeoutError:
            received, total, verified = _status(port)
            if received != 0 or total != image.size or verified:
                raise UpgradeError("暂存区准备状态不明确，已停止升级")
        progress(8, "开始通过蓝牙 SPP 上传固件" if wireless else "开始通过 USB 上传固件")
        _send_image(port, image, progress, cancelled, chunk_size,
                    response_timeout_s)
        progress(82, "设备正在校验暂存镜像")
        try:
            _request(port, "FW END", "ack", "FW_END", timeout_s=45.0)
        except TimeoutError:
            received, total, verified = _status(port)
            if not verified or received != total:
                raise UpgradeError("设备整包校验未完成")
        progress(87, "镜像已验证，准备重启进入 BL")
        apply_started = True
        try:
            _request(port, "FW APPLY", "ack", "FW_APPLY",
                     timeout_s=8.0 if wireless else 3.0)
        except TimeoutError:
            # A reset may race the ACK. The version read-back below decides.
            pass
    finally:
        if owns_port or apply_started:
            port.close()

    progress(90, "BL 正在刷写并验证片内应用；请勿断电")
    deadline = time.monotonic() + 120.0
    last_error = ""
    while time.monotonic() < deadline:
        time.sleep(1.0)
        try:
            probe = serial_factory(port=port_name, baudrate=115200,
                                   timeout=0.15, write_timeout=2.0)
            try:
                if hasattr(probe, "reset_input_buffer"):
                    probe.reset_input_buffer()
                info = _request(probe, "INFO", "info", timeout_s=2.5)
                version = info.values.get("fw", "")
                if version == image.version and info.values.get("ota") == "CONFIRMED":
                    progress(100, f"升级完成，设备版本 {version}")
                    return UpgradeResult(old_version, version, image.size)
                if version == image.version and info.values.get("ota") == "TRIAL":
                    progress(95, "新固件已启动，等待健康检查和回退确认")
                last_error = f"设备当前版本 {version}，预期 {image.version}；状态 {info.values.get('ota', '?')}"
            finally:
                probe.close()
        except Exception as exc:
            last_error = str(exc)
    raise UpgradeError("设备未在规定时间内报告新版本；可能已回退。" + last_error)
