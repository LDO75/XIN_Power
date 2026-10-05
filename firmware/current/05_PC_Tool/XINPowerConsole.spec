# -*- mode: python ; coding: utf-8 -*-

a = Analysis(
    ["xin_power_qt.py"],
    pathex=[],
    binaries=[],
    datas=[("qml", "qml")],
    hiddenimports=["serial.tools.list_ports"],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="XIN_Power_Studio_V6.2.3",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
)
