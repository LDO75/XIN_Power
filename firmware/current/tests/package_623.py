"""Validate and package V6.2.3 firmware, rebuilt PC EXE and complete source."""
from pathlib import Path
import hashlib, json, re, shutil, sys, zipfile
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
workspace=root.parent.parent
sys.path.insert(0,str(root/'05_PC_Tool'))
from firmware_image import load_firmware

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    dest=Path(sys.argv[1]).resolve()
    if dest.exists(): raise SystemExit('Destination already exists; do not overwrite a release')
    hexfile=root/'02_Application/MDK-ARM/Objects/XIN_Power.hex'
    image=load_firmware(hexfile,'6.2.3')
    assert b'@INFO,fw=6.2.3,proto=5' in image.data
    assert '0 Error(s), 0 Warning(s)' in (root/'tests/keil_release.log').read_text()
    checks=(root/'tests/c_regression_623.log').read_text(encoding='utf-8-sig')
    assert 'FAIL' not in checks and 'error:' not in checks and checks.count('PASS')>=11
    assert 'poisoned pixels' in checks and 'Vpp ramp freeze' in checks
    pc_checks=(root/'tests/pc_regression_623.log').read_text(encoding='utf-8-sig')
    assert 'Ran 19 tests' in pc_checks and pc_checks.strip().endswith('OK')
    firmware=list((root/'02_Application/Core').rglob('*.c'))+list((root/'02_Application/Core').rglob('*.h'))+list((root/'02_Application/Core').rglob('*.inc'))
    assert max(p.stat().st_mtime for p in firmware)<=hexfile.stat().st_mtime
    pc=root/'05_PC_Tool/dist/XIN_Power_Studio_V6.2.3.exe'
    assert pc.is_file()
    pc_sources=[p for p in (root/'05_PC_Tool').rglob('*') if p.is_file() and
                p.suffix in {'.py','.qml','.spec'} and not set(p.relative_to(root).parts)&{'tests','build','dist','__pycache__'}]
    assert max(p.stat().st_mtime for p in pc_sources)<=pc.stat().st_mtime
    assert 'smoke_exit=0' in (root/'tests/pc_smoke_623.log').read_text()
    assert (root/'tests/pc_smoke_623.png').stat().st_size>1000
    assert 'completed successfully' in (root/'tests/pc_build_623.log').read_text(encoding='utf-8-sig')
    assert 'screen sleep' in checks and '40ms=25Hz' in checks
    assert 'PI full-span' in checks and checks.count('MODEL:')==216
    assert 'test_pi_diagnostic_and_csv_columns' in pc_checks
    assert 'test_wired_retry_keeps_original_port' in pc_checks
    dest.mkdir(parents=True)
    shutil.copy2(hexfile,dest/'XIN_Power_V6.2.3_APP_USB_BT.hex')
    (dest/'XIN_Power_V6.2.3_APP_USB_BT.bin').write_bytes(image.data)
    shutil.copy2(pc,dest/pc.name)
    shutil.copy2(root/'README_下载与校准.md',dest/'README_下载与校准.md')
    shutil.copy2(root/'tests/QA_6.2.3.md',dest/'QA_6.2.3.md')
    shutil.copy2(root/'docs/有线断链分析.md',dest/'有线断链分析.md')
    shutil.copy2(root/'05_PC_Tool/realtime_settings.json',dest/'realtime_settings.json')
    shutil.copy2(root/'docs/PI控制说明.md',dest/'PI控制说明.md')
    evidence=dest/'验证记录';evidence.mkdir()
    for name in ('keil_release.log','build_623.log','c_regression_623.log','pc_regression_623.log','device_main_623.png','device_settings_623.png','device_settings_scroll_623.png','device_sleep_623.png','pc_build_623.log','pc_smoke_623.log','pc_smoke_623.png','model_summary_623.json'):
        shutil.copy2(root/'tests'/name,evidence/name)
    for p in (root/'02_Application/MDK-ARM/Objects/XIN_Power.htm',root/'02_Application/MDK-ARM/Listings/XIN_Power.map'):
        shutil.copy2(p,evidence/p.name)
    excluded={'Objects','Listings','build','dist','__pycache__','.venv','history'}
    active_tests={'test_620.c','test_trim_623.c','model_summary_623.json','test_uart_recovery.c','test_settings_620.c','test_monitor_500.c','test_lcd_yield.c','test_console_620.c','test_ui_620.c','run_620_checks.ps1','run_short_checks.ps1','check_stack_budget.py','package_release.ps1','package_623.py','wire_vectors_620.bin','QA_6.2.3.md'}
    files=[]
    for folder in ('02_Application','05_PC_Tool','docs','tests'):
        for p in (root/folder).rglob('*'):
            if not p.is_file():continue
            rel=p.relative_to(root)
            if set(rel.parts)&excluded:continue
            if p.suffix.lower() in {'.exe','.pyc','.log','.ppm','.png','.bak','.uvguix','.uvoptx'}:continue
            if p.name in {'live_api.json','live_session_gui.jsonl'}:continue
            if folder=='tests' and p.name not in active_tests:continue
            if folder=='05_PC_Tool' and 'tests' in rel.parts and p.name not in {'test_620.py','test_622.py','test_623.py'}:continue
            files.append(p)
    files += [root/n for n in ('README_下载与校准.md','PROJECT_HANDOFF.md','build_firmware.ps1')]
    packaged={p.resolve() for p in files}
    for node in ET.parse(root/'02_Application/MDK-ARM/XIN_Power.uvprojx').findall('.//FilePath'):
        p=(root/'02_Application/MDK-ARM'/node.text.replace('\\','/')).resolve()
        assert p in packaged, f'Missing project source: {node.text}'
    source_hashes={p.relative_to(root).as_posix():sha(p) for p in sorted(files)}
    zippath=dest/'XIN_Power_V6.2.3_Source.zip'
    with zipfile.ZipFile(zippath,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(files):z.write(p,'XIN_Power_V6.2.3/'+p.relative_to(root).as_posix())
        z.writestr('XIN_Power_V6.2.3/SOURCE_SHA256.json',json.dumps(source_hashes,ensure_ascii=False,indent=2))
    with zipfile.ZipFile(zippath) as z:assert z.testzip() is None
    for suffix in ('hex','bin'):assert load_firmware(dest/f'XIN_Power_V6.2.3_APP_USB_BT.{suffix}','6.2.3').data==image.data
    manifest={'firmware':'6.2.3','PC':'6.2.3','protocol':5,'hardware':{'output_ideal_diode':'MOS D/S bypassed','R316_ohm':4990,'R292_ohm':1800},'hardware_tested':False,'image_bytes':image.size,'image_sha256':image.sha256,'image_crc32':f'{image.crc32:08X}','source_files':len(files),'telemetry_default_ms':40,'screen_default_sleep_seconds':60,'files':{p.relative_to(dest).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(dest.rglob('*')) if p.is_file()}}
    (dest/'release_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (dest/'SHA256SUMS.txt').write_text('\n'.join(f'{v["sha256"]}  {k}' for k,v in manifest['files'].items())+'\n',encoding='utf-8')
    print(json.dumps({'destination':str(dest),'source_files':len(files),'image_bytes':image.size,'PC':'6.2.3'},ensure_ascii=False))

if __name__=='__main__':main()
