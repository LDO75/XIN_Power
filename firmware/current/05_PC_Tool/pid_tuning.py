"""RAM PID tuning and exact telemetry/config revision tracking."""
from decimal import Decimal, DecimalException
import re
from PySide6.QtCore import QObject, Property, Signal, Slot

KEYS = ('kp', 'ki', 'kd', 'period', 'deadband', 'nearstep', 'farstep', 'nearband')
DEFAULTS = (150, 1000, 1, 20, 10, 1, 32, 500)

def encode_config(values):
    if len(values) != 8:
        raise ValueError('需要完整的 8 个参数')
    result = []
    try:
        for i, value in enumerate(values):
            number = Decimal(str(value).strip())
            scaled = number * (1000 if i < 3 else 1)
            if not scaled.is_finite() or scaled != scaled.to_integral_value():
                raise ValueError('增益最多三位小数，其余参数必须为整数')
            if scaled < 0 or scaled > 10000:
                raise ValueError('参数超出范围')
            result.append(int(scaled))
    except (DecimalException, OverflowError):
        raise ValueError('请输入有效数字') from None
    kp, ki, kd, period, deadband, near, far, band = result
    if not (0 <= kp <= 3000 and 0 <= ki <= 10000 and 0 <= kd <= 1000
            and 20 <= period <= 2000 and 0 <= deadband <= 1000
            and 1 <= near <= 256 and near <= far <= 512 and 1 <= band <= 10000):
        raise ValueError('参数超出范围；远目标步幅不能小于近目标步幅')
    return 'PID SET ' + ' '.join(map(str, result))

class PidTuning(QObject):
    changed = Signal()
    configChanged = Signal()

    def __init__(self, send, parent=None):
        super().__init__(parent)
        self._send = send
        self._supported = False
        self._connected = False
        self._params = dict(zip(KEYS, DEFAULTS))
        self._revision = None
        self._awaiting_echo = False
        self._status = '连接 V6.0.6 后读取参数；当前输入框为试验默认值'
        self._live = '等待设备遥测'

    def connection_changed(self, connected, version):
        parts = re.match(r'^(\d+)\.(\d+)\.(\d+)', version or '')
        supported = bool(connected and parts and tuple(map(int, parts.groups())) >= (6, 0, 6) and tuple(map(int, parts.groups())) < (6, 0, 8))
        became_supported = supported and not self._supported
        if not connected:
            self._revision = None
            self._awaiting_echo = False
            self._live = '未连接；实时记录已停止'
            self._status = '未连接，参数未确认'
        elif not supported:
            self._status = '当前固件为直接 DAC 调压，PID 不适用；PID 试验仅支持6.0.6/6.0.7'
        self._connected, self._supported = connected, supported
        self.changed.emit()
        if became_supported:
            self.read()

    @Property(bool, notify=changed)
    def supported(self): return self._supported

    @Property(str, notify=changed)
    def status(self): return self._status

    @Property(str, notify=changed)
    def liveText(self): return self._live

    @Property('QVariantMap', notify=configChanged)
    def parameters(self):
        return {k: f'{v/1000:.3f}' if i < 3 else str(v)
                for i, (k, v) in enumerate(self._params.items())}

    @Slot()
    def read(self):
        if self._supported:
            self._status = '等待读取设备参数…'
            self._send('PID GET')
            self.changed.emit()

    @Slot(str, str, str, str, str, str, str, str)
    def apply(self, *values):
        if not self._supported: return
        try:
            command = encode_config(values)
        except ValueError as exc:
            self._status = str(exc)
        else:
            self._awaiting_echo = False
            self._status = '已发送，等待设备 ACK 与参数回读；尚未确认生效'
            self._send(command)
        self.changed.emit()

    @Slot()
    def reset(self):
        if self._supported:
            self._awaiting_echo = False
            self._status = '等待设备恢复 RAM 默认参数并回读…'
            self._send('PID RESET')
            self.changed.emit()

    def on_frame(self, frame):
        v = frame.values
        if frame.kind == 'pid_config':
            try:
                values = [int(v[k]) for k in KEYS]
                # Reuse precisely the same range rules as command generation.
                encode_config([str(x/1000) if i < 3 else str(x) for i, x in enumerate(values)])
                revision = int(v['rev'])
            except (KeyError, ValueError):
                self._status = '参数回读不完整或无效'
            else:
                self._params = dict(zip(KEYS, values))
                self._revision = revision
                self._status = ('已应用并回读确认' if self._awaiting_echo else '已读取设备参数') + f' · 参数版本 {revision} · 重启恢复默认'
                self._awaiting_echo = False
                self.configChanged.emit()
            self.changed.emit()
        elif frame.kind in ('ack', 'error') and v.get('cmd', '').startswith('PID'):
            if frame.kind == 'error':
                self._status = '设备拒绝参数：' + v.get('reason', v.get('detail', 'UNKNOWN'))
                self._awaiting_echo = False
            else:
                self._awaiting_echo = True
                self._status = '设备 ACK 已收到，等待参数回读确认…'
            self.changed.emit()
        elif frame.kind == 'telemetry':
            mode = '持续 PID' if v.get('pid') == '1' else ('直接 DAC / PID 暂停' if v.get('out') == '1' else '输出关闭')
            self._live = (f"{mode} · 目标 {v.get('setv','—')} mV · PID反馈 {v.get('vfb','—')} mV\n"
                          f"DAC {v.get('dac','—')} · 误差 {v.get('err','—')} mV · 最近步幅 {v.get('delta','—')} 码\n"
                          f"更新次数 {v.get('updates','—')} · 最近间隔 {v.get('pdt','—')} ms · 参数版本 {v.get('pidrev','—')}")
            self.changed.emit()

    def csv_parameters(self, revision):
        # Do not label telemetry with a config from a different revision.
        if self._revision is None or str(self._revision) != str(revision):
            return [''] * 8
        return [self.parameters[k] for k in KEYS]
