# 图片来源

- device_main_open.png、device_settings_open.png、device_settings_scroll_open.png：本次公开副本真实C端绘制代码的离线像素输出，中文采用XIN UI Heiti，不是实物照片。
- pc_smoke_623.png：原6.2.3 Qt离屏加载预览，不代表已连接设备或已实测回传速率。
- device_photo.jpg：开发者在讨论中提供的设备实物照片，拍摄于此次公开字体替换前，用于展示外观。
- off_oscillation_before_bypass.jpg：开发者提供的旁路前关断波动波形照片，只用于开发历程，不能用作当前旁路后的波形结果。

所有图像保留原始像素内容或由项目自身绘制代码生成，没有将离线渲染误标为实板验收。

## 2026-10-11实物与负载照片

以下JPG来自作者提供的实物照片，仅按比例缩小用于文档，保留仪表与设备显示内容。

| 文件 | 内容 |
| --- | --- |
| front.jpg | 整机主界面 |
| power_top.jpg / bottom_thermal.jpg / control_install.jpg | 功率板、底面散热与控制板安装 |
| input.jpg / usb_control.jpg / output.jpg / fan.jpg | 输入、通信、输出和风扇位置 |
| load_15w.jpg / device_15w.jpg | 约15W电子负载和设备读数 |
| load_60w.jpg / device_60w.jpg | 约60W电子负载和设备读数 |
| load_95w.jpg / device_95w.jpg | 约95W、每档5分钟阶段读数 |
| device_95w_20min.jpg | 约95W连续20分钟，设备95.55W、NTC 69℃ |
| scope_a.jpg / scope_b.jpg / scope_c.jpg | 负载端慢时基波形，负载对应关系与探头设置待确认 |

负载采用电子负载CC恒流抽电流模式。设备NTC与电子负载自身温度分别记录。读数及原始照片SHA256见[负载测试记录](../负载测试记录.md)。
