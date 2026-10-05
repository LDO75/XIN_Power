"""XIN Power V5.9 串口协议解析与命令生成（兼容 V5.8.6）。"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Tuple


@dataclass(frozen=True)
class ParsedFrame:
    """一行设备输出的解析结果。"""

    kind: str
    values: Dict[str, str]
    raw: str


@dataclass(frozen=True)
class CalibrationResult:
    voltage_gain_ppm: int
    voltage_offset_mv: int
    current_gain_ppm: int
    current_offset_ua: int


def _parse_pairs(parts: list[str]) -> Dict[str, str]:
    values: Dict[str, str] = {}
    for part in parts:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key.strip().lower()] = value.strip()
        elif part.strip():
            values.setdefault("message", part.strip())
    return values


def parse_line(line: str) -> ParsedFrame:
    """解析固件输出；普通方括号日志保持为 log 类型。"""

    raw = line.rstrip("\r\n")
    if raw.startswith("@PID,"):
        return ParsedFrame("pid_config", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@FAULT,"):
        return ParsedFrame("fault", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@TEL,"):
        return ParsedFrame("telemetry", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@ACK,"):
        return ParsedFrame("ack", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@ERR,"):
        return ParsedFrame("error", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@INFO,"):
        return ParsedFrame("info", _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@CAL,"):
        return ParsedFrame("calibration", _parse_pairs(raw.split(",")[1:]), raw)
    for prefix, kind in (("@CURVE,", "curve"), ("@CPOINT,", "curve_point"),
                         ("@CSTATE,", "curve_state"), ("@CSAVE,", "curve_save"),
                         ("@FLASH,", "flash_diagnostic")):
        if raw.startswith(prefix):
            return ParsedFrame(kind, _parse_pairs(raw.split(",")[1:]), raw)
    if raw.startswith("@HELP,"):
        return ParsedFrame("help", {"message": raw[6:]}, raw)
    return ParsedFrame("log", {}, raw)


def telemetry_number(values: Dict[str, str], key: str, default: float = 0.0) -> float:
    """安全读取一个遥测数值，字段缺失或损坏时返回默认值。"""

    try:
        return float(values.get(key, default))
    except (TypeError, ValueError):
        return default


def voltage_command(volts: float) -> Tuple[str, int]:
    """将伏特输入转换成固件使用的毫伏命令。"""

    millivolts = int(round(volts * 1000.0))
    if not 3200 <= millivolts <= 32000:
        raise ValueError("电压必须在 3.200V～33.200V 之间")
    return f"SET V {millivolts}", millivolts


def current_command(amps: float) -> Tuple[str, int]:
    """将安培输入转换成固件使用的毫安命令。"""

    milliamps = int(round(amps * 1000.0))
    if not 100 <= milliamps <= 8000:
        raise ValueError("电流必须在 0.100A～8.000A 之间")
    return f"SET I {milliamps}", milliamps


def calculate_calibration(
    raw_v1_mv: float,
    actual_v1_mv: float,
    raw_v2_mv: float,
    actual_v2_mv: float,
    raw_zero_ua: float,
    raw_load_ua: float,
    actual_load_ua: float,
) -> CalibrationResult:
    """由两点电压和零点/带载电流计算线性校准参数。"""

    voltage_span = raw_v2_mv - raw_v1_mv
    current_span = raw_load_ua - raw_zero_ua
    if abs(voltage_span) < 1000:
        raise ValueError("电压校准点跨度不足")
    if current_span < 100000:
        raise ValueError("电流校准点跨度不足")
    voltage_gain = round((actual_v2_mv - actual_v1_mv) * 1_000_000 / voltage_span)
    voltage_offset = round(actual_v1_mv - raw_v1_mv * voltage_gain / 1_000_000)
    current_gain = round(actual_load_ua * 1_000_000 / current_span)
    current_offset = round(-raw_zero_ua * current_gain / 1_000_000)
    if not (700000 <= voltage_gain <= 1300000 and -1500 <= voltage_offset <= 1500):
        raise ValueError("电压校准结果超出安全范围")
    if not (700000 <= current_gain <= 1300000 and -500000 <= current_offset <= 500000):
        raise ValueError("电流校准结果超出安全范围")
    return CalibrationResult(voltage_gain, voltage_offset, current_gain, current_offset)
