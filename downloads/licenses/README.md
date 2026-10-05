# Windows上位机依赖许可

本次WindowsEXE沿用6.2.3交付，源码与构建脚本在firmware/current/05_PC_Tool。

| 组件 | 版本/来源 | 许可及源码入口 |
|---|---|---|
| Python | 3.12.7，打包构建环境 | Python-3.12-LICENSE.txt；https://www.python.org/downloads/source/ |
| PySide6 / Shiboken6 / Qt | 6.8.3 Essentials | LGPL-3.0.txt及GPL-3.0.txt；https://code.qt.io/pyside/pyside-setup/；https://code.qt.io/qt/ |
| pyserial | 3.5 | pyserial-LICENSE.txt；https://github.com/pyserial/pyserial/tree/v3.5 |
| PyInstaller | 6.16.0 | pyinstaller-COPYING.txt，含Bootloader例外；https://github.com/pyinstaller/pyinstaller/tree/v6.16.0 |

许可文本分别从构建环境、GNU官方许可页面和上述版本的PyPI源码分发包取得。可通过公开上位机源码及PyInstaller打包脚本重新构建并替换库；完整许可条件以各文本为准。运行程序不代表许可变更。
