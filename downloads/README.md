# 当前公开构建下载

## 6.2.3-open.1

设备仍报告6.2.3/协议5。本次公开APP使用思源黑体派生位图，与原6.2.3私有交付镜像HASH不同，控制参数/硬件基准不变。

| 文件 | 用途 |
|---|---|
| XIN_Power_V6.2.3_OPEN1_APP_USB_BT.hex / .bin | APP，基址0x08008000，已有Bootloader时选它 |
| XIN_Power_BL_BASE_5.9.3.hex | 留存5.9.3基准Bootloader在公开目录重新编译的镜像 |
| XIN_Power_V6.2.3_OPEN1_FACTORY_SWD.hex | 本次Bootloader与APP合并，空片SWD首次安装 |
| XIN_Power_Studio_V6.2.3.part1 / .part2 | 原6.2.3 Windows上位机的两个分卷，与当前协议兼容 |
| merge_pc_tool.cmd / merge_pc_tool.ps1 | 合并分卷，生成并校验 XIN_Power_Studio_V6.2.3.exe |
| XIN_Power_V6.2.3_OPEN1_FullSource.zip | 当前完整工程、构建脚本与许可；与源码目录对应 |
| SHA256SUMS.txt | 本次下载文件校验 |

GitHub文件页面点击Download raw即可下载。已有BL只更新APP，不要全片擦除。完整说明见docs/构建与下载及发布验证。

Windows上位机：下载 `.part1`、`.part2`、`merge_pc_tool.cmd`、`merge_pc_tool.ps1`，放在同一个文件夹，双击 `merge_pc_tool.cmd`。合并结果的SHA256必须是 `26b0bc8aff796a220c3d64fe7aa41e574d7a40b57a84bfd3dafa50d0bbba67cf`。分卷仅用于绕过上传接口的单次传输限制，合并后的程序与原交付EXE完全一致。

Windows程序基于Python3.12、PySide6/Qt6.8.3、pyserial3.5及PyInstaller6.16打包。依赖许可文本在licenses；上位机源码和打包脚本可用于重建/替换依赖，Qt源代码入口为https://code.qt.io/qt/。本次上传没有重新运行实机串口。
