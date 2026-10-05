"""Manual DAC calibration: one acknowledged action at a time, no auto sweep."""
from __future__ import annotations

import math
import time
from PySide6.QtCore import QObject, Property, QTimer, Signal, Slot

LEGACY_TARGETS_MV = (3000, 3300, 5000, 7200, 8400, 9000,
              12000, 18000, 20000, 24000, 28000, 32000,
              3600, 3900, 4000, 4500, 2000)
TARGETS_MV = (3200,3300,3500,3800,4000,4200,5000,7200,8400,9000,
              12000,18000,20000,24000,28000,32000)


def meter_millivolts(text: str, index: int) -> int:
    value = float(text)
    if not math.isfinite(value) or not 0 <= index < max(len(TARGETS_MV),len(LEGACY_TARGETS_MV)):
        raise ValueError("请输入有效的万用表读数")
    mv = round(value * 1000)
    if not 0 < mv <= 65535:
        raise ValueError("请输入大于 0 且不超过 65.535 V 的实测值")
    return mv


class CurveCalibration(QObject):
    changed = Signal()

    def __init__(self, send, can_start, parent=None):
        super().__init__(parent)
        self._send = send
        self._can_start = can_start
        self._targets = TARGETS_MV
        self._supported = False
        self._device_version = ""
        self._active = False
        self._may_be_active = False
        self._ready = False
        self._index = 0
        self._dac = 0
        self._voltage = 0
        self._rows = [{} for _ in self._targets]
        self._device_point_count = len(self._targets)
        self._saved = False
        self._capture_expected = None
        self._save_diagnostic = ""
        self._pending = ""
        self._deadline = 0.0
        self._last_state = 0.0
        self._read_index = -1
        self._status = "请先连接电源，手动调节 DAC 并记录万用表实测值。"
        self._timer = QTimer(self)
        self._timer.setInterval(500)
        self._timer.timeout.connect(self._poll)
        self._timer.start()

    @Property(bool, notify=changed)
    def supported(self):
        return self._supported

    @Property(bool, notify=changed)
    def active(self):
        return self._active

    @Property(bool, notify=changed)
    def ready(self):
        return self._active and self._ready and not self._pending

    @Property(bool, notify=changed)
    def busy(self):
        return bool(self._pending)

    @Property(bool, notify=changed)
    def requiresStop(self):
        # A read-only table refresh must not switch off a normal output when
        # disconnecting. An unacknowledged START, however, may have enabled CE.
        return self._active or self._may_be_active or self._pending not in ("", "refresh")

    @Property(str, notify=changed)
    def status(self):
        return self._status

    @Property(str, notify=changed)
    def liveText(self):
        return (f"当前点 {self._targets[self._index] / 1000:g} V  ·  "
                f"DAC {self._dac}  ·  设备读数 {self._voltage / 1000:.3f} V")

    @Property(str, notify=changed)
    def targetText(self):
        return f"{self._targets[self._index] / 1000:.3f}"

    @Property("QVariantList", notify=changed)
    def pointRows(self):
        return [dict(index=i, label=f"{mv / 1000:g} V", target=f"{mv / 1000:.3f}",
                     actual=f"{row.get('actual', 0) / 1000:.3f} V" if row.get("valid") else "—",
                     dac=str(row.get("dac", "—")) if row.get("valid") else "—",
                     state=("已保存" if self._saved else "未保存") if row.get("valid") else "待校准")
                for i in sorted(range(self._device_point_count), key=self._targets.__getitem__)
                for mv, row in [(self._targets[i], self._rows[i])]]

    @Property(int, notify=changed)
    def total(self):
        return self._device_point_count

    @Property(int, notify=changed)
    def completed(self):
        return sum(bool(row.get("valid")) for row in self._rows)

    def connection_changed(self, connected: bool, version: str):
        try:
            supported = connected and tuple(int(x) for x in version.split(".")) >= (6, 0, 3)
        except (ValueError, IndexError):
            supported = False
        if supported == self._supported and version == self._device_version:
            return
        self._targets = TARGETS_MV if supported and tuple(int(x) for x in version.split(".")) >= (6,2,0) else LEGACY_TARGETS_MV
        self._device_version = version
        self._supported = supported
        self._pending = ""
        self._active = self._ready = False
        self._may_be_active = False
        self._last_state = 0.0
        self._rows = [{} for _ in self._targets]
        self._device_point_count = len(self._targets)
        self._capture_expected = None
        self._save_diagnostic = ""
        if supported:
            self.refresh()
        else:
            self._status = "请先连接支持 DAC 多点校准的电源。"
        self.changed.emit()

    def _request(self, pending: str, command: str):
        self._pending = pending
        self._deadline = time.monotonic() + (8 if pending == "CAL_POINT_CAPTURE" else 5)
        self._ready = False
        self._send(command)
        self.changed.emit()

    @Slot()
    def refresh(self):
        if not self._supported or self._pending or self._active:
            return
        self._status = "正在读取设备中的校准表…"
        self._read_index = -1
        self._request("refresh", "CAL POINT GET")

    @Slot(int)
    def start(self, index: int):
        if not self._supported or self._pending or self._active:
            return
        if not self._can_start():
            self._status = "请先完成升级或测量校准操作。"
            self.changed.emit()
            return
        if not 0 <= index < len(self._targets):
            return
        if index >= self._device_point_count:
            self._status = "2V 校准点需要固件 6.1.1 或更新版本。"
            self.changed.emit()
            return
        self._index = index
        self._may_be_active = True
        self._status = "正在写入本点 DAC 并开启校准输出。"
        self._request("CAL_POINT_START", f"CAL POINT START {index}")

    @Slot(int)
    def step(self, delta: int):
        if not self._active or not self.ready or delta not in (-5, -1, 1, 5):
            return
        self._status = "正在写入 DAC…"
        self._request("CAL_POINT_STEP", f"CAL POINT STEP {delta}")

    @Slot(str)
    def capture(self, text: str):
        if not self._active or not self.ready:
            return
        try:
            value = meter_millivolts(text, self._index)
        except (ValueError, OverflowError) as exc:
            self._status = str(exc)
            self.changed.emit()
            return
        self._capture_expected = (self._index, value, self._dac)
        self._save_diagnostic = ""
        self._status = "正在关闭输出并保存本点；等待设备确认，不要断电。"
        self._request("CAL_POINT_CAPTURE", f"CAL POINT CAPTURE {value}")

    @Slot()
    def stop(self):
        if not self._supported or self._pending == "CAL_POINT_STOP":
            return
        self._status = "正在关闭校准输出；尚未采集的点不会保存。"
        self._request("CAL_POINT_STOP", "CAL POINT STOP")

    def _poll(self):
        if self._pending and time.monotonic() > self._deadline:
            self._pending = ""
            self._status = "操作未确认。请查询校准表；若输出仍开启，请立即停止校准。"
            self._ready = False
            if self._supported:
                self._send("CAL POINT STATUS")
            self.changed.emit()
        if self._supported and self._active and not self._pending:
            self._send("CAL POINT STATUS")

    def on_frame(self, frame):
        v = frame.values
        try:
            if frame.kind == "curve" and self._pending == "refresh":
                if int(v["schema"]) not in (2,5) or int(v["total"]) not in (16, len(self._targets)):
                    raise ValueError("不支持的校准表格式")
                if (int(v["schema"])==5) != (self._targets==TARGETS_MV):
                    raise ValueError("不支持的校准表格式")
                self._device_point_count = int(v["total"])
                self._rows = [{} for _ in self._targets]
                self._saved = v.get("saved") == "1"
                self._read_index = 0
                self._send("CAL POINT GET 0")
            elif frame.kind == "curve_point" and self._pending == "refresh":
                i = int(v["id"])
                if i != self._read_index or int(v["target"]) != self._targets[i]:
                    return
                self._rows[i] = dict(valid=v.get("valid") == "1",
                                     actual=int(v["actual"]), dac=int(v["dac"]))
                self._deadline = time.monotonic() + 5
                self._read_index += 1
                if self._read_index < self._device_point_count:
                    self._send(f"CAL POINT GET {self._read_index}")
                else:
                    self._pending = ""
                    self._status = f"设备校准表：{self.completed}/{self._device_point_count} 点。"
                    if self._capture_expected is not None:
                        index, actual, dac = self._capture_expected
                        row = self._rows[index]
                        if (self._saved and row.get("valid") and
                                row.get("actual") == actual and row.get("dac") == dac):
                            self._status = f"本点保存并读回核对成功：{actual / 1000:.3f} V，DAC {dac}；输出已关闭。"
                        else:
                            self._status = "本次采集与设备校准表读回不一致，尚未确认保存成功，请查看日志。"
                        self._capture_expected = None
            elif frame.kind == "curve_state":
                if not self._supported:
                    return
                self._last_state = time.monotonic()
                self._active = v.get("active") == "1"
                self._may_be_active = self._active
                self._ready = v.get("ready") == "1"
                i = int(v["id"])
                if 0 <= i < len(self._targets):
                    self._index = i
                self._dac = int(v["dac"])
                self._voltage = int(v["vout"])
                if v.get("fault", "NONE") != "NONE":
                    self._ready = False
                    reason = v.get("fault", "UNKNOWN")
                    self._status = f"保护已触发（{reason}），输出已关闭。请到监控页清除故障后再启动校准。"
                elif self._active and not self._pending:
                    self._status = ("DAC 已写入：对照万用表调整，再输入实测值保存。"
                                    if self._ready else "等待 DAC 写入确认。")
                    if self._ready and self._voltage < 500:
                        self._status = "DAC 已写入，但设备输出低于 0.5V；请对照万用表检查未起压原因，仍可手动微调 DAC。"
                elif not self._active and not self._pending and self._capture_expected is not None:
                    self.refresh()  # A lost ACK is resolved by reading the saved pair.
            elif frame.kind == "ack" and v.get("cmd") == self._pending:
                command = self._pending
                self._pending = ""
                if command == "CAL_POINT_START":
                    self._active = True
                    self._ready = False
                    self._send("CAL POINT STATUS")
                elif command == "CAL_POINT_STEP":
                    self._ready = False
                    self._send("CAL POINT STATUS")
                elif command == "CAL_POINT_CAPTURE":
                    self._active = False
                    self._may_be_active = False
                    self._status = "设备已确认写入，正在读回核对本点。"
                    self._send("STATUS")
                    self.refresh()
                elif command == "CAL_POINT_STOP":
                    self._active = False
                    self._may_be_active = False
                    self._status = "校准已停止，输出已关闭。"
                    self._send("STATUS")
            elif frame.kind == "curve_save":
                if v.get("ok") == "0":
                    self._save_diagnostic = v.get("reason", frame.raw)
                else:
                    return
            elif frame.kind == "flash_diagnostic":
                self._save_diagnostic = (f"Flash擦除失败：{v.get('stage', '?')}，"
                                         f"耗时{v.get('elapsed', '?')}ms，SPI错误{v.get('hal', '?')}")
            elif frame.kind == "error" and v.get("cmd", "").startswith("CAL_POINT"):
                self._pending = ""
                self._ready = False
                self._capture_expected = None
                if v.get("reason") == "FAULT_ACTIVE_USE_CLEAR":
                    self._status = "故障仍锁存，不能启动校准。请到监控页点击清除故障，输出仍保持关闭。"
                else:
                    self._status = "保存或校准操作失败：" + (self._save_diagnostic or v.get("reason", frame.raw))
                self._send("CAL POINT STATUS")
            else:
                return
        except (KeyError, ValueError, IndexError):
            self._pending = ""
            self._ready = False
            self._status = "校准回复格式异常，请重新读取。"
        self.changed.emit()
