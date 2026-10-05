import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from pathlib import Path
import sys,struct,unittest,csv,tempfile
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from wire_protocol import Parser,decode,Packet
from readable_log import format_frame

class TrimProtocolTests(unittest.TestCase):
    def test_real_firmware_pi_capability_and_frame_compatibility(self):
        packets=Parser().feed((Path(__file__).resolve().parents[2]/'tests/wire_vectors_620.bin').read_bytes())
        info=decode(next(p for p in packets if p.type==4))
        self.assertEqual(info.values['fw'],'6.2.3');self.assertEqual(info.values['control'],'DAC_PI')
        data=next(p for p in packets if p.type==5)
        self.assertEqual(len(data.payload),77)
        frame=decode(data);self.assertEqual(frame.values['trim_available'],'1')
        self.assertEqual(int(frame.values['pe']),int(frame.values['setv'])-int(frame.values['vfast']))
    def test_pi_flags_and_chinese_log_without_breaking_old_frames(self):
        payload=bytearray(77);struct.pack_into('<H',payload,34,3300);struct.pack_into('<H',payload,48,3250)
        struct.pack_into('<H',payload,50,0x302)
        f=decode(Packet(5,1,bytes(payload)))
        self.assertEqual(f.values['pid'],'1');self.assertEqual(f.values['pe'],'50');self.assertIn('PI微调',format_frame(f))
        struct.pack_into('<H',payload,50,0x202)
        self.assertIn('PI暂停',format_frame(decode(Packet(5,1,bytes(payload)))))
        struct.pack_into('<H',payload,50,2)
        self.assertNotIn('PI',format_frame(decode(Packet(5,1,bytes(payload)))))
    def test_pi_diagnostic_and_csv_columns(self):
        from PySide6.QtWidgets import QApplication
        from xin_power_qt import Bridge
        app=QApplication.instance() or QApplication([]);b=Bridge()
        try:
            payload=bytearray(77);struct.pack_into('<H',payload,34,3300);struct.pack_into('<H',payload,48,3250)
            struct.pack_into('<H',payload,50,0x302);b._on_frame(decode(Packet(5,1,bytes(payload))))
            self.assertIn('PI微调 / 误差 50 mV',b.controlDiagnostic)
            with tempfile.TemporaryDirectory() as d:
                p=Path(d)/'data.csv'
                with patch('xin_power_qt.QFileDialog.getSaveFileName',return_value=(str(p),'')): b.exportCsv()
                with p.open(encoding='utf-8-sig') as stream: rows=list(csv.reader(stream))
                self.assertEqual(len(rows[0]),29);self.assertEqual(len(rows[1]),29)
                self.assertEqual(rows[1][-2:],['1','50'])
        finally:
            b._record.close();b._frame_timer.stop();b._log_timer.stop();b.curve._timer.stop();b.deleteLater()

if __name__=='__main__':unittest.main(verbosity=2)
