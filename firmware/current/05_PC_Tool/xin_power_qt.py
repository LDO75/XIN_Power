"""XIN Power desktop console: Qt Quick UI with a serial worker."""

from __future__ import annotations

from collections import deque
from pathlib import Path
from queue import Empty, Queue
import csv
import os
import argparse
import json
import sys
from datetime import datetime
import threading
import time

from PySide6.QtCore import QObject, Property, QThread, QTimer, QUrl, Signal, Slot
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtWidgets import QApplication, QFileDialog

from device_link import describe_ports
from link_diagnostics import recoverable_serial_failure, describe_serial_failure
from wire_protocol import SerialSession
from readable_log import format_frame, format_command, FAULT_TEXT
from session_record import SessionRecord
from firmware_image import FirmwareImage, load_firmware
from protocol import parse_line, telemetry_number
from updater_protocol import UpgradeError, perform_upgrade
from dac_calibration import CurveCalibration
from pid_tuning import PidTuning


class SerialReader(QThread):
    lineReceived = Signal(str)
    opened = Signal()
    stopped = Signal(str)
    upgradeProgress = Signal(int, str)
    upgradeSucceeded = Signal(str)
    upgradeFailed = Signal(str, bool)

    def __init__(self, port_name: str) -> None:
        super().__init__()
        self.port_name = port_name
        self.commands: Queue[str] = Queue()
        self.frames: Queue = Queue()
        self.urgent = Queue()
        self.stop_event = threading.Event()
        self._upgrade_request: tuple[FirmwareImage, str] | None = None
        self._upgrade_cancel = threading.Event()

    def send(self, command: str) -> None:
        (self.urgent if command in ("OUT OFF","CAL POINT STOP") else self.commands).put(command)

    def stop(self) -> None:
        self.stop_event.set()

    def begin_upgrade(self, image: FirmwareImage, transport: str) -> None:
        # The existing serial handle belongs to this thread. Reuse it rather
        # than closing/reopening a Windows Bluetooth SPP virtual COM port.
        while True:
            try:
                self.commands.get_nowait()
            except Empty:
                break
        self._upgrade_cancel.clear()
        self._upgrade_request = (image, transport)

    def cancel_upgrade(self) -> None:
        self._upgrade_cancel.set()

    def run(self) -> None:
        try:
            import serial
            # Only the explicitly selected port is opened. Short read timeouts
            # let queued commands and disconnect requests run without GUI waits.
            with serial.Serial(self.port_name,115200,timeout=.005,write_timeout=1) as port:
                port.reset_input_buffer();self.opened.emit()
                session=SerialSession(port)
                info=session.handshake(self.stop_event.is_set)
                if info.values.get('hw')!='PY32F403_V2':
                    raise ValueError('选中端口回应的硬件型号不匹配')
                self.frames.put(info)
                # Fresh connection: configure telemetry/read status only. Queued
                # commands from a lost connection are never replayed here.
                try: new_rate = tuple(map(int, info.values.get('fw','0').split('.'))) >= (6,2,2)
                except ValueError: new_rate = False
                if new_rate: session.send('LOG RATE 40')
                self.send('STATUS')
                while not self.stop_event.is_set():
                    request=self._upgrade_request
                    if request is not None and session.pending is None:
                        self._upgrade_request=None
                        image,transport=request
                        def factory(**kwargs):
                            raw=serial.Serial(**kwargs)
                            adapted=SerialSession(raw)
                            try:adapted.handshake(self._upgrade_cancel.is_set)
                            except Exception:raw.close();raise
                            return adapted
                        try:
                            result=perform_upgrade(self.port_name,image,factory,self.upgradeProgress.emit,
                                self._upgrade_cancel.is_set,transport=transport,initial_port=session)
                        except Exception as exc:
                            self.upgradeFailed.emit(str(exc),session.is_open)
                            if not session.is_open:break
                            session.pending=None;self.send('INFO')
                        else:
                            self.upgradeSucceeded.emit(result.new_version);break
                    try:urgent=self.urgent.get_nowait()
                    except Empty:urgent=""
                    if urgent:
                        session.pending=None;session.send(urgent)
                    if session.pending is None:
                        try:command=self.commands.get_nowait()
                        except Empty:command=''
                        if command:
                            try:session.send(command)
                            except (ValueError,OverflowError) as exc:
                                self.frames.put(parse_line('@ERR,cmd=LOCAL,reason='+str(exc)))
                    try:
                        received = session.poll()
                    except TimeoutError:
                        # A missing command ACK is not a broken COM handle.
                        # Keep receiving, report unknown outcome, never replay it.
                        session.pending = None
                        self.frames.put(parse_line('@ERR,cmd=LINK,reason=命令确认超时，结果未知，请读取状态确认'))
                        received = []
                    for frame in received: self.frames.put(frame)
        except Exception as exc:
            self.stopped.emit(describe_serial_failure(str(exc)) if not self.stop_event.is_set() else '')
        else:self.stopped.emit('')


class ProbeScanThread(QThread):
    completed = Signal(object)
    failed = Signal(str)

    def __init__(self, connected_port: str = "", connected_version: str = "") -> None:
        super().__init__()
        self.connected_port = connected_port
        self.connected_version = connected_version

    def run(self) -> None:
        try:
            from serial.tools import list_ports
            # Enumeration never opens ports or probes paired Bluetooth endpoints.
            self.completed.emit([(item.device,f'{item.device}  ·  {item.description}',item.transport,
                                  item.device==self.connected_port)
                                 for item in describe_ports(list_ports.comports())])
        except Exception as exc:self.failed.emit(str(exc))


class Bridge(QObject):
    portsChanged = Signal()
    stateChanged = Signal()
    telemetryChanged = Signal()
    logsChanged = Signal()
    logStateChanged = Signal()
    diagnosticsChanged = Signal()
    updateChanged = Signal()
    calibrationChanged = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.live_api = None
        self._ports: list[str] = []
        self._all_ports = []
        self._transport_mode = "wired"
        self._connecting = False
        self._batching = False
        self._frame_timer = QTimer(self)
        self._frame_timer.setInterval(20)
        self._frame_timer.timeout.connect(self._drain_frames)
        self._frame_timer.start()
        self._record = SessionRecord()
        self._last_telemetry_log = 0.0
        self._vpp = "—"
        self._port_types: dict[str, str] = {}
        self._verified_ports: set[str] = set()
        self._scan_thread: ProbeScanThread | None = None
        self._scanning = False
        self._scan_text = "选择有线或无线，再扫描系统串口"
        self._selected_port = ""
        self._connected = False
        self._connection_text = "未连接"
        self._firmware = "—"
        self._hardware = "—"
        self._voltage = "0.000"
        self._current = "0.000"
        self._input_voltage = "0.000"
        self._power = "0.00"
        self._temperature = "—"
        self._set_voltage = "5.000"
        self._set_current = "0.100"
        self._output_on = False
        self._fault = "NONE"
        self._history_v: deque[float] = deque(maxlen=140)
        self._history_i: deque[float] = deque(maxlen=140)
        self._telemetry_arrivals: deque[float] = deque(maxlen=11)
        self._telemetry_rate_text = "等待遥测"
        self._control_diagnostic = "等待控制状态"
        self._telemetry_rows = deque(maxlen=1000)
        self._session_lines = deque(maxlen=1000)
        self._logs: deque[str] = deque(maxlen=1000)
        self._log_paused = False
        self._paused_log_text = ""
        self._pending_log_count = 0
        self._log_dirty = False
        self._voltage_diagnostic = "读取校准参数后，显示原始电压、增益和偏移；不会自动归零。"
        self._log_timer = QTimer(self)
        self._log_timer.setInterval(100)
        self._log_timer.timeout.connect(self._flush_log_view)
        self._log_timer.start()
        self._worker: SerialReader | None = None
        self._reconnect_pending = False
        self._reconnect_attempts = 0
        self._reconnect_port = ''
        self._image_path = ""
        self._update_progress = 0
        self._update_status = "选择固件文件后可开始升级"
        self._updating = False
        self._calibration_status = "按页面指引采集 5V / 20V 实测值"
        self._calibration_raw: dict[int, int] = {}
        self._calibration_actual: dict[int, float] = {}
        self._capture_pending: tuple[int, float] | None = None
        self._save_pending: tuple[int, int] | None = None
        self.curve = CurveCalibration(
            self.sendCommand,
            lambda: self._connected and not self._updating
                    and self._save_pending is None and self._capture_pending is None,
            self,
        )
        self.pid = PidTuning(self.sendCommand, self)
        self.stateChanged.connect(lambda: self.pid.connection_changed(
            self._connected and not self._updating, self._firmware))
        self._disconnect_after_calibration = False
        self.stateChanged.connect(lambda: self.curve.connection_changed(
            self._connected and not self._updating, self._firmware))

    @Property(QObject, constant=True)
    def pidController(self):
        return self.pid

    @Property("QVariantList", notify=portsChanged)
    def ports(self) -> list[str]:
        return self._ports

    @Property(str, notify=stateChanged)
    def selectedPort(self) -> str:
        return self._selected_port

    @Property(str, notify=stateChanged)
    def selectedTransport(self) -> str:
        return ("蓝牙 SPP" if self._port_types.get(self._selected_port) == "BLUETOOTH SPP"
                else "USB / 串口")

    @Property(str, notify=stateChanged)
    def transportMode(self):return self._transport_mode

    @Property(bool, notify=stateChanged)
    def connecting(self):return self._connecting

    @Property(str, notify=telemetryChanged)
    def vpp(self):return self._vpp

    @Slot(str)
    def selectTransport(self, mode):
        if mode not in ('wired','wireless') or self._worker or self._updating:return
        self._reconnect_pending=False
        self._transport_mode=mode;self._selected_port='';self._filter_ports()

    def _filter_ports(self):
        rows=[row for row in self._all_ports if (row[2]=='BLUETOOTH SPP')==(self._transport_mode=='wireless')]
        self._ports=[row[1] for row in rows]
        self._port_types={row[0]:row[2] for row in self._all_ports}
        if self._selected_port not in {row[0] for row in rows} and not self._worker and not self._reconnect_pending:
            self._selected_port=rows[0][0] if rows else ''
        self._scan_text=(f'找到 {len(rows)} 个'+('已配对蓝牙串口' if self._transport_mode=='wireless' else '有线串口')+
                         '；连接时验证电源身份')
        self.portsChanged.emit();self.stateChanged.emit()

    def _drain_frames(self):
        if not self._worker:return
        self._batching=True;count=0
        try:
            while count<512:
                try:frame=self._worker.frames.get_nowait()
                except Empty:break
                self._on_frame(frame);count+=1
        finally:self._batching=False
        if count:self.telemetryChanged.emit()

    @Property(bool, notify=stateChanged)
    def scanning(self) -> bool:
        return self._scanning

    @Property(str, notify=stateChanged)
    def scanText(self) -> str:
        return self._scan_text

    @Property(bool, notify=stateChanged)
    def connected(self) -> bool:
        return self._connected

    @Property(str, notify=stateChanged)
    def connectionText(self) -> str:
        return self._connection_text

    @Property(str, notify=stateChanged)
    def firmware(self) -> str:
        return self._firmware

    @Property(str, notify=stateChanged)
    def hardware(self) -> str:
        return self._hardware

    @Property(str, notify=telemetryChanged)
    def voltage(self) -> str:
        return self._voltage

    @Property(str, notify=telemetryChanged)
    def current(self) -> str:
        return self._current

    @Property(str, notify=telemetryChanged)
    def inputVoltage(self) -> str:
        return self._input_voltage

    @Property(str, notify=telemetryChanged)
    def power(self) -> str:
        return self._power

    @Property(str, notify=telemetryChanged)
    def temperature(self) -> str:
        return self._temperature

    @Property(str, notify=telemetryChanged)
    def setVoltage(self) -> str:
        return self._set_voltage

    @Property(str, notify=telemetryChanged)
    def setCurrent(self) -> str:
        return self._set_current

    @Property(bool, notify=telemetryChanged)
    def outputOn(self) -> bool:
        return self._output_on

    @Property(str, notify=telemetryChanged)
    def fault(self) -> str:
        return self._fault

    @Property(str, notify=telemetryChanged)
    def faultText(self) -> str:
        labels = {"NONE": "无故障", "SHORT": "短路保护", "IOUT_OC": "输出过流",
                  "IIN_OC": "输入过流", "VIN_UV": "输入欠压", "VOUT_OV": "输出过压",
                  "OTP": "过温", "MON_OFFLINE": "INA226持续失联"}
        return f"{labels.get(self._fault, '未知故障')}（{self._fault}）"

    @Property("QVariantList", notify=telemetryChanged)
    def historyVoltage(self) -> list[float]:
        return list(self._history_v)

    @Property("QVariantList", notify=telemetryChanged)
    def historyCurrent(self) -> list[float]:
        return list(self._history_i)

    @Property(str, notify=telemetryChanged)
    def controlDiagnostic(self) -> str:
        return self._control_diagnostic

    @Property(str, notify=telemetryChanged)
    def voltageWindowText(self) -> str:
        if not self._history_v:
            return "等待采样"
        low, high = min(self._history_v), max(self._history_v)
        return f"最小 {low:.3f} V · 最大 {high:.3f} V · 峰峰值 {(high-low)*1000:.1f} mV"

    @Property(str, notify=telemetryChanged)
    def telemetryRateText(self) -> str:
        return self._telemetry_rate_text

    @Property(str, notify=logsChanged)
    def logText(self) -> str:
        return self._paused_log_text if self._log_paused else "\n".join(self._logs)

    @Property(bool, notify=logStateChanged)
    def logPaused(self) -> bool:
        return self._log_paused

    @Property(int, notify=logStateChanged)
    def pendingLogCount(self) -> int:
        return self._pending_log_count

    @Property(str, notify=diagnosticsChanged)
    def voltageDiagnostic(self) -> str:
        return self._voltage_diagnostic

    @Property(str, notify=updateChanged)
    def imagePath(self) -> str:
        return self._image_path

    @Property(int, notify=updateChanged)
    def updateProgress(self) -> int:
        return self._update_progress

    @Property(str, notify=updateChanged)
    def updateStatus(self) -> str:
        return self._update_status

    @Property(bool, notify=updateChanged)
    def updating(self) -> bool:
        return self._updating

    @Property(str, notify=calibrationChanged)
    def calibrationStatus(self) -> str:
        return self._calibration_status

    def _record_session(self, text: str) -> None:
        stamp=datetime.now().isoformat(sep=" ",timespec="milliseconds")
        self._session_lines.append(stamp+"  "+text)
        self._record.write(dict(time=stamp,text=text))

    def _log(self, text: str) -> None:
        self._record_session(text)
        self._logs.append(f"{time.strftime('%H:%M:%S')}  {text}")
        self._log_dirty = True
        if self._log_paused:
            self._pending_log_count += 1

    def _flush_log_view(self) -> None:
        if not self._log_dirty:
            return
        self._log_dirty = False
        if self._log_paused:
            self.logStateChanged.emit()
        else:
            self.logsChanged.emit()

    @Slot()
    def toggleLogPause(self) -> None:
        if not self._log_paused:
            self._paused_log_text = "\n".join(self._logs)
        self._log_paused = not self._log_paused
        self._pending_log_count = 0
        self._log_dirty = False
        self.logStateChanged.emit()
        self.logsChanged.emit()

    @Slot()
    def clearLog(self) -> None:
        # Only the log ring/view is cleared. Telemetry history and CSV stay intact.
        self._logs.clear()
        self._paused_log_text = ""
        self._pending_log_count = 0
        self._log_dirty = False
        self.logStateChanged.emit()
        self.logsChanged.emit()

    @Slot(str)
    def runQuickCommand(self, command: str) -> None:
        # Never turn output on, erase calibration or apply firmware from this menu.
        allowed = {"PING", "INFO", "STATUS", "CAL GET", "CAL RAW", "DIAG",
                   "FW STATUS", "HELP", "LOG ON", "LOG OFF"}
        if self._connected and not self._updating and command in allowed:
            self.sendCommand(command)

    @Slot(int)
    def setTelemetryInterval(self, milliseconds: int) -> None:
        if self._connected and not self._updating and milliseconds in (20,40,100,200):
            self.sendCommand(f"LOG RATE {milliseconds}")

    @Slot()
    def scanPorts(self) -> None:
        if self._scanning or self._updating or (self._worker and not self._connected):
            return
        self._scanning = True
        self._scan_text = "正在枚举系统串口…"
        self._scan_thread = ProbeScanThread(
            self._selected_port if self._connected else "",
            self._firmware if self._connected else "",
        )
        self._scan_thread.completed.connect(self._on_scan_complete)
        self._scan_thread.failed.connect(self._on_scan_failed)
        self._scan_thread.start()
        self.stateChanged.emit()

    def _on_scan_complete(self, entries):
        self._all_ports=list(entries);self._scanning=False
        self._filter_ports()
        thread=self._scan_thread
        if thread:
            thread.finished.connect(thread.deleteLater)
            if thread.isFinished():thread.deleteLater()
        self._scan_thread=None

    def _on_scan_failed(self, reason):
        self._scanning=False;self._scan_text='扫描失败：'+reason
        self._log(self._scan_text);self.stateChanged.emit()
        thread=self._scan_thread
        if thread:
            thread.finished.connect(thread.deleteLater)
            if thread.isFinished():thread.deleteLater()
        self._scan_thread=None

    @Slot(str)
    def selectPort(self, label: str) -> None:
        chosen = label.split()[0] if label else ""
        if self._worker and chosen != self._selected_port:
            return
        if chosen != self._selected_port and not self._connected:
            self._reconnect_pending = False
            self._firmware = "—"
            self._hardware = "—"
        self._selected_port = chosen
        self.stateChanged.emit()

    @Slot()
    def connectDevice(self) -> None:
        if self._worker or self._updating or not self._selected_port:
            return
        self._telemetry_arrivals.clear()
        self._telemetry_rate_text = "等待遥测"
        self._control_diagnostic = "等待控制状态"
        self.telemetryChanged.emit()
        worker = SerialReader(self._selected_port)
        worker.opened.connect(lambda source=worker:
                              self._log(f"串口已打开：{source.port_name}"))
        worker.stopped.connect(lambda reason, source=worker:
                               self._on_stopped(source, reason))
        worker.upgradeProgress.connect(self._on_update_progress)
        worker.upgradeSucceeded.connect(self._on_update_success)
        worker.upgradeFailed.connect(self._on_update_failure)
        self._worker = worker
        self._connecting = True
        self._connection_text = "正在验证选中设备…"
        self.stateChanged.emit()
        worker.start()

    @Slot()
    def disconnectDevice(self) -> None:
        if self._updating:
            return
        if self.curve.requiresStop:
            self._disconnect_after_calibration = True
            self.curve.stop()
            self._connection_text = "正在关闭校准输出，确认后断开连接…"
            self.stateChanged.emit()
            return
        self._disconnect_after_calibration = False
        self._reconnect_pending = False
        if self._worker:
            worker = self._worker
            worker.stop()
            self._connecting = True
            self._connection_text = "正在断开…"
            self.stateChanged.emit()
            return
        self._connected = False
        self._connection_text = "未连接"
        self._telemetry_arrivals.clear()
        self._telemetry_rate_text = "等待连接"
        self.telemetryChanged.emit()
        self.stateChanged.emit()

    def _on_stopped(self, source: SerialReader, reason: str) -> None:
        if source is not self._worker:
            return
        # Deletion waits for QThread.finished without blocking the GUI event loop.
        source.finished.connect(source.deleteLater)
        if source.isFinished():source.deleteLater()
        self._connecting = False
        self._worker = None
        self._connected = False
        self.curve.connection_changed(False, self._firmware)
        if (reason and not self._reconnect_pending and self._transport_mode == 'wired'
                and not source.stop_event.is_set() and recoverable_serial_failure(reason)):
            self._reconnect_pending = True
            self._reconnect_attempts = 0
            self._reconnect_port = source.port_name
            self._log(f'有线连接中断：{reason}；尝试恢复同一端口 {source.port_name}，不重发输出操作')
        self._telemetry_arrivals.clear()
        self._telemetry_rate_text = "等待连接"
        self.telemetryChanged.emit()
        if self._reconnect_pending:
            if self._reconnect_attempts < 5:
                self._connection_text = f"正在恢复同一端口连接（{self._reconnect_attempts}/5）…"
                self.stateChanged.emit()
                QTimer.singleShot(1800, self._attempt_reconnect)
                return
            self._reconnect_pending = False
            self._connection_text = "自动重连未成功；请扫描后手动连接"
            self._scan_text = "请检查线缆、USB端口和串口驱动后重新连接"
            self.stateChanged.emit()
            self._log(self._connection_text + (f"：{reason}" if reason else ""))
            return
        self._connection_text = "连接已断开" if not reason else f"连接失败：{reason}"
        self.stateChanged.emit()
        if reason:
            self._log(self._connection_text)

    def _attempt_reconnect(self) -> None:
        if not self._reconnect_pending or self._worker or self._updating:
            return
        if self._selected_port != self._reconnect_port:
            self._reconnect_pending = False
            return
        self._reconnect_attempts += 1
        self.connectDevice()

    @Slot(str)
    def sendCommand(self, command: str) -> None:
        command = command.strip()
        if not command or not self._worker or self._updating:
            return
        if len(command) > 96 or any(ord(c) < 32 or ord(c) > 126 for c in command):
            self._log("命令必须是 96 字符以内的 ASCII 文本")
            return
        self._worker.send(command)
        self._log("发送 | " + format_command(command))

    @Slot(str, str)
    def applySetpoints(self, volts: str, amps: str) -> None:
        try:
            v = round(float(volts) * 1000)
            i = round(float(amps) * 1000)
            if not (3200 <= v <= 32000 and 100 <= i <= 8000):
                raise ValueError()
        except ValueError:
            self._log("设定范围：3.200～32.000 V，0.100～8.000 A")
            return
        self.sendCommand(f"SET V {v}")
        self.sendCommand(f"SET I {i}")

    @Slot()
    def toggleOutput(self) -> None:
        self.sendCommand("OUT OFF" if self._output_on else "OUT ON")

    @Slot()
    def clearFault(self) -> None:
        self.sendCommand("CLEAR")

    def _on_line(self, line: str) -> None:
        self._on_frame(parse_line(line))

    def _on_frame(self, frame) -> None:
        line=frame.raw
        if self.live_api is not None:
            self.live_api.capture(frame, line)
        self.curve.on_frame(frame)
        self.pid.on_frame(frame)
        if self._disconnect_after_calibration and frame.kind == "ack" and \
                frame.values.get("cmd") == "CAL_POINT_STOP":
            QTimer.singleShot(0, self.disconnectDevice)
        if frame.kind != "telemetry":
            self._log("接收 | " + format_frame(frame))
        else:
            self._record_session("接收 | " + format_frame(frame))
            if time.monotonic()-self._last_telemetry_log>=.2:
                self._last_telemetry_log=time.monotonic()
                self._logs.append(time.strftime("%H:%M:%S")+"  "+format_frame(frame))
                self._log_dirty=True
                if self._log_paused:self._pending_log_count+=1
        if frame.kind == "ack" and frame.values.get("cmd") == "LOG_RATE":
            self._telemetry_arrivals.clear()
            self._telemetry_rate_text = "正在测量新回传速率…"
            self.telemetryChanged.emit()
        if frame.kind == "info":
            self._firmware = frame.values.get("fw", "—")
            self._hardware = frame.values.get("hw", "—")
            self._connected = True
            self._connecting = False
            self.curve.connection_changed(True, self._firmware)
            self._reconnect_pending = False
            self._reconnect_attempts = 0
            self._verified_ports.add(self._selected_port)
            self._connection_text = f"已连接 XIN Power · {self._selected_port}"
            self.stateChanged.emit()
        elif frame.kind == "telemetry":
            v = frame.values
            self._telemetry_arrivals.append(time.monotonic())
            if len(self._telemetry_arrivals) >= 2:
                elapsed = self._telemetry_arrivals[-1] - self._telemetry_arrivals[0]
                if elapsed > 0:
                    intervals = len(self._telemetry_arrivals) - 1
                    self._telemetry_rate_text = (
                        f"{intervals / elapsed:.1f} Hz · "
                        f"{elapsed * 1000 / intervals:.0f} ms/帧"
                    )
            else:
                self._telemetry_rate_text = "正在测量…"
            voltage = telemetry_number(v, "vout") / 1000.0
            current = telemetry_number(v, "iout") / 1_000_000.0
            self._voltage = f"{voltage:.3f}"
            self._current = f"{current:.3f}"
            self._input_voltage = f"{telemetry_number(v, 'vin') / 1000:.3f}"
            self._power = f"{telemetry_number(v, 'pout') / 1000:.2f}"
            self._temperature = f"{telemetry_number(v, 'temp') / 10:.1f}" if v.get("tempvalid","1")=="1" else "—"
            self._vpp=v.get("vpp","—") if v.get("vppvalid")=="1" else "—"
            self._set_voltage = f"{telemetry_number(v, 'setv') / 1000:.3f}"
            self._set_current = f"{telemetry_number(v, 'seti') / 1000:.3f}"
            next_output = v.get("out") == "1"
            next_setpoint = v.get("setv", "")
            history_key = (next_output, next_setpoint)
            if getattr(self, "_history_key", None) != history_key:
                self._history_v.clear()
                self._history_i.clear()
                self._history_key = history_key
            self._output_on = next_output
            self._fault = v.get("fault", "NONE")
            self._control_diagnostic = (
                f"{'输出开启' if next_output else '输出关闭，显示残余实测电压'} · "
                f"DAC {v.get('dac', '—')} · CE GPIO {v.get('cegpio', '—')} · "
                f"DAC 写入 {v.get('dw', '—')} 次 · 最近写入 {v.get('dwt', '—')} ms"+
                (f" · PI{'微调' if v.get('pid')=='1' else '暂停'} / 误差 {v.get('pe','—')} mV" if v.get('trim_available')=='1' else ''))
            fast_voltage = telemetry_number(v, "vfast") / 1000.0 if "vfast" in v else voltage
            self._history_v.append(fast_voltage)
            self._history_i.append(current)
            csv_row=[datetime.now().isoformat(sep=' ',timespec='milliseconds'),v.get('t',''),
                     self._voltage,self._current,self._input_voltage,self._power,self._temperature,
                     v.get('fault',''),v.get('setv',''),v.get('seti',''),v.get('req',''),v.get('out',''),
                     v.get('valid',''),v.get('dac',''),v.get('cegpio',''),v.get('dw',''),v.get('dwt',''),
                     v.get('vraw',''),v.get('vfast',''),v.get('iraw',''),v.get('seq',''),v.get('samplet',''),
                     v.get('vpp',''),v.get('vppvalid',''),v.get('hz',''),v.get('maxgap',''),self._firmware,
                     v.get('pid',''),v.get('pe','')]
            self._telemetry_rows.append(csv_row)
            self._record.write(dict(csv=csv_row))
            if not self._batching:self.telemetryChanged.emit()
            if self._save_pending and not self._output_on:
                gain, offset = self._save_pending
                self._save_pending = None
                self.sendCommand(f"CAL SET V {gain} {offset}")
                self.sendCommand("CAL SAVE")
                self._calibration_status = "两点校准参数已发送并请求保存；请检查 ACK"
                self.calibrationChanged.emit()
        elif frame.kind == "calibration":
            try:
                raw_mv = int(frame.values["vraw"])
                gain = int(frame.values["vgain"])
                offset_mv = int(frame.values["voff"])
                calibrated_mv = max(0, min(65535, (raw_mv * gain + 500000) // 1000000 + offset_mv))
                self._voltage_diagnostic = (
                    f"原始 {raw_mv} mV × 增益 {gain / 1000000:.6f} "
                    f"+ 偏移 {offset_mv} mV → {calibrated_mv / 1000:.3f} V。"
                    "关闭输出时请与万用表实测对照；两点校准不代表零点已校准。"
                )
            except (KeyError, ValueError):
                self._voltage_diagnostic = "校准回复缺少原始电压、增益或偏移，请重新读取。"
            self.diagnosticsChanged.emit()
            if not self._capture_pending:
                return
            index, actual = self._capture_pending
            self._capture_pending = None
            try:
                raw = int(frame.values["vraw"])
            except (KeyError, ValueError):
                self._calibration_status = "设备未返回有效 vraw，请重试"
            else:
                self._calibration_raw[index] = raw
                self._calibration_actual[index] = actual
                self._calibration_status = f"已采集点 {index}：原始 {raw} mV，实测 {actual:.3f} V"
                if index == 1:
                    self.sendCommand("SET V 20000")
                    self._calibration_status += "；已切换 20 V，请等待稳定"
            self.calibrationChanged.emit()

    @Slot()
    def prepareCalibration(self) -> None:
        if not self._connected or self._updating or self.curve.active or self.curve.busy:
            return
        self._calibration_raw.clear()
        self._calibration_actual.clear()
        self.sendCommand("SET I 2000")
        self.sendCommand("SET V 5000")
        self.sendCommand("OUT ON")
        self._calibration_status = "已设为 5 V；请用标准表测量，稳定后采集点 1"
        self.calibrationChanged.emit()

    @Slot(int, str)
    def captureCalibration(self, index: int, actual_text: str) -> None:
        if self.curve.active or self.curve.busy or self._updating or not self._connected:
            return
        try:
            actual = float(actual_text)
            if index not in (1, 2) or not (2.0 <= actual <= 32.0):
                raise ValueError()
            if index == 2 and 1 not in self._calibration_raw:
                raise ValueError()
        except ValueError:
            self._calibration_status = "请按顺序输入有效的实测电压（保留三位小数）"
            self.calibrationChanged.emit()
            return
        self._capture_pending = (index, actual)
        self.sendCommand("CAL GET")

    @Slot()
    def saveVoltageCalibration(self) -> None:
        if self.curve.active or self.curve.busy or self._updating or not self._connected:
            return
        try:
            raw1, raw2 = self._calibration_raw[1], self._calibration_raw[2]
            actual1, actual2 = self._calibration_actual[1], self._calibration_actual[2]
            if raw2 - raw1 < 1000:
                raise ValueError("原始电压跨度不足")
            gain = round((actual2 - actual1) * 1_000_000_000 / (raw2 - raw1))
            offset = round(actual1 * 1000 - raw1 * gain / 1_000_000)
            if not (700000 <= gain <= 1300000 and -1500 <= offset <= 1500):
                raise ValueError("校准系数超出安全范围")
        except KeyError:
            self._calibration_status = "先完成 5 V 和 20 V 两点采集"
        except ValueError as exc:
            self._calibration_status = str(exc)
        else:
            self.sendCommand("OUT OFF")
            self._save_pending = (gain, offset)
            self._calibration_status = "等待设备确认输出关闭，然后写入并保存校准"
        self.calibrationChanged.emit()

    @Slot(result=str)
    def chooseFirmware(self) -> str:
        filename, _ = QFileDialog.getOpenFileName(
            None, "选择应用固件（不含 BL）", "", "固件文件 (*.hex *.bin)"
        )
        if filename:
            self._image_path = filename
            self.updateChanged.emit()
        return filename

    @Slot(str)
    def startUpgrade(self, version: str) -> None:
        if self.curve.active or self.curve.busy:
            self._update_status = "请先停止校准并确认输出关闭，再开始升级"
            self.updateChanged.emit()
            return
        if self._updating or not self._connected or not self._worker or \
                not self._selected_port or not self._image_path:
            self._update_status = "请先连接并识别 XIN Power，再选择应用固件文件"
            self.updateChanged.emit()
            return
        transport = ("bluetooth" if self._port_types.get(self._selected_port) == "BLUETOOTH SPP"
                     else "usb")
        if transport == "bluetooth" and self._firmware == "5.9.0":
            self._update_status = "设备仍为 5.9.0：请先用 USB 升到 5.9.1；当前连接保持不变"
            self.updateChanged.emit()
            return
        try:
            image = load_firmware(self._image_path, version)
        except (OSError, ValueError) as exc:
            self._update_status = f"固件文件不合格：{exc}"
            self.updateChanged.emit()
            return
        self._updating = True
        self._update_progress = 0
        self._update_status = "正在使用当前已连接的端口启动升级；不会先断开蓝牙"
        self._connection_text = f"已连接 · {self._selected_port} · 升级准备中"
        self.stateChanged.emit()
        self.updateChanged.emit()
        self._worker.begin_upgrade(image, transport)

    @Slot()
    def cancelUpgrade(self) -> None:
        if self._worker and self._updating and self._update_progress < 82:
            self._worker.cancel_upgrade()
            self._update_status = "正在请求取消；请等待设备确认"
            self.updateChanged.emit()

    def _on_update_progress(self, percent: int, message: str) -> None:
        self._update_progress = percent
        self._update_status = message
        if percent < 90:
            self._connection_text = f"已连接 · {self._selected_port} · 升级中 {percent}%"
        else:
            self._connected = False
            self._connection_text = "设备刷写／重启中，等待版本确认"
        self.stateChanged.emit()
        self.updateChanged.emit()

    def _on_update_success(self, version: str) -> None:
        self._updating = False
        self._update_progress = 100
        self._update_status = f"升级成功 · V{version}；正在恢复日常监控连接"
        self._connected = False
        self._reconnect_pending = True
        self._reconnect_attempts = 0
        self._reconnect_port = self._selected_port
        self._connection_text = "升级成功，正在恢复连接…"
        self.stateChanged.emit()
        self.updateChanged.emit()
        self._log(self._update_status)

    def _on_update_failure(self, reason: str, port_still_open: bool) -> None:
        self._updating = False
        if port_still_open:
            self._update_status = f"升级未完成：{reason}。设备连接仍在，输出保持关闭。"
            self._connection_text = f"已连接 · {self._selected_port} · 升级已停止"
        else:
            self._update_status = f"升级结果待确认：{reason}。正在重新连接查看设备版本。"
            self._connected = False
            self._reconnect_pending = True
            self._reconnect_attempts = 0
            self._reconnect_port = self._selected_port
            self._connection_text = "设备重启／端口恢复中…"
        self.stateChanged.emit()
        self.updateChanged.emit()
        self._log(self._update_status)

    @Slot()
    def exportLog(self) -> None:
        filename, _ = QFileDialog.getSaveFileName(None, "导出日志", "xin_power.log", "日志 (*.log)")
        if filename:
            # Export the live ring even when the on-screen view is paused.
            self._record.flush()
            with self._record.path.open(encoding='utf-8') as src,open(filename,'w',encoding='utf-8') as dst:
                for line in src:
                    row=json.loads(line)
                    if 'text' in row:dst.write(row['time']+'  '+row['text']+'\n')

    @Slot()
    def exportCsv(self) -> None:
        filename, _ = QFileDialog.getSaveFileName(None, "导出遥测", "xin_power.csv", "CSV (*.csv)")
        if filename:
            with open(filename, "w", encoding="utf-8-sig", newline="") as handle:
                writer = csv.writer(handle)
                writer.writerow(['电脑时间','设备毫秒','输出电压V','输出电流A','输入电压V',
                                 '输出功率W','温度C','故障','设定电压mV','关断电流mA',
                                 '输出请求','输出开启','采样有效','DAC','CE引脚电平','DAC写入次数',
                                 '最近DAC写入设备毫秒','未校准电压mV','未滤波校准电压mV',
                                 '未校准电流uA','采样序号','采样设备毫秒','低频波动Vpp_mV',
                                 '波动有效','采样Hz','最大采样间隔ms','固件版本','PI微调启用','电压误差mV'])
                self._record.flush()
                with self._record.path.open(encoding='utf-8') as src:
                    for line in src:
                        row=json.loads(line)
                        if 'csv' in row:writer.writerow(row['csv'])


def main() -> int:
    parser = argparse.ArgumentParser(description='XIN Power Studio with optional local PID API')
    parser.add_argument('--control-api-file')
    parser.add_argument('--control-record')
    parser.add_argument('--control-api-port', type=int, default=0)
    parser.add_argument('--connect-port')
    parser.add_argument('--smoke-test', action='store_true')
    parser.add_argument('--smoke-output')
    args = parser.parse_args()
    # The GUI launcher cannot pass command-line options. A local settings file
    # explicitly enables the same API when this executable is opened normally.
    settings_path=(Path(sys.executable).parent if getattr(sys,'frozen',False) else Path(__file__).parent)/'realtime_settings.json'
    if settings_path.exists() and not args.smoke_test:
        settings=json.loads(settings_path.read_text(encoding='utf-8'))
        if settings.get('enabled'):
            args.control_api_file=args.control_api_file or str(settings_path.parent/'live_api.json')
            args.control_record=args.control_record or str(settings_path.parent/'live_session_gui.jsonl')

    # Native Windows controls do not support our custom backgrounds/content items.
    if args.smoke_test:
        os.environ["QT_QPA_PLATFORM"]="offscreen"
        os.environ["QT_QUICK_BACKEND"]="software"
    QQuickStyle.setStyle("Basic")
    app = QApplication([])
    if args.smoke_test:
        # The Windows offscreen plugin has no system font catalogue. Load the
        # same installed fonts used by the normal desktop platform for QA.
        from PySide6.QtGui import QFontDatabase
        font_dir = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts'
        for name in ('msyh.ttc', 'consola.ttf', 'seguisym.ttf', 'segoeui.ttf'):
            QFontDatabase.addApplicationFont(str(font_dir / name))
    bridge = Bridge()
    if args.control_api_file:
        from live_control import LiveControl
        record=args.control_record or str(Path(args.control_api_file).with_suffix('.jsonl'))
        bridge.live_api=LiveControl(bridge,args.control_api_file,record,args.control_api_port)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("bridge", bridge)
    engine.rootContext().setContextProperty("curve", bridge.curve)
    engine.rootContext().setContextProperty("pid", bridge.pid)
    engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("qml") / "Main.qml")))
    if not engine.rootObjects():
        if bridge.live_api: bridge.live_api.close()
        return 1
    if args.connect_port and not args.smoke_test:
        bridge.selectPort(args.connect_port)
        QTimer.singleShot(300,bridge.connectDevice)
    if bridge.live_api:
        engine.rootObjects()[0].setProperty('activePage',0)
        # A CLI launcher may hide its initial window; show the requested GUI after startup.
        def show_control_window():
            root=engine.rootObjects()[0]
            root.hide()
            root.show()
        QTimer.singleShot(500, show_control_window)
    def shutdown():
        if bridge.live_api:
            bridge.live_api.close()
        if bridge.curve.requiresStop:
            bridge.sendCommand("CAL POINT STOP")
            # Wait briefly for actual STOP acknowledgement while the background
            # reader continues. A broken link must not be reported as confirmed.
            confirmed=False;deadline=time.monotonic()+1.5
            while bridge._worker and time.monotonic()<deadline:
                try:frame=bridge._worker.frames.get(timeout=.05)
                except Empty:continue
                if frame.kind=='ack' and frame.values.get('cmd')=='CAL_POINT_STOP':
                    confirmed=True;break
            bridge._log('校准关断已确认' if confirmed else '关闭程序时校准关断未收到确认')
        bridge.curve.connection_changed(False, "")
        bridge.disconnectDevice()
        if bridge._worker:
            bridge._worker.stop();bridge._worker.wait(5000)
        if bridge._scan_thread:bridge._scan_thread.wait(2000)
        bridge._record.close()
    app.aboutToQuit.connect(shutdown)
    if args.smoke_test:
        def smoke_finish():
            root=engine.rootObjects()[0]
            if args.smoke_output:root.grabWindow().save(args.smoke_output)
            app.quit()
        QTimer.singleShot(400,smoke_finish)
    code = app.exec()
    bridge.disconnectDevice()
    return code


if __name__ == "__main__":
    raise SystemExit(main())
