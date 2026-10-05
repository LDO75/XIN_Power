# XIN Power V6.2.3 通信协议5

实现：固件wire_protocol.h/.c与debug_console.c；上位机wire_protocol.py。115200，8N1。实际二进制字节，不发送ASCII形式的HEX字符串。多字节整数小端、有符号整数补码、字段连续无结构体填充。

## 帧格式

| 帧偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 2 | A5 5A同步头 |
| 2 | 1 | 版本05 |
| 3 | 1 | 类型 |
| 4 | 2 | payload长度u16，最多448 |
| 6 | 2 | 序号u16 |
| 8 | payload长度 | payload |
| 8+payload长度 | 2 | CRC16 |

CRC16-CCITT：poly=0x1021，init=0xFFFF，无反射、无最终异或。覆盖偏移2开始的版本/类型/长度/序号/payload，不含同步头和CRC。Python为binascii.crc_hqx(body,0xffff)。总长度=payload长度+10。CRC/版本/长度错误重新找帧头；不完整帧间隔超过200ms清除。

| 类型 | 内容 |
|---|---|
| 1 | 命令：opcode u8 + 参数 |
| 2 / 3 | ACK / ERR：opcode u8 + ASCII说明/原因，无结束零 |
| 4 | INFO，29字节 |
| 5 | 遥测，77字节 |
| 6 | 故障现场，41字节 |
| 7 | 曲线概况，7字节 |
| 8 | 单点，9字节 |
| 9 | 校准状态，10字节 |
| 10 | 测量校准，33字节 |
| 11 | 少量诊断/帮助/升级状态UTF-8文本，仍封装在二进制帧内 |

77字节遥测不用sprintf，整帧87字节，默认25Hz=2175字节/秒，占115200 8N1约19%；可选50Hz约38%。双UART DMA，命令/故障预留空间；遥测拥塞丢弃整帧，下周期发最新状态，不阻塞采样。

## 命令payload

以下参数不含opcode。u/i为无符号/有符号位宽。

| opcode | 操作 | 参数 | 主要回复 |
|---|---|---|---|
| 1 | HELLO/INFO | nonce u32 | INFO |
| 2 | STATUS | 无 | 遥测；故障时附现场 |
| 3 | SET V | mV u16，3200–32000 | ACK/ERR |
| 4 | SET I | mA u16，100–8000 | ACK/ERR |
| 5 | OUT | u8，0关/1开 | ACK/ERR |
| 6 | CLEAR | 无 | ACK/ERR |
| 7 / 8 | CAL GET / RAW | 无 | 测量校准 |
| 9 / 10 | CAL SET V / I | gain i32，offset i32 | ACK/ERR |
| 11 / 12 | CAL SAVE / RESET | 无 | ACK/ERR |
| 13 | CURVE GET | 无 | 曲线概况 |
| 14 | POINT GET | id u16，0–15 | 单点 |
| 15 | POINT START | id u16，0–15 | ACK/ERR |
| 16 | POINT STEP | delta i16，只接受−5/−1/1/5 | ACK/ERR |
| 17 | POINT CAPTURE | 实测mV u16，1–65535 | ACK/ERR |
| 18 / 19 | POINT STOP / STATE | 无 | ACK / 校准状态 |
| 20 | LOG | u8，0关/1开 | ACK/ERR |
| 21 | RATE | u16，20–5000ms，默认40ms，分别设置命令所在通道 | ACK/ERR |
| 22 / 23 | DIAG / HELP | 无 | 诊断文本 |
| 24 | PING | 无 | ACK |
| 30 | FW STATUS | 无 | 升级状态文本 |
| 31 | FW BEGIN | size u32，CRC32 u32，versionCode u32 | ACK/ERR |
| 32 | FW DATA | offset u32 + 1–192个固件字节 | ACK/ERR |
| 33–35 | FW END / APPLY / ABORT | 无 | ACK/ERR |

versionCode例：6.2.0=60200。36保留，APP当前没有REBOOT处理。测量gain为ppm（1000000表示1倍），offset V为mV/I为µA，沿用原系数接口。

请求序号递增，回复沿用。异步遥测和故障使用独立事件序号，可能与请求序号数值相同，不能只按序号匹配ACK。普通请求仅一条在途，1.2s未确认最多重发2次，字节/序号完全相同；FW BEGIN/END首次等待90/45s。OUT OFF/STOP可先于等待中的查询发送。

每通道固件缓存最近4条完整请求/回复：相同序号+payload只重放，不重复STEP/CAPTURE；同序号不同payload拒绝。HELLO随机nonce变化清缓存。上位机按序号/类型/内容去重回复；遥测与故障不按请求去重。超时后须查询确认，不能当成保存成功。

## INFO（29字节）

依次：major/minor/patch/hardwareVersion各u8；vmin/vmax/imin/imax/sample_ms/screen_ms/telemetry_ms/rinj/hardware_tag各u16；points u8；flags u8；baud u32；otaCode u8。

电压mV，电流mA；V2，rinj=5110Ω，tag=0x4991，points=16。flags bit0=Flash可用、bit1=Bootloader可用。otaCode：0无、1暂存、2复制、3试运行、4确认、5回退、6取消、7校验、8设置保存忙。

## 遥测（77字节）

以下偏移相对于payload；无效时仍传最近记录的数值，应检查flags；关闭输出不伪造电压0。

| payload偏移 | 字段 | 类型 | 含义 |
|---|---|---|---|
| 0 | t | u32 | 设备发送时刻ms |
| 4 | seq | u32 | 完整采样序号 |
| 8 | samplet | u32 | 最近完整新样本时刻ms |
| 12 | vin | u16 | 输入mV |
| 14 | iin | i32 | 输入µA |
| 18 | pin | u32 | 输入mW |
| 22 | vout | u16 | 输出校准后mV |
| 24 | iout | i32 | 输出校准后并扣除泄放µA |
| 28 | pout | u32 | 输出mW |
| 32 | temp | i16 | 0.1℃ |
| 34 | setv | u16 | 目标mV |
| 36 | seti | u16 | 用户阈值mA |
| 38 | ilim | u16 | 实际保护阈值mA |
| 40 | dac | u16 | 12位DAC码 |
| 42 | vraw | u16 | 未校准输出mV |
| 44 | iraw | i32 | 未校准输出µA |
| 48 | vfast | u16 | 校准后未显示滤波mV |
| 50 | flags | u16 | 状态位 |
| 52 | fault_code | u8 | 故障码 |
| 53 | vpp | u16 | 最近1秒低频波动mV |
| 55 | hz | u16 | 实际采样Hz |
| 57 | maxgap | u16 | 统计期最大样本间隔ms |
| 59 | dw | u32 | DAC写入次数 |
| 63 | dwt | u32 | 最近写入时刻ms |
| 67 | rxdrop | u16 | 本通道RX溢出计数低16位 |
| 69 | txdrop | u16 | 本通道TX丢包计数低16位 |
| 71 | in_errors | u16 | 输入INA错误计数低16位 |
| 73 | out_errors | u16 | 输出INA错误计数低16位 |
| 75 | ntcraw | u16 | NTC ADC码 |

flags bit0=req请求开启、1=out实际开启、2=valid完整测量有效、3=CE脚高、4=输入INA在线、5=输出INA在线、6=Vpp有效、7=温度有效、8=PI微调准入（校准/关闭/误差超范围时0）、9=具备本版小误差PI，其余保留0。CE为关断高有效。hz/maxgap是采样统计，不是遥测频率。Vpp取最近1秒未显示滤波的校准后输出电压max−min，是低频波动。

## 故障（41字节）

依次：t u32；faultCode u8；phase u8；set/dac/vout/vuncal/vin各u16；iin/iout_uncal各i32；pout u32；temp i16；flags u8；age/seq各u32；ilim u16。

t为触发时刻ms；电压mV、电流µA、功率mW、温度0.1℃、ilim mA。flags bit0=完整测量有效、1=输入在线、2=输出在线。age为最近完整双路样本年龄，不伪装为此次电流时间；即时过流可以先于母线电压读取触发，所以故障电压可能来自上一完整样本。

故障码：0 NONE、1 MON_OFFLINE、2 VIN_UV、4 VOUT_OV、5 IIN_OC、6 IOUT_OC、10 OTP。短路大电流走过流路径，无低压SHORT码。先关CE后入队故障，LOG OFF仍发故障，锁存后STATUS也读回现场。

## 校准回复

- 概况7字节：schema u8=5，mask u32，saved u8，total u8=16。
- 单点9字节：id u8，target u16，valid u8，actual u16，dac u16，saved u8。
- 状态10字节：active u8，id u8，target/dac/vout各u16，ready u8，faultCode u8。
- 测量校准33字节：flags u8；vgain/voff/igain/ioff各i32；v1/c1/v2/c2/vraw各u16；iraw i32；dac u16。flags bit0=总有效、1=电压有效、2=电流有效、3=两点DAC有效、4=已保存。

电压mV，DAC0–4095，mask bit id=实测有效。CAPTURE成功ACK后读回同一actual与dac，才标记保存成功。单点保存立即应用。

## 兼容与检查

空闲应用通道不主动发ASCII。上位机先二进制HELLO，无回复才换行清旧APP命令缓冲并发文本INFO，不改变输出。协议4/旧Bootloader保留。显式文本INFO进入兼容模式，遥测最多10Hz。

tests/test_console_620.c通过真实固件命令处理生成tests/wire_vectors_620.bin；PC tests/test_620.py解析该文件校验双方布局，覆盖CRC、重试去重、OTA和新旧校准表。旧上位机未适配新点位ID，建议配套使用6.2.3。

6.2.3 INFO的控制类型由版本识别为DAC_PI；误差pe由setv−vfast在上位机计算。帧长度/偏移不变，旧软件忽略原保留高位仍可解包。
