"""Offline link recovery tests: never open a real serial port."""
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QSG_RHI_BACKEND','software')
from pathlib import Path
import sys, threading, unittest
from unittest.mock import patch, Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PySide6.QtWidgets import QApplication
from xin_power_qt import Bridge, SerialReader
from protocol import parse_line
from link_diagnostics import recoverable_serial_failure, describe_serial_failure
APP=QApplication.instance() or QApplication([])

class LinkTests(unittest.TestCase):
    def setUp(self): self.b=Bridge()
    def tearDown(self):
        self.b._worker=None;self.b._record.close()
        self.b._frame_timer.stop();self.b._log_timer.stop();self.b.curve._timer.stop();self.b.deleteLater()
    def source(self, stopped=False):
        source=Mock();source.port_name='COM9';source.stop_event=threading.Event()
        if stopped: source.stop_event.set()
        source.isFinished.return_value=True
        self.b._selected_port='COM9';self.b._worker=source
        return source
    def test_os_errors_classified_without_claiming_physical_cause(self):
        for reason in ('ClearCommError failed (PermissionError(13, 拒绝访问))',
                       'GetOverlappedResult failed (PermissionError(13))'):
            self.assertTrue(recoverable_serial_failure(describe_serial_failure(reason)))
            self.assertIn(reason,describe_serial_failure(reason))
        self.assertFalse(recoverable_serial_failure('端口已打开，但电源未回应身份查询'))
    def test_wired_retry_keeps_original_port_and_bounded_attempts(self):
        b=self.b;source=self.source();reason='ClearCommError failed'
        with patch('xin_power_qt.QTimer.singleShot') as timer:
            b._on_stopped(source,reason)
            self.assertTrue(b._reconnect_pending);self.assertEqual(b._reconnect_port,'COM9')
            # A rescan must not silently move recovery to a different port.
            b._on_scan_complete([('COM8','COM8 USB','USB / UART',False)])
            self.assertEqual(b.selectedPort,'COM9')
            with patch.object(b,'connectDevice') as connect:
                b._attempt_reconnect();connect.assert_called_once_with()
                self.assertEqual(b._reconnect_attempts,1)
            b._reconnect_attempts=5;source=self.source()
            b._on_stopped(source,reason)
            self.assertFalse(b._reconnect_pending)
            self.assertIn('未成功',b.connectionText)
    def test_manual_disconnect_or_port_change_cancels_retry(self):
        b=self.b
        with patch('xin_power_qt.QTimer.singleShot'):
            b._on_stopped(self.source(),'GetOverlappedResult failed')
        b.disconnectDevice()
        with patch.object(b,'connectDevice') as connect:
            b._attempt_reconnect();connect.assert_not_called()
        b._reconnect_pending=True;b.selectPort('COM7 USB')
        self.assertFalse(b._reconnect_pending)
        b._reconnect_pending=True;b.selectTransport('wireless')
        self.assertFalse(b._reconnect_pending)
    def test_wireless_user_stop_and_handshake_errors_do_not_retry(self):
        b=self.b
        for mode,stopped,reason in [('wireless',False,'ClearCommError failed'),
                                   ('wired',True,'ClearCommError failed'),
                                   ('wired',False,'端口已打开，但电源未回应身份查询')]:
            b._transport_mode=mode
            with patch('xin_power_qt.QTimer.singleShot') as timer:
                b._on_stopped(self.source(stopped),reason)
                self.assertFalse(b._reconnect_pending);timer.assert_not_called()
    def test_reader_ack_timeout_keeps_session_and_new_connection_only_reads(self):
        reader=SerialReader('MOCK');commands=[];stopped=[];reader.stopped.connect(stopped.append)
        class Port:
            def __enter__(self): return self
            def __exit__(self,*args): pass
            def reset_input_buffer(self): pass
        class Session:
            pending=None
            def __init__(self,port): self.polls=0
            def handshake(self,cancel): return parse_line('@INFO,fw=6.2.2,hw=PY32F403_V2')
            def send(self,command): commands.append(command)
            def poll(self):
                self.polls+=1
                if self.polls==1: raise TimeoutError('missing ACK')
                reader.stop()
                return [parse_line('@TEL,vout=3300,out=1,fault=NONE')]
        with patch('serial.Serial',return_value=Port()) as serial_open,patch('xin_power_qt.SerialSession',Session):
            reader.run()
        self.assertEqual(commands,['LOG RATE 40','STATUS'])
        serial_open.assert_called_once_with('MOCK',115200,timeout=.005,write_timeout=1)
        frames=[]
        while not reader.frames.empty(): frames.append(reader.frames.get_nowait())
        self.assertEqual([f.kind for f in frames],['info','error','telemetry'])
        self.assertIn('结果未知',frames[1].values['reason']);self.assertEqual(stopped,[''])
        reader.deleteLater()

if __name__=='__main__':unittest.main(verbosity=2)
