"""Classify observed Windows serial I/O failures without guessing the cause."""

def recoverable_serial_failure(reason: str) -> bool:
    return any(token in reason for token in (
        'ClearCommError failed', 'GetOverlappedResult failed',
        'ReadFile failed', 'WriteFile failed', '串口系统读写失败',
    ))

def describe_serial_failure(reason: str) -> str:
    if recoverable_serial_failure(reason):
        return '串口系统读写失败（USB设备或驱动连接可能中断）：' + reason
    return reason
