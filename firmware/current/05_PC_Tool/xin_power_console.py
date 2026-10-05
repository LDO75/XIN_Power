"""XIN Power V5.8.6 双通道仪器控制台。

USB 转串口与 VG6328A 经典蓝牙 SPP 共用同一套协议。本界面强调设备
自动识别、实时仪表监控，以及与整机风格一致的调试终端。
"""

from __future__ import annotations

import csv
import queue
import threading
import time
import tkinter as tk
from collections import deque
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # 缺少依赖时仍显示明确提示，而不是直接闪退。
    serial = None
    list_ports = None

from device_link import PortDescriptor, ProbeResult, describe_ports, probe_xin_power
from protocol import (
    ParsedFrame,
    calculate_calibration,
    current_command,
    parse_line,
    telemetry_number,
    voltage_command,
)


APP_TITLE = "XIN Power Control Studio · V5.8.6"
BAUD_RATE = 115200
POLL_INTERVAL_MS = 35
HISTORY_LENGTH = 300

COLOR = {
    "bg": "#050914",
    "panel": "#0A1220",
    "panel_2": "#0D1728",
    "panel_3": "#101D31",
    "border": "#1B3654",
    "grid": "#142A42",
    "cyan": "#18D7FF",
    "cyan_dim": "#0B7591",
    "blue": "#3B82F6",
    "amber": "#FFB547",
    "green": "#34E7A2",
    "red": "#FF5D73",
    "text": "#DCEBFA",
    "muted": "#7890A8",
    "muted_2": "#40566E",
    "terminal": "#03060C",
}

UI_FONT = "Microsoft YaHei UI"
MONO_FONT = "Cascadia Mono"


class SerialWorker:
    """后台串口读取器；USB COM 与蓝牙 SPP COM 共用此实现。"""

    def __init__(self, event_queue: queue.Queue[tuple[str, object]]) -> None:
        self.event_queue = event_queue
        self.port = None
        self.thread: threading.Thread | None = None
        self.running = threading.Event()
        self.write_lock = threading.Lock()

    @property
    def connected(self) -> bool:
        return bool(self.port and self.port.is_open and self.running.is_set())

    @staticmethod
    def open_port(port_name: str) -> object:
        if serial is None:
            raise RuntimeError("缺少 pyserial，请先运行 run_windows.bat 或安装 requirements.txt")
        return serial.Serial(
            port=port_name,
            baudrate=BAUD_RATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.12,
            write_timeout=0.8,
        )

    def connect(self, port_name: str) -> None:
        self.disconnect()
        self.attach_port(self.open_port(port_name))

    def attach_port(self, port: object) -> None:
        self.disconnect()
        running = threading.Event()
        running.set()
        self.port = port
        self.running = running
        self.thread = threading.Thread(target=self._reader, args=(port, running), daemon=True)
        self.thread.start()

    def disconnect(self) -> None:
        self.running.clear()
        port = self.port
        self.port = None
        if port is not None:
            try:
                port.close()
            except Exception:
                pass

    def send(self, command: str) -> None:
        if not self.connected or self.port is None:
            raise RuntimeError("设备尚未连接")
        payload = (command.strip() + "\r\n").encode("ascii", errors="strict")
        with self.write_lock:
            self.port.write(payload)
            self.port.flush()

    def _reader(self, port: object, running: threading.Event) -> None:
        buffer = bytearray()
        reported = False
        try:
            while running.is_set() and port.is_open:
                chunk = port.read(256)
                if not chunk:
                    continue
                buffer.extend(chunk)
                while b"\n" in buffer:
                    line, _, remainder = buffer.partition(b"\n")
                    buffer = bytearray(remainder)
                    text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                    self.event_queue.put(("line", (port, text)))
        except Exception as exc:
            if running.is_set():
                self.event_queue.put(("disconnect", (port, str(exc))))
                reported = True
        finally:
            if running.is_set() and not reported:
                self.event_queue.put(("disconnect", (port, "串口已关闭")))
            # 旧读取线程不得清除快速重连后的新会话状态。
            if self.port is port and self.running is running:
                running.clear()


def make_panel(parent: tk.Misc, inner_bg: str | None = None) -> tuple[tk.Frame, tk.Frame]:
    """创建统一的一像素描边仪器面板。"""

    shell = tk.Frame(parent, background=COLOR["border"], borderwidth=0)
    inner = tk.Frame(shell, background=inner_bg or COLOR["panel"])
    inner.pack(fill="both", expand=True, padx=1, pady=1)
    return shell, inner


class StatusPill(tk.Frame):
    """带状态点的紧凑信息胶囊。"""

    def __init__(self, master: tk.Misc, text: str, color: str = COLOR["muted"]) -> None:
        super().__init__(
            master,
            background=COLOR["panel_2"],
            highlightbackground=COLOR["border"],
            highlightthickness=1,
        )
        self.dot = tk.Canvas(self, width=13, height=13, background=COLOR["panel_2"], highlightthickness=0)
        self.dot.pack(side="left", padx=(9, 3), pady=7)
        self.dot_id = self.dot.create_oval(3, 3, 10, 10, fill=color, outline="")
        self.variable = tk.StringVar(value=text)
        tk.Label(
            self,
            textvariable=self.variable,
            background=COLOR["panel_2"],
            foreground=COLOR["text"],
            font=(MONO_FONT, 9),
        ).pack(side="left", padx=(0, 10))

    def set(self, text: str, color: str) -> None:
        self.variable.set(text)
        self.dot.itemconfigure(self.dot_id, fill=color)


class MetricCard(tk.Frame):
    """仪器读数卡，数值和单位分离以保持视觉稳定。"""

    def __init__(
        self,
        master: tk.Misc,
        title: str,
        initial: str,
        unit: str,
        accent: str,
        hero: bool = False,
    ) -> None:
        super().__init__(master, background=COLOR["border"])
        inner = tk.Frame(self, background=COLOR["panel_2"])
        inner.pack(fill="both", expand=True, padx=1, pady=1)
        tk.Frame(inner, background=accent, height=2).pack(fill="x")
        top = tk.Frame(inner, background=COLOR["panel_2"])
        top.pack(fill="x", padx=15, pady=(11, 1))
        tk.Label(
            top,
            text=title.upper(),
            background=COLOR["panel_2"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 9, "bold"),
        ).pack(side="left")
        tk.Label(
            top,
            text="● LIVE",
            background=COLOR["panel_2"],
            foreground=accent,
            font=(MONO_FONT, 8),
        ).pack(side="right")
        reading = tk.Frame(inner, background=COLOR["panel_2"])
        reading.pack(fill="both", expand=True, padx=14, pady=(0, 12))
        self.variable = tk.StringVar(value=initial)
        tk.Label(
            reading,
            textvariable=self.variable,
            background=COLOR["panel_2"],
            foreground=accent,
            font=(MONO_FONT, 33 if hero else 21, "bold"),
            anchor="w",
        ).pack(side="left", fill="both", expand=True)
        tk.Label(
            reading,
            text=unit,
            background=COLOR["panel_2"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 12 if hero else 10, "bold"),
        ).pack(side="right", anchor="s", pady=(0, 5))


class TrendCanvas(tk.Canvas):
    """与面板主题一致的 VOUT/IOUT 双轨滚动趋势图。"""

    def __init__(self, master: tk.Misc, **kwargs: object) -> None:
        super().__init__(master, background=COLOR["panel"], highlightthickness=0, **kwargs)
        self.voltage: deque[float] = deque(maxlen=HISTORY_LENGTH)
        self.current: deque[float] = deque(maxlen=HISTORY_LENGTH)
        self.bind("<Configure>", lambda _event: self.redraw())

    def append(self, voltage_v: float, current_a: float) -> None:
        self.voltage.append(voltage_v)
        self.current.append(current_a)
        self.redraw()

    def clear(self) -> None:
        self.voltage.clear()
        self.current.clear()
        self.redraw()

    def redraw(self) -> None:
        self.delete("all")
        width = max(self.winfo_width(), 180)
        height = max(self.winfo_height(), 120)
        left, right, top, bottom = 45, 22, 30, 28
        plot_w = width - left - right
        plot_h = height - top - bottom
        for index in range(6):
            x = left + plot_w * index / 5
            self.create_line(x, top, x, height - bottom, fill=COLOR["grid"])
        for index in range(5):
            y = top + plot_h * index / 4
            self.create_line(left, y, width - right, y, fill=COLOR["grid"])
        self.create_text(
            left,
            14,
            text="OUTPUT TREND",
            fill=COLOR["muted"],
            anchor="w",
            font=(MONO_FONT, 9, "bold"),
        )
        self.create_text(left + 116, 14, text="● VOUT", fill=COLOR["cyan"], anchor="w", font=(MONO_FONT, 8))
        self.create_text(left + 180, 14, text="● IOUT", fill=COLOR["amber"], anchor="w", font=(MONO_FONT, 8))
        if len(self.voltage) < 2:
            self.create_text(
                width / 2,
                height / 2,
                text="WAITING FOR TELEMETRY",
                fill=COLOR["muted_2"],
                font=(MONO_FONT, 10),
            )
            return

        max_v = max(max(self.voltage) * 1.08, 1.0)
        max_i = max(max(self.current) * 1.08, 0.2)

        def points(values: deque[float], maximum: float) -> list[float]:
            result: list[float] = []
            count = len(values)
            for index, value in enumerate(values):
                x = left + plot_w * index / max(count - 1, 1)
                y = height - bottom - plot_h * max(value, 0.0) / maximum
                result.extend((x, y))
            return result

        self.create_line(
            *points(self.voltage, max_v), fill=COLOR["cyan"], width=1,
            smooth=True, splinesteps=24,
        )
        self.create_line(
            *points(self.current, max_i), fill=COLOR["amber"], width=1,
            smooth=True, splinesteps=24,
        )
        self.create_text(
            width - right,
            14,
            text=f"{max_v:.1f} V  /  {max_i:.2f} A",
            fill=COLOR["text"],
            anchor="e",
            font=(MONO_FONT, 9),
        )


class SettingsDialog(tk.Toplevel):
    """高级连接选项与输出自动校准向导。"""

    def __init__(self, app: "PowerConsoleApp") -> None:
        super().__init__(app)
        self.app = app
        self.title("XIN Power 设置")
        self.geometry("780x690")
        self.minsize(720, 630)
        self.configure(background=COLOR["bg"])
        self.transient(app)
        self.cal_step = "idle"
        self.cal_data: dict[str, float] = {}
        self.actual_var = tk.StringVar()
        self.status_var = tk.StringVar(value="连接设备后，可开始校准。")
        self.step_var = tk.StringVar(value="READY")
        self._build()
        self.sync_ports()

    def _build(self) -> None:
        root = tk.Frame(self, background=COLOR["bg"])
        root.pack(fill="both", expand=True, padx=16, pady=16)

        link_shell, link = make_panel(root, COLOR["panel_2"])
        link_shell.pack(fill="x")
        tk.Label(link, text="CONNECTION · ADVANCED", background=COLOR["panel_2"],
                 foreground=COLOR["cyan"], font=(MONO_FONT, 10, "bold")).grid(
            row=0, column=0, columnspan=4, sticky="w", padx=15, pady=(13, 7))
        link.columnconfigure(0, weight=1)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(link, textvariable=self.port_var, state="readonly")
        self.port_combo.grid(row=1, column=0, sticky="ew", padx=(15, 7), pady=(0, 13))
        ttk.Button(link, text="刷新", command=self._refresh).grid(row=1, column=1, pady=(0, 13))
        ttk.Button(link, text="连接所选", style="Primary.TButton",
                   command=self._connect_selected).grid(row=1, column=2, padx=7, pady=(0, 13))
        ttk.Button(link, text="蓝牙说明", command=self.app.show_bluetooth_guide).grid(
            row=1, column=3, padx=(0, 15), pady=(0, 13))

        cal_shell, cal = make_panel(root, COLOR["panel"])
        cal_shell.pack(fill="both", expand=True, pady=(13, 0))
        tk.Label(cal, text="OUTPUT AUTO CALIBRATION", background=COLOR["panel"],
                 foreground=COLOR["amber"], font=(MONO_FONT, 12, "bold")).pack(
            anchor="w", padx=18, pady=(16, 3))
        tk.Label(
            cal,
            text="REF3030 + INA226 + DAC 两点校准。向导自动切换输出、采样、计算并保存；"
                 "实际值必须来自万用表/电子负载。",
            background=COLOR["panel"], foreground=COLOR["muted"],
            font=(UI_FONT, 10), wraplength=700, justify="left",
        ).pack(anchor="w", padx=18, pady=(0, 13))

        stage = tk.Frame(cal, background=COLOR["panel_2"],
                         highlightbackground=COLOR["border"], highlightthickness=1)
        stage.pack(fill="x", padx=18)
        tk.Label(stage, textvariable=self.step_var, background=COLOR["panel_2"],
                 foreground=COLOR["cyan"], font=(MONO_FONT, 11, "bold")).pack(
            anchor="w", padx=15, pady=(12, 4))
        tk.Label(stage, textvariable=self.status_var, background=COLOR["panel_2"],
                 foreground=COLOR["text"], font=(UI_FONT, 10), wraplength=670,
                 justify="left").pack(anchor="w", padx=15, pady=(0, 12))

        input_row = tk.Frame(cal, background=COLOR["panel"])
        input_row.pack(fill="x", padx=18, pady=14)
        tk.Label(input_row, text="标准仪表实际值", background=COLOR["panel"],
                 foreground=COLOR["muted"], font=(UI_FONT, 10)).pack(side="left")
        ttk.Entry(input_row, textvariable=self.actual_var, width=16).pack(
            side="left", padx=(10, 7))
        self.unit_label = tk.Label(input_row, text="V / A", background=COLOR["panel"],
                                   foreground=COLOR["cyan"], font=(MONO_FONT, 10, "bold"))
        self.unit_label.pack(side="left")
        tk.Label(input_row, text="电压校准支持 0.001 V 输入", background=COLOR["panel"],
                 foreground=COLOR["muted"], font=(UI_FONT, 9)).pack(side="left", padx=(18, 0))

        buttons = tk.Frame(cal, background=COLOR["panel"])
        buttons.pack(fill="x", padx=18)
        self.start_button = ttk.Button(buttons, text="开始自动校准", style="Primary.TButton",
                                       command=self.start_calibration)
        self.start_button.pack(side="left")
        self.capture_button = ttk.Button(buttons, text="记录并继续", state="disabled",
                                         command=self.capture_step)
        self.capture_button.pack(side="left", padx=8)
        ttk.Button(buttons, text="读取当前校准", command=lambda: app.send_command("CAL GET")).pack(
            side="right")

        notice = (
            "顺序：关闭输出并采零点 → 5V 电压点 → 20V 电压点 → 5V 带载电流点。"
            "带载步骤建议 0.5–1.5A；保存前固件会再次确认输出已经关闭。"
        )
        tk.Label(cal, text=notice, background=COLOR["panel"], foreground=COLOR["muted_2"],
                 font=(UI_FONT, 9), wraplength=700, justify="left").pack(
            anchor="w", padx=18, pady=(18, 12))

    def sync_ports(self) -> None:
        values = list(self.app.port_lookup.keys())
        self.port_combo["values"] = values
        selected = self.app.port_var.get()
        if selected in values:
            self.port_var.set(selected)
        elif values:
            self.port_var.set(values[0])

    def _refresh(self) -> None:
        self.app.refresh_ports()
        self.sync_ports()

    def _connect_selected(self) -> None:
        selected = self.port_var.get()
        if selected:
            self.app.port_var.set(selected)
        self.app.connect_selected()
        self.after(200, self.sync_ports)

    def _average_raw(self) -> tuple[float, float, float]:
        samples = list(self.app.telemetry_history)[-10:]
        valid = [item for item in samples if item.get("valid", "0") == "1"]
        if len(valid) < 3:
            raise ValueError("有效遥测样本不足，请等待约 1 秒后重试。")
        vraw = sum(telemetry_number(item, "vraw") for item in valid) / len(valid)
        iraw = sum(telemetry_number(item, "iraw") for item in valid) / len(valid)
        dac = sum(telemetry_number(item, "dac") for item in valid) / len(valid)
        return vraw, iraw, dac

    def start_calibration(self) -> None:
        if not self.app.link_verified:
            messagebox.showwarning("尚未连接", "请先在主界面扫描、选择端口并连接设备。", parent=self)
            return
        if not messagebox.askokcancel(
            "开始校准",
            "请确认已换成 3.0V 参考源，并准备好万用表和电子负载。\n\n"
            "校准期间不要手动操作输出。继续吗？",
            parent=self,
        ):
            return
        self.cal_data.clear()
        self.cal_step = "zero"
        self.step_var.set("STEP 0 / 3 · ZERO CURRENT")
        self.status_var.set("正在关闭输出并采集 INA226 电流零点……")
        self.start_button.configure(state="disabled")
        self.capture_button.configure(state="disabled")
        self.app.send_command("OUT OFF")
        self.after(1200, self._prepare_first_voltage)

    def _prepare_first_voltage(self) -> None:
        try:
            _, iraw, _ = self._average_raw()
        except ValueError as exc:
            self.status_var.set(str(exc))
            self.after(800, self._prepare_first_voltage)
            return
        self.cal_data["zero_iraw"] = iraw
        self.app.send_command("CAL RESET")
        self.app.send_command("SET I 2000")
        self.app.send_command("SET V 5000")
        self.app.send_command("OUT ON")
        self.cal_step = "v5"
        self.step_var.set("STEP 1 / 3 · 5V POINT")
        self.status_var.set("已设置 5.000V。等待稳定后，将万用表实际电压填入下方（单位 V）。")
        self.unit_label.configure(text="V")
        self.actual_var.set("")
        self.after(1500, lambda: self.capture_button.configure(state="normal"))

    def _read_actual(self, unit: str) -> float:
        try:
            value = float(self.actual_var.get().strip())
        except ValueError as exc:
            raise ValueError(f"请输入有效的实际{unit}数值。") from exc
        if value <= 0:
            raise ValueError(f"实际{unit}必须大于 0。")
        return value

    def capture_step(self) -> None:
        try:
            vraw, iraw, dac = self._average_raw()
            if self.cal_step == "v5":
                actual = self._read_actual("电压")
                if not 3.5 <= actual <= 6.5:
                    raise ValueError("5V 校准点实际值应在 3.5–6.5V。")
                self.cal_data.update(v1=actual * 1000.0, raw_v1=vraw, code1=dac)
                self.app.send_command("SET V 20000")
                self.cal_step = "v20"
                self.step_var.set("STEP 2 / 3 · 20V POINT")
                self.status_var.set("已设置 20.000V。等待稳定后，填入万用表实际电压（单位 V）。")
                self.actual_var.set("")
                self.capture_button.configure(state="disabled")
                self.after(1800, lambda: self.capture_button.configure(state="normal"))
            elif self.cal_step == "v20":
                actual = self._read_actual("电压")
                if not 16.0 <= actual <= 24.0:
                    raise ValueError("20V 校准点实际值应在 16–24V。")
                self.cal_data.update(v2=actual * 1000.0, raw_v2=vraw, code2=dac)
                self.app.send_command("SET V 5000")
                self.app.send_command("SET I 2000")
                self.cal_step = "current"
                self.step_var.set("STEP 3 / 3 · LOAD CURRENT")
                self.status_var.set(
                    "已回到 5V / 2A 限流。请接入 0.5–1.5A 稳定负载，填入电子负载/电流表实际电流（单位 A）。"
                )
                self.unit_label.configure(text="A")
                self.actual_var.set("")
                self.capture_button.configure(state="disabled")
                self.after(1800, lambda: self.capture_button.configure(state="normal"))
            elif self.cal_step == "current":
                actual = self._read_actual("电流")
                if not 0.2 <= actual <= 1.8:
                    raise ValueError("带载校准电流建议并限制在 0.2–1.8A。")
                self.cal_data.update(actual_i=actual * 1_000_000.0, raw_i=iraw)
                self.capture_button.configure(state="disabled")
                self._finish_calibration()
        except ValueError as exc:
            messagebox.showwarning("无法记录", str(exc), parent=self)

    def _finish_calibration(self) -> None:
        d = self.cal_data
        try:
            result = calculate_calibration(
                d["raw_v1"], d["v1"], d["raw_v2"], d["v2"],
                d["zero_iraw"], d["raw_i"], d["actual_i"],
            )
        except ValueError as exc:
            messagebox.showerror("校准失败", f"{exc}，请检查表笔、负载和 INA226。", parent=self)
            self._abort()
            return
        vgain = result.voltage_gain_ppm
        voff = result.voltage_offset_mv
        igain = result.current_gain_ppm
        ioff = result.current_offset_ua
        v1, v2 = round(d["v1"]), round(d["v2"])
        c1, c2 = round(d["code1"]), round(d["code2"])
        if c1 <= c2:
            messagebox.showerror(
                "校准结果异常",
                f"V gain={vgain}ppm offset={voff}mV\nI gain={igain}ppm offset={ioff}uA\n"
                "结果超出安全范围，未写入。",
                parent=self,
            )
            self._abort()
            return
        self.step_var.set("SAVING · OUTPUT OFF")
        self.status_var.set("校准计算完成，正在关闭输出并写入 Flash……")
        self.app.send_command("OUT OFF")

        def save() -> None:
            self.app.last_calibration = {}
            self.app.send_command(f"CAL SET V {vgain} {voff}")
            self.app.send_command(f"CAL SET I {igain} {ioff}")
            self.app.send_command(f"CAL SET DAC {v1} {c1} {v2} {c2}")
            self.app.send_command("CAL SAVE")
            self.app.send_command("CAL GET")
            self.status_var.set(
                f"正在验证 Flash 回读……电压 {vgain}ppm/{voff:+d}mV，"
                f"电流 {igain}ppm/{ioff:+d}uA。"
            )
            self.after(1200, lambda: self._verify_save(vgain, voff, igain, ioff))

        self.after(1200, save)

    def _verify_save(self, vgain: int, voff: int, igain: int, ioff: int) -> None:
        values = self.app.last_calibration
        if (values.get("saved") == "1" and values.get("valid") == "1" and
                values.get("vvalid", "1") == "1" and
                values.get("ivalid", "1") == "1" and
                values.get("dacvalid") == "1"):
            self.cal_step = "done"
            self.step_var.set("COMPLETE · CALIBRATION SAVED")
            self.status_var.set(
                f"电压 {vgain}ppm / {voff:+d}mV；电流 {igain}ppm / {ioff:+d}uA。"
                "请重新执行 5V 与带载点验证。"
            )
            self.start_button.configure(state="normal")
            return
        self.step_var.set("SAVE FAILED · CHECK TERMINAL")
        self.status_var.set("没有收到有效的 Flash 回读。请检查终端中的 @ERR/CAL_SAVE，再重新校准。")
        self.start_button.configure(state="normal")

    def _abort(self) -> None:
        self.app.send_command("OUT OFF", quiet=True)
        self.cal_step = "idle"
        self.start_button.configure(state="normal")
        self.capture_button.configure(state="disabled")


class PowerConsoleApp(tk.Tk):
    """XIN Power 实时仪表、控制与终端主窗口。"""

    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1420x880")
        self.minsize(1160, 730)
        self.configure(background=COLOR["bg"])

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.worker = SerialWorker(self.events)
        self.port_lookup: dict[str, str] = {}
        self.port_info: dict[str, PortDescriptor] = {}
        self.detected_devices: dict[str, tuple[str, str]] = {}
        self.discovery_running = False
        self.connect_attempting = False
        self.connection_token = 0
        self.app_closing = False
        self.connection_guard = threading.Lock()
        self.link_verified = False
        self.link_timeout_id: str | None = None
        self.log_lines: list[str] = []
        self.last_telemetry: dict[str, str] = {}
        self.telemetry_history: deque[dict[str, str]] = deque(maxlen=80)
        self.last_calibration: dict[str, str] = {}
        self.last_telemetry_time = 0.0
        self.record_file = None
        self.record_writer = None
        self.log_paused = tk.BooleanVar(value=False)
        self.show_telemetry = tk.BooleanVar(value=False)
        self.output_requested = False
        self.connected_device = ""
        self.firmware_version = "--"
        self.protocol_version = "--"

        self._configure_style()
        self._build_ui()
        self.refresh_ports()
        self.after(POLL_INTERVAL_MS, self._poll_events)
        self.after(500, self._update_age)
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("TFrame", background=COLOR["bg"])
        style.configure(
            "TButton",
            background=COLOR["panel_3"],
            foreground=COLOR["text"],
            borderwidth=0,
            focusthickness=0,
            padding=(12, 8),
            font=(UI_FONT, 9, "bold"),
        )
        style.map(
            "TButton",
            background=[("active", "#18304C"), ("disabled", COLOR["panel"])],
            foreground=[("disabled", COLOR["muted_2"])],
        )
        style.configure("Primary.TButton", background=COLOR["cyan_dim"], foreground="#F4FCFF", padding=(14, 9))
        style.map("Primary.TButton", background=[("active", "#1098B8")])
        style.configure("Danger.TButton", background="#8B2940", foreground="white", padding=(14, 9))
        style.map("Danger.TButton", background=[("active", "#B53550")])
        style.configure("OutputOn.TButton", background="#126B4D", foreground="#F4FFF9", padding=(17, 11))
        style.map("OutputOn.TButton", background=[("active", "#188B63")])
        style.configure("OutputOff.TButton", background="#8A2837", foreground="#FFF6F7", padding=(17, 11))
        style.map("OutputOff.TButton", background=[("active", "#AC3547")])
        style.configure(
            "TEntry",
            fieldbackground="#07101D",
            foreground=COLOR["text"],
            insertcolor=COLOR["cyan"],
            bordercolor=COLOR["border"],
            lightcolor=COLOR["border"],
            darkcolor=COLOR["border"],
            padding=8,
            font=(MONO_FONT, 10),
        )
        style.configure(
            "TCombobox",
            fieldbackground="#07101D",
            foreground=COLOR["text"],
            arrowcolor=COLOR["cyan"],
            bordercolor=COLOR["border"],
            lightcolor=COLOR["border"],
            darkcolor=COLOR["border"],
            padding=7,
            font=(MONO_FONT, 9),
        )
        style.map(
            "TCombobox",
            fieldbackground=[("readonly", "#07101D")],
            foreground=[("readonly", COLOR["text"])],
        )
        style.configure("TCheckbutton", background=COLOR["terminal"], foreground=COLOR["muted"], font=(UI_FONT, 9))
        style.map(
            "TCheckbutton",
            background=[("active", COLOR["terminal"])],
            foreground=[("active", COLOR["text"])],
        )
        style.configure(
            "Vertical.TScrollbar",
            background=COLOR["panel_3"],
            troughcolor=COLOR["terminal"],
            bordercolor=COLOR["terminal"],
            arrowcolor=COLOR["muted"],
        )

    def _build_ui(self) -> None:
        root = tk.Frame(self, background=COLOR["bg"])
        root.pack(fill="both", expand=True, padx=16, pady=14)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)

        self._build_header(root)
        self._build_connection_bar(root)

        body = tk.Frame(root, background=COLOR["bg"])
        body.grid(row=2, column=0, sticky="nsew", pady=(12, 0))
        body.columnconfigure(0, weight=7, uniform="body")
        body.columnconfigure(1, weight=4, uniform="body")
        body.rowconfigure(0, weight=1)

        dashboard_shell, dashboard = make_panel(body)
        dashboard_shell.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        terminal_shell, terminal = make_panel(body, COLOR["terminal"])
        terminal_shell.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        self._build_dashboard(dashboard)
        self._build_terminal(terminal)

    def _build_header(self, parent: tk.Misc) -> None:
        header = tk.Frame(parent, background=COLOR["bg"])
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)

        logo = tk.Canvas(header, width=43, height=43, background=COLOR["bg"], highlightthickness=0)
        logo.grid(row=0, column=0, rowspan=2, padx=(0, 11))
        logo.create_rectangle(3, 3, 40, 40, outline=COLOR["cyan_dim"], width=2)
        logo.create_line(11, 30, 20, 12, 25, 23, 33, 9, fill=COLOR["cyan"], width=3)
        logo.create_oval(17, 19, 27, 29, outline=COLOR["amber"], width=2)

        tk.Label(
            header,
            text="XIN POWER",
            background=COLOR["bg"],
            foreground=COLOR["text"],
            font=(MONO_FONT, 19, "bold"),
        ).grid(row=0, column=1, sticky="sw")
        tk.Label(
            header,
            text="CONTROL  /  MONITOR  /  CALIBRATION     V5.8.6",
            background=COLOR["bg"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 8),
        ).grid(row=1, column=1, sticky="nw")

        self.sample_pill = StatusPill(header, "TELEMETRY --", COLOR["muted_2"])
        self.sample_pill.grid(row=0, column=2, rowspan=2, padx=5)
        self.link_pill = StatusPill(header, "OFFLINE", COLOR["red"])
        self.link_pill.grid(row=0, column=3, rowspan=2, padx=(5, 0))

    def _build_connection_bar(self, parent: tk.Misc) -> None:
        shell, connection = make_panel(parent, COLOR["panel_2"])
        shell.grid(row=1, column=0, sticky="ew", pady=(13, 0))
        connection.columnconfigure(1, weight=1)

        tk.Label(
            connection,
            text="设备连接",
            background=COLOR["panel_2"],
            foreground=COLOR["cyan"],
            font=(UI_FONT, 10, "bold"),
        ).grid(row=0, column=0, padx=(16, 12), pady=(12, 6), sticky="w")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(connection, textvariable=self.port_var, state="readonly", width=48)
        self.port_combo.grid(row=0, column=1, sticky="ew", pady=(10, 5))
        self.port_combo.bind("<<ComboboxSelected>>", lambda _event: self._port_selected())
        self.discover_button = ttk.Button(connection, text="扫描设备", command=self.discover_devices)
        self.discover_button.grid(row=0, column=2, padx=(10, 0), pady=(9, 5))
        self.connect_button = ttk.Button(
            connection, text="连接所选", style="Primary.TButton", command=self.toggle_connection,
        )
        self.connect_button.grid(row=0, column=3, padx=(8, 0), pady=(9, 5))
        ttk.Button(connection, text="设置 / 校准", command=self.show_settings).grid(
            row=0, column=4, padx=(8, 14), pady=(9, 5))
        self.device_text = tk.StringVar(value="USB 或蓝牙 SPP：先扫描并选择设备，再点击连接。")
        tk.Label(connection, textvariable=self.device_text, background=COLOR["panel_2"],
                 foreground=COLOR["muted"], font=(UI_FONT, 9), anchor="w").grid(
            row=1, column=0, columnspan=5, sticky="ew", padx=16, pady=(0, 11))

    def show_settings(self) -> None:
        SettingsDialog(self)

    def _build_dashboard(self, parent: tk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        parent.rowconfigure(3, weight=1)

        section = tk.Frame(parent, background=COLOR["panel"])
        section.grid(row=0, column=0, sticky="ew", padx=13, pady=(12, 8))
        tk.Label(
            section,
            text="REAL-TIME INSTRUMENT",
            background=COLOR["panel"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 9, "bold"),
        ).pack(side="left")
        self.mode_text = tk.StringVar(value="OUTPUT OFF  ·  FAULT NONE")
        self.mode_label = tk.Label(
            section,
            textvariable=self.mode_text,
            background=COLOR["panel"],
            foreground=COLOR["green"],
            font=(MONO_FONT, 9, "bold"),
        )
        self.mode_label.pack(side="right")

        hero = tk.Frame(parent, background=COLOR["panel"])
        hero.grid(row=1, column=0, sticky="ew", padx=12)
        hero.columnconfigure(0, weight=1, uniform="hero")
        hero.columnconfigure(1, weight=1, uniform="hero")
        self.cards: dict[str, MetricCard] = {}
        self.cards["vout"] = MetricCard(hero, "Output Voltage", "0.000", "V", COLOR["cyan"], hero=True)
        self.cards["vout"].grid(row=0, column=0, sticky="nsew", padx=(0, 5))
        self.cards["iout"] = MetricCard(hero, "Output Current", "0.000", "A", COLOR["amber"], hero=True)
        self.cards["iout"].grid(row=0, column=1, sticky="nsew", padx=(5, 0))

        sub = tk.Frame(parent, background=COLOR["panel"])
        sub.grid(row=2, column=0, sticky="ew", padx=12, pady=10)
        for column in range(4):
            sub.columnconfigure(column, weight=1, uniform="sub")
        sub_cards = [
            ("vin", "Input", "0.000", "V", COLOR["blue"]),
            ("pout", "Power", "0.000", "W", COLOR["green"]),
            ("temp", "Power Stage", "--.-", "°C", COLOR["red"]),
            ("setpoint", "Setpoint", "5.000", "V", "#B794F4"),
        ]
        for column, (key, title, initial, unit, accent) in enumerate(sub_cards):
            self.cards[key] = MetricCard(sub, title, initial, unit, accent)
            self.cards[key].grid(
                row=0,
                column=column,
                sticky="nsew",
                padx=(0 if column == 0 else 4, 0 if column == 3 else 4),
            )

        lower = tk.Frame(parent, background=COLOR["panel"])
        lower.grid(row=3, column=0, sticky="nsew", padx=12, pady=(0, 12))
        lower.columnconfigure(0, weight=1)
        lower.rowconfigure(1, weight=1)
        self._build_controls(lower)
        trend_shell, trend_inner = make_panel(lower, COLOR["panel"])
        trend_shell.grid(row=1, column=0, sticky="nsew", pady=(10, 0))
        self.trend = TrendCanvas(trend_inner, height=245)
        self.trend.pack(fill="both", expand=True)

    def _build_controls(self, parent: tk.Frame) -> None:
        shell, controls = make_panel(parent, COLOR["panel_2"])
        shell.grid(row=0, column=0, sticky="ew")
        controls.columnconfigure(2, weight=1)
        controls.columnconfigure(5, weight=1)
        tk.Label(
            controls,
            text="输出设定",
            background=COLOR["panel_2"],
            foreground=COLOR["muted"],
            font=(UI_FONT, 9, "bold"),
        ).grid(row=0, column=0, padx=(13, 10), pady=11)
        tk.Label(
            controls,
            text="电压",
            background=COLOR["panel_2"],
            foreground=COLOR["cyan"],
            font=(UI_FONT, 9, "bold"),
        ).grid(row=0, column=1, sticky="e")
        self.voltage_var = tk.StringVar(value="5.000")
        ttk.Entry(controls, textvariable=self.voltage_var, width=10).grid(
            row=0,
            column=2,
            sticky="ew",
            padx=7,
            pady=7,
        )
        ttk.Button(controls, text="设置电压", command=self.set_voltage).grid(row=0, column=3, pady=7)
        tk.Label(
            controls,
            text="限流",
            background=COLOR["panel_2"],
            foreground=COLOR["amber"],
            font=(UI_FONT, 9, "bold"),
        ).grid(row=0, column=4, padx=(18, 0), sticky="e")
        self.current_var = tk.StringVar(value="0.100")
        ttk.Entry(controls, textvariable=self.current_var, width=10).grid(
            row=0,
            column=5,
            sticky="ew",
            padx=7,
            pady=7,
        )
        ttk.Button(controls, text="设置限流", command=self.set_current).grid(row=0, column=6, pady=7)
        self.output_button = ttk.Button(
            controls,
            text="输出关闭",
            style="OutputOff.TButton",
            command=self.toggle_output,
        )
        self.output_button.grid(row=0, column=7, padx=(14, 10), pady=7)

    def _build_terminal(self, parent: tk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        parent.rowconfigure(2, weight=1)

        title = tk.Frame(parent, background=COLOR["terminal"])
        title.grid(row=0, column=0, sticky="ew", padx=12, pady=(10, 7))
        tk.Label(
            title,
            text="DIAGNOSTIC TERMINAL",
            background=COLOR["terminal"],
            foreground=COLOR["cyan"],
            font=(MONO_FONT, 10, "bold"),
        ).pack(side="left")
        self.terminal_meta = tk.StringVar(value="FW --  ·  PROTO --  ·  115200 8N1")
        tk.Label(
            title,
            textvariable=self.terminal_meta,
            background=COLOR["terminal"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 8),
        ).pack(side="right")

        toolbar = tk.Frame(parent, background=COLOR["terminal"])
        toolbar.grid(row=1, column=0, sticky="ew", padx=10)
        ttk.Checkbutton(toolbar, text="SHOW TELEMETRY", variable=self.show_telemetry).pack(side="left")
        ttk.Checkbutton(toolbar, text="PAUSE VIEW", variable=self.log_paused).pack(side="left", padx=8)
        ttk.Button(toolbar, text="STATUS", command=lambda: self.send_command("STATUS")).pack(side="right")
        ttk.Button(toolbar, text="CLEAR FAULT", command=lambda: self.send_command("CLEAR")).pack(side="right", padx=6)

        log_frame = tk.Frame(parent, background=COLOR["terminal"])
        log_frame.grid(row=2, column=0, sticky="nsew", padx=(10, 5), pady=8)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log_text = tk.Text(
            log_frame,
            background=COLOR["terminal"],
            foreground="#9CB1C5",
            insertbackground=COLOR["cyan"],
            selectbackground="#194764",
            relief="flat",
            borderwidth=0,
            wrap="none",
            font=(MONO_FONT, 9),
            padx=8,
            pady=8,
        )
        scrollbar_y = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar_y.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scrollbar_y.grid(row=0, column=1, sticky="ns")
        self.log_text.tag_configure("error", foreground=COLOR["red"])
        self.log_text.tag_configure("ack", foreground=COLOR["green"])
        self.log_text.tag_configure("info", foreground=COLOR["cyan"])
        self.log_text.tag_configure("tx", foreground=COLOR["amber"])
        self.log_text.tag_configure("telemetry", foreground=COLOR["muted_2"])
        self.log_text.tag_configure("system", foreground="#B794F4")
        self._append_log("XIN POWER TERMINAL READY", "system")
        self._append_log("扫描设备、选择端口并连接；也可直接连接已知 COM 口。", "system")

        command = tk.Frame(parent, background=COLOR["terminal"])
        command.grid(row=3, column=0, sticky="ew", padx=10)
        command.columnconfigure(1, weight=1)
        tk.Label(
            command,
            text=">_",
            background=COLOR["terminal"],
            foreground=COLOR["cyan"],
            font=(MONO_FONT, 12, "bold"),
        ).grid(row=0, column=0, padx=(3, 7))
        self.command_var = tk.StringVar()
        command_entry = ttk.Entry(command, textvariable=self.command_var)
        command_entry.grid(row=0, column=1, sticky="ew")
        command_entry.bind("<Return>", lambda _event: self.send_raw_command())
        ttk.Button(command, text="SEND", style="Primary.TButton", command=self.send_raw_command).grid(
            row=0,
            column=2,
            padx=(7, 0),
        )

        footer = tk.Frame(parent, background=COLOR["terminal"])
        footer.grid(row=4, column=0, sticky="ew", padx=10, pady=(8, 10))
        self.detail_text = tk.StringVar(value="DAC --  ·  MONITOR --  ·  RX/TX DROP --/--")
        tk.Label(
            footer,
            textvariable=self.detail_text,
            background=COLOR["terminal"],
            foreground=COLOR["muted"],
            font=(MONO_FONT, 8),
        ).pack(side="left")
        ttk.Button(footer, text="SAVE LOG", command=self.save_log).pack(side="right")
        self.record_button = ttk.Button(footer, text="REC CSV", command=self.toggle_recording)
        self.record_button.pack(side="right", padx=6)
        ttk.Button(footer, text="CLEAR", command=self.clear_log).pack(side="right")

    def refresh_ports(self) -> None:
        if list_ports is None:
            self.link_pill.set("PYSERIAL MISSING", COLOR["red"])
            self.device_text.set("缺少 pyserial；请运行 run_windows.bat 安装依赖。")
            self.discover_button.configure(state="disabled")
            self.connect_button.configure(state="disabled")
            return
        previous_device = self.port_lookup.get(self.port_var.get(), "")
        ports = describe_ports(list_ports.comports())
        self.port_lookup.clear()
        self.port_info = {item.device: item for item in ports}
        display_values: list[str] = []
        selected = ""
        for item in ports:
            identity = self.detected_devices.get(item.device)
            badge = "★ XIN POWER" if identity else item.transport
            fw = f"  FW {identity[0]}" if identity and identity[0] else ""
            display = f"{item.device}   |   {badge}{fw}   |   {item.description}"
            self.port_lookup[display] = item.device
            display_values.append(display)
            if item.device == previous_device:
                selected = display
        self.port_combo["values"] = display_values
        if selected:
            self.port_var.set(selected)
        elif display_values:
            self.port_var.set(display_values[0])
        else:
            self.port_var.set("")
        if not self.worker.connected and not self.connect_attempting:
            self.connect_button.configure(state="normal" if display_values else "disabled")
        self._port_selected()

    def _port_selected(self) -> None:
        device = self.port_lookup.get(self.port_var.get(), "")
        info = self.port_info.get(device)
        if self.worker.connected:
            return
        if not info:
            self.link_pill.set("NO SERIAL PORT", COLOR["red"])
            self.device_text.set("没有可用 COM 端口；蓝牙请先在 Windows 中配对 XLBT。")
        elif device in self.detected_devices:
            self.link_pill.set(f"READY · {device} · XIN POWER", COLOR["green"])
            self.device_text.set(f"已识别 {device}，点击“连接所选”即可连接。")
        elif info.transport == "BLUETOOTH SPP":
            self.link_pill.set(f"READY · {device} · BT SPP", COLOR["cyan"])
            self.device_text.set(f"已选择 {device} · 蓝牙 SPP；可直接连接，或先扫描确认设备。")
        else:
            self.link_pill.set(f"READY · {device} · {info.transport}", COLOR["amber"])
            self.device_text.set(f"已选择 {device} · {info.transport}；可直接连接，或先扫描确认设备。")

    def discover_devices(self) -> None:
        """显式探测所有空闲 COM 口，用协议握手识别本设备。"""

        if self.discovery_running:
            return
        if self.worker.connected:
            messagebox.showinfo("自动发现", "请先断开当前连接，再执行端口探测。")
            return
        if serial is None or list_ports is None:
            messagebox.showerror("自动发现", "缺少 pyserial，无法枚举和探测串口。")
            return
        ports = describe_ports(list_ports.comports())
        if not ports:
            self.link_pill.set("NO SERIAL PORT", COLOR["red"])
            self.device_text.set("没有可用串口；蓝牙请先在 Windows 配对 XLBT")
            self.show_bluetooth_guide()
            return
        self.discovery_running = True
        self.detected_devices.clear()
        self.discover_button.configure(text=f"正在探测 0 / {len(ports)}", state="disabled")
        self.connect_button.configure(state="disabled")
        self.link_pill.set("SCANNING PORTS", COLOR["amber"])
        self.device_text.set("正在逐个确认 COM 端口，请稍候……")
        self._append_log(f"AUTO DISCOVERY START · {len(ports)} PORT(S)", "system")
        threading.Thread(target=self._discovery_worker, args=(ports,), daemon=True).start()

    def _discovery_worker(self, ports: list[PortDescriptor]) -> None:
        found: list[str] = []
        assert serial is not None
        for index, item in enumerate(ports, start=1):
            self.events.put(("probe_progress", (index, len(ports), item.device)))
            result = probe_xin_power(item.device, serial.Serial, BAUD_RATE, timeout_s=1.0)
            if result.matched:
                found.append(item.device)
                self.events.put(("probe_match", result))
            elif result.error:
                self.events.put(("probe_busy", (item.device, result.error)))
        self.events.put(("probe_done", found))

    def _finish_discovery(self, found: list[str]) -> None:
        self.discovery_running = False
        self.discover_button.configure(text="扫描设备", state="normal")
        self.refresh_ports()
        if found:
            target = found[0]
            self._append_log(f"DEVICE FOUND · {', '.join(found)}", "ack")
            selected = next((display for display, device in self.port_lookup.items()
                             if device == target), None)
            if selected is None:
                self.port_var.set("")
                self.connect_button.configure(state="disabled")
                self.link_pill.set("PORT CHANGED · RESCAN", COLOR["amber"])
                self.device_text.set(f"刚识别的 {target} 已从 Windows 端口列表消失，请重新扫描。")
            else:
                self.port_var.set(selected)
                self._port_selected()
                self.device_text.set(f"找到 {len(found)} 个 XIN Power 端口；已选择 {target}，点击“连接所选”。")
        else:
            self.link_pill.set("XIN POWER NOT FOUND", COLOR["red"])
            self.device_text.set("扫描未收到设备响应；仍可在上方选择 COM 口并直接连接。")
            self._append_log("NO XIN POWER RESPONSE · CHECK USB OR PAIR XLBT", "error")

    def show_bluetooth_guide(self) -> None:
        """显示 VG6328A 的准确广播名与 Windows SPP 配对流程。"""

        dialog = tk.Toplevel(self)
        dialog.title("VG6328A / Windows 连接向导")
        dialog.geometry("650x520")
        dialog.minsize(580, 470)
        dialog.configure(background=COLOR["bg"])
        dialog.transient(self)

        shell, panel = make_panel(dialog, COLOR["panel"])
        shell.pack(fill="both", expand=True, padx=16, pady=16)
        tk.Label(
            panel,
            text="VG6328A  CONNECTION GUIDE",
            background=COLOR["panel"],
            foreground=COLOR["cyan"],
            font=(MONO_FONT, 15, "bold"),
        ).pack(anchor="w", padx=20, pady=(18, 3))
        tk.Label(
            panel,
            text="你搜索不到“VG6328A”是正常的：型号名不是出厂广播名。",
            background=COLOR["panel"],
            foreground=COLOR["text"],
            font=(UI_FONT, 11, "bold"),
        ).pack(anchor="w", padx=20, pady=(0, 14))

        names = tk.Frame(
            panel,
            background=COLOR["panel_2"],
            highlightbackground=COLOR["border"],
            highlightthickness=1,
        )
        names.pack(fill="x", padx=20, pady=(0, 15))
        tk.Label(
            names,
            text="XLBT",
            background=COLOR["panel_2"],
            foreground=COLOR["green"],
            font=(MONO_FONT, 20, "bold"),
        ).grid(row=0, column=0, padx=18, pady=(14, 2), sticky="w")
        tk.Label(
            names,
            text="经典蓝牙 SPP · Windows 上位机请选择这个",
            background=COLOR["panel_2"],
            foreground=COLOR["text"],
            font=(UI_FONT, 10),
        ).grid(row=0, column=1, padx=8, pady=(14, 2), sticky="w")
        tk.Label(
            names,
            text="XLBLE",
            background=COLOR["panel_2"],
            foreground=COLOR["amber"],
            font=(MONO_FONT, 20, "bold"),
        ).grid(row=1, column=0, padx=18, pady=(2, 14), sticky="w")
        tk.Label(
            names,
            text="BLE GATT · 不会直接生成传统串口 COM",
            background=COLOR["panel_2"],
            foreground=COLOR["muted"],
            font=(UI_FONT, 10),
        ).grid(row=1, column=1, padx=8, pady=(2, 14), sticky="w")

        steps = [
            "1   给控制板上电，确认 VG6328A 模块供电为 3.3V。",
            "2   Windows → 设置 → 蓝牙和设备 → 添加设备 → 蓝牙。",
            "3   选择 XLBT；不要把 XLBLE 当作本上位机的串口链路。",
            "4   配对完成后回到本工具，点击主界面的“扫描设备”。",
            "5   选择扫描结果中的 XLBT / XIN Power COM 端口，点击“连接所选”。",
            "6   扫描无结果时也可直接选择 COM 口连接；请以设备响应为准。",
        ]
        for step in steps:
            tk.Label(
                panel,
                text=step,
                background=COLOR["panel"],
                foreground=COLOR["text"],
                font=(UI_FONT, 10),
                anchor="w",
                justify="left",
            ).pack(fill="x", padx=22, pady=3)

        note = (
            "原厂默认同时开启 BLE 与 SPP；默认 UART 为 115200 8N1。若列表完全没有 XLBT，"
            "请先确认模块天线区域未被金属遮挡，并重新给整板上电。"
        )
        tk.Label(
            panel,
            text=note,
            wraplength=580,
            justify="left",
            background=COLOR["panel"],
            foreground=COLOR["muted"],
            font=(UI_FONT, 9),
        ).pack(fill="x", padx=22, pady=(14, 6))
        ttk.Button(panel, text="我知道了", style="Primary.TButton", command=dialog.destroy).pack(
            anchor="e",
            padx=20,
            pady=(8, 18),
        )

    def toggle_connection(self) -> None:
        if self.worker.connected:
            self._set_disconnected("MANUALLY DISCONNECTED")
            return
        self.connect_selected()

    def connect_selected(self) -> None:
        if self.worker.connected:
            messagebox.showinfo("已经连接", "请先断开当前设备，再连接其他端口。")
            return
        if self.discovery_running or self.connect_attempting:
            return
        display = self.port_var.get()
        port_name = self.port_lookup.get(display)
        if not port_name:
            messagebox.showwarning("未选择端口", "请先扫描设备或从列表中选择一个 COM 端口。")
            return
        self.connect_attempting = True
        self.port_combo.configure(state="disabled")
        self.discover_button.configure(state="disabled")
        self.connect_button.configure(text="正在连接…", state="disabled")
        self.link_pill.set(f"OPENING · {port_name}", COLOR["amber"])
        self.device_text.set(f"正在打开 {port_name}；若端口刚完成扫描，会自动短暂重试。")
        with self.connection_guard:
            self.connection_token += 1
            token = self.connection_token
        self.after(20, lambda: self._attempt_connection(port_name, display, 0, token))

    def _attempt_connection(self, port_name: str, display: str, attempt: int, token: int) -> None:
        if token != self.connection_token or self.app_closing:
            return
        threading.Thread(
            target=self._open_port_worker,
            args=(port_name, display, attempt, token),
            daemon=True,
        ).start()

    def _open_port_worker(self, port_name: str, display: str, attempt: int, token: int) -> None:
        port = None
        error = None
        try:
            port = SerialWorker.open_port(port_name)
        except Exception as exc:
            error = str(exc)
        with self.connection_guard:
            stale = self.app_closing or token != self.connection_token
            if not stale:
                self.events.put(("open_result", (token, port_name, display, attempt, port, error)))
        if stale and port is not None:
            port.close()

    def _on_open_result(self, token: int, port_name: str, display: str,
                        attempt: int, port: object | None, error: str | None) -> None:
        if token != self.connection_token or not self.connect_attempting:
            if port is not None:
                port.close()
            return
        if error is not None:
            if attempt < 2:
                self.device_text.set(f"{port_name} 暂未就绪，正在重试（{attempt + 1}/2）…")
                self.after(450 if attempt == 0 else 850,
                           lambda: self._attempt_connection(port_name, display, attempt + 1, token))
                return
            self.connect_attempting = False
            self.port_combo.configure(state="readonly")
            self.discover_button.configure(state="normal")
            self.connect_button.configure(text="连接所选", state="normal")
            self.link_pill.set(f"OPEN FAILED · {port_name}", COLOR["red"])
            self.device_text.set(f"{port_name} 无法打开；确认设备已配对、未被其他软件占用。")
            messagebox.showerror("连接失败", f"无法打开 {port_name}：\n{error}")
            return
        assert port is not None
        try:
            self.worker.attach_port(port)
        except Exception as exc:
            port.close()
            self.connect_attempting = False
            self._set_disconnected("OPEN FAILED")
            messagebox.showerror("连接失败", f"无法启动 {port_name} 读取：\n{exc}")
            return
        self.connect_attempting = False
        self.connected_device = port_name
        self.link_verified = False
        self.link_pill.set(f"VERIFYING · {port_name}", COLOR["amber"])
        transport = self.port_info.get(port_name)
        self.device_text.set(
            f"{port_name} · {transport.transport if transport else 'SERIAL'} · 已打开，等待设备确认…"
        )
        self.connect_button.configure(text="断开连接", style="Danger.TButton", state="normal")
        self._append_log(f"PORT OPEN · {display}", "system")
        port = self.worker.port
        for delay in (150, 1200, 2600):
            self.after(delay, lambda current_port=port: self._send_handshake(current_port))
        self.link_timeout_id = self.after(5500, lambda: self._verify_timeout(port))

    def _send_handshake(self, port: object) -> None:
        if self.worker.port is port and self.worker.connected and not self.link_verified:
            self.send_command("PING", quiet=True)
            self.send_command("INFO", quiet=True)

    def _connection_verified(self) -> None:
        if self.link_verified or not self.worker.connected:
            return
        self.link_verified = True
        if self.link_timeout_id is not None:
            self.after_cancel(self.link_timeout_id)
            self.link_timeout_id = None
        port_name = self.connected_device
        transport = self.port_info.get(port_name)
        self.link_pill.set(f"CONNECTED · {port_name}", COLOR["green"])
        self.device_text.set(f"{port_name} · {transport.transport if transport else 'SERIAL'} · 设备已确认")
        self._append_log(f"DEVICE VERIFIED · {port_name}", "ack")
        port = self.worker.port
        for delay, command in ((80, "LOG ON"), (180, "LOG RATE 500"), (280, "STATUS")):
            self.after(delay, lambda item=command, current_port=port:
                       self._send_initial_command(current_port, item))

    def _send_initial_command(self, port: object, command: str) -> None:
        if self.worker.port is port and self.link_verified:
            self.send_command(command, quiet=True)

    def _verify_timeout(self, port: object) -> None:
        self.link_timeout_id = None
        if self.worker.port is not port or self.link_verified:
            return
        port_name = self.connected_device
        self._append_log(f"NO DEVICE RESPONSE · {port_name}", "error")
        self._set_disconnected("NO DEVICE RESPONSE")
        self.device_text.set(f"{port_name} 已打开但未收到兼容的 XIN Power INFO；检查端口与设备供电。")
        messagebox.showwarning("设备未响应", f"{port_name} 已打开，但未收到兼容协议 3 的 INFO 响应。")

    def _set_disconnected(self, reason: str) -> None:
        with self.connection_guard:
            self.connection_token += 1
        if self.link_timeout_id is not None:
            self.after_cancel(self.link_timeout_id)
            self.link_timeout_id = None
        self.worker.disconnect()
        self.link_verified = False
        self.connect_attempting = False
        self.connected_device = ""
        self.link_pill.set(reason, COLOR["red"])
        self.device_text.set("请选择 COM 端口，或点击“扫描设备”重新识别。")
        self.sample_pill.set("TELEMETRY --", COLOR["muted_2"])
        self.last_telemetry_time = 0.0
        self.port_combo.configure(state="readonly")
        self.discover_button.configure(state="normal")
        self.connect_button.configure(
            text="连接所选", style="Primary.TButton",
            state="normal" if self.port_var.get() in self.port_lookup else "disabled",
        )
        self.output_requested = False
        self.output_button.configure(text="输出关闭", style="OutputOff.TButton")

    def send_command(self, command: str, quiet: bool = False) -> bool:
        if not self.link_verified and not quiet:
            messagebox.showwarning("尚未连接", "请先连接设备并等待设备确认。")
            return False
        try:
            self.worker.send(command)
        except Exception as exc:
            if not quiet:
                messagebox.showerror("发送失败", str(exc))
            return False
        self._append_log(f"{self._time_text()}  TX >  {command}", "tx")
        return True

    def set_voltage(self) -> None:
        try:
            command, millivolts = voltage_command(float(self.voltage_var.get()))
        except ValueError as exc:
            messagebox.showwarning("电压无效", str(exc))
            return
        if self.send_command(command):
            self.cards["setpoint"].variable.set(f"{millivolts / 1000:.3f}")

    def set_current(self) -> None:
        try:
            command, _ = current_command(float(self.current_var.get()))
        except ValueError as exc:
            messagebox.showwarning("电流无效", str(exc))
            return
        self.send_command(command)

    def toggle_output(self) -> None:
        target = not self.output_requested
        if target:
            try:
                voltage_cmd, _ = voltage_command(float(self.voltage_var.get()))
                current_cmd, _ = current_command(float(self.current_var.get()))
            except ValueError as exc:
                messagebox.showwarning("设定无效", str(exc))
                return
            if not self.send_command(voltage_cmd) or not self.send_command(current_cmd):
                return
        self.send_command("OUT ON" if target else "OUT OFF")

    def send_raw_command(self) -> None:
        command = self.command_var.get().strip()
        if command and self.send_command(command):
            self.command_var.set("")

    def _poll_events(self) -> None:
        processed = 0
        try:
            while processed < 100:
                kind, payload = self.events.get_nowait()
                processed += 1
                if kind == "line":
                    port, line = payload
                    if port is self.worker.port:
                        self._handle_line(str(line))
                elif kind == "disconnect":
                    port, reason = payload
                    if port is self.worker.port:
                        self._append_log(f"{self._time_text()}  SYS!  LINK LOST · {reason}", "error")
                        self._set_disconnected("LINK LOST")
                elif kind == "probe_progress":
                    index, total, device = payload
                    self.discover_button.configure(text=f"正在探测 {index} / {total}")
                    self.link_pill.set(f"PROBING · {device}", COLOR["amber"])
                elif kind == "probe_match":
                    result: ProbeResult = payload
                    self.detected_devices[result.device] = (result.firmware, result.protocol)
                elif kind == "probe_busy":
                    device, error = payload
                    self._append_log(f"PROBE SKIP · {device} · {error}", "telemetry")
                elif kind == "probe_done":
                    self._finish_discovery(list(payload))
                elif kind == "open_result":
                    self._on_open_result(*payload)
        except queue.Empty:
            pass
        self.after(POLL_INTERVAL_MS, self._poll_events)

    def _handle_line(self, text: str) -> None:
        frame = parse_line(text)
        stored = f"[{datetime.now():%H:%M:%S.%f}"[:-3] + f"] {frame.raw}"
        self.log_lines.append(stored)
        if len(self.log_lines) > 50000:
            del self.log_lines[:10000]

        prefix = f"{self._time_text()}  RX <  "
        if frame.kind == "telemetry":
            self._update_telemetry(frame)
            if self.show_telemetry.get():
                self._append_log(prefix + frame.raw, "telemetry")
        elif frame.kind == "error":
            self._append_log(prefix + frame.raw, "error")
        elif frame.kind == "ack":
            self._append_log(prefix + frame.raw, "ack")
        elif frame.kind == "info":
            self.firmware_version = frame.values.get("fw", "--")
            self.protocol_version = frame.values.get("proto", "--")
            self.terminal_meta.set(
                f"FW {self.firmware_version}  ·  PROTO {self.protocol_version}  ·  115200 8N1"
            )
            if self.connected_device:
                self.detected_devices[self.connected_device] = (
                    self.firmware_version,
                    self.protocol_version,
                )
            if self.protocol_version == "3" and self.firmware_version != "--":
                self._connection_verified()
            self._append_log(prefix + frame.raw, "info")
        elif frame.kind == "calibration":
            self.last_calibration = frame.values
            self._append_log(prefix + frame.raw, "info")
        elif frame.kind == "help":
            self._append_log(prefix + frame.raw, "info")
        else:
            self._append_log(prefix + frame.raw)

    def _update_telemetry(self, frame: ParsedFrame) -> None:
        values = frame.values
        self.last_telemetry = values
        self.telemetry_history.append(dict(values))
        self.last_telemetry_time = time.monotonic()
        vin = telemetry_number(values, "vin") / 1000.0
        vout = telemetry_number(values, "vout") / 1000.0
        iout = telemetry_number(values, "iout") / 1_000_000.0
        pout = telemetry_number(values, "pout") / 1000.0
        temp = telemetry_number(values, "temp") / 10.0
        setv = telemetry_number(values, "setv") / 1000.0
        output = values.get("out", "0") == "1"
        requested = values.get("req", "0") == "1"
        cc_mode = values.get("cc", "0") == "1"
        fault = values.get("fault", "UNKNOWN")

        self.cards["vin"].variable.set(f"{vin:.3f}")
        self.cards["vout"].variable.set(f"{vout:.3f}")
        self.cards["iout"].variable.set(f"{iout:.3f}")
        self.cards["pout"].variable.set(f"{pout:.3f}")
        self.cards["temp"].variable.set(f"{temp:.1f}")
        self.cards["setpoint"].variable.set(f"{setv:.3f}")
        mode = "CC" if cc_mode else ("CV" if output else "OFF")
        mode_color = COLOR["red"] if fault != "NONE" else (COLOR["amber"] if cc_mode else COLOR["green"])
        self.mode_text.set(f"OUTPUT {mode}  ·  FAULT {fault}")
        self.mode_label.configure(foreground=mode_color)
        self.detail_text.set(
            f"DAC {values.get('dac', '--')}  ·  SET {setv:.3f}V/"
            f"{telemetry_number(values, 'seti') / 1000:.3f}A / "
            f"EFF {telemetry_number(values, 'ilim', telemetry_number(values, 'seti')) / 1000:.3f}A  ·  "
            f"MON {values.get('valid', '0')}  ·  "
            f"DROP {values.get('rxdrop', '0')}/{values.get('txdrop', '0')}"
        )
        self.output_requested = requested
        self.output_button.configure(
            text="输出开启" if requested else "输出关闭",
            style="OutputOn.TButton" if requested else "OutputOff.TButton",
        )
        self.trend.append(vout, max(iout, 0.0))
        self._record_telemetry(values)

    def _update_age(self) -> None:
        if self.worker.connected and self.last_telemetry_time:
            age_ms = int((time.monotonic() - self.last_telemetry_time) * 1000)
            if age_ms <= 1500:
                self.sample_pill.set(f"TELEMETRY {age_ms} ms", COLOR["green"])
            else:
                self.sample_pill.set(f"STALE {age_ms} ms", COLOR["amber"])
        elif self.worker.connected:
            self.sample_pill.set("WAITING TELEMETRY", COLOR["amber"])
        else:
            self.sample_pill.set("TELEMETRY --", COLOR["muted_2"])
        self.after(500, self._update_age)

    def _append_log(self, text: str, tag: str | None = None) -> None:
        if self.log_paused.get():
            return
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + "\n", tag or "")
        line_count = int(self.log_text.index("end-1c").split(".")[0])
        if line_count > 6000:
            self.log_text.delete("1.0", "1001.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    @staticmethod
    def _time_text() -> str:
        return datetime.now().strftime("%H:%M:%S.%f")[:-3]

    def clear_log(self) -> None:
        self.log_lines.clear()
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def save_log(self) -> None:
        default_name = f"xin_power_{datetime.now():%Y%m%d_%H%M%S}.log"
        path = filedialog.asksaveasfilename(
            title="保存完整会话日志",
            defaultextension=".log",
            initialfile=default_name,
            filetypes=[("日志文件", "*.log"), ("文本文件", "*.txt"), ("所有文件", "*.*")],
        )
        if path:
            Path(path).write_text("\n".join(self.log_lines) + "\n", encoding="utf-8")

    def toggle_recording(self) -> None:
        if self.record_file is not None:
            self.record_file.close()
            self.record_file = None
            self.record_writer = None
            self.record_button.configure(text="REC CSV")
            return
        default_name = f"xin_power_telemetry_{datetime.now():%Y%m%d_%H%M%S}.csv"
        path = filedialog.asksaveasfilename(
            title="记录遥测 CSV",
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=[("CSV 文件", "*.csv")],
        )
        if not path:
            return
        self.record_file = open(path, "w", newline="", encoding="utf-8-sig")
        fields = [
            "pc_time",
            "t",
            "vin",
            "iin",
            "pin",
            "vout",
            "iout",
            "pout",
            "temp",
            "setv",
            "seti",
            "ilim",
            "req",
            "out",
            "cc",
            "dac",
            "valid",
            "vraw",
            "iraw",
            "fault",
            "rxdrop",
            "txdrop",
        ]
        self.record_writer = csv.DictWriter(self.record_file, fieldnames=fields, extrasaction="ignore")
        self.record_writer.writeheader()
        self.record_button.configure(text="STOP CSV")

    def _record_telemetry(self, values: dict[str, str]) -> None:
        if self.record_writer is None or self.record_file is None:
            return
        row = dict(values)
        row["pc_time"] = datetime.now().isoformat(timespec="milliseconds")
        self.record_writer.writerow(row)
        self.record_file.flush()

    def _close(self) -> None:
        with self.connection_guard:
            self.app_closing = True
            self.connection_token += 1
        # 关闭窗口前撤销轮询、握手和重试回调，避免销毁后仍调用 Tk 命令。
        for after_id in self.tk.splitlist(self.tk.call("after", "info")):
            try:
                self.after_cancel(after_id)
            except tk.TclError:
                pass
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "open_result" and payload[4] is not None:
                payload[4].close()
        self.worker.disconnect()
        if self.record_file is not None:
            self.record_file.close()
        self.destroy()


def main() -> None:
    app = PowerConsoleApp()
    app.mainloop()


if __name__ == "__main__":
    main()
