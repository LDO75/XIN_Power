# V6.2.0 命令

新上位机将操作编码为协议5二进制包；普通串口助手先发送INFO可进入兼容文本模式。完整字节约定见[通信协议](../docs/WIRE_PROTOCOL_5.md)。

| 操作 | 参数/行为 |
|---|---|
| INFO / STATUS / DIAG | 版本、状态、链路诊断，只读 |
| SET V mV | 3200–32000mV，已保存点映射DAC，无PID |
| SET I mA | 100–8000mA，过流关断阈值，无恒流环 |
| OUT ON / OFF | 开启/关闭；故障锁存时先CLEAR |
| CLEAR | 清除故障，保持输出关闭 |
| CAL POINT GET / GET id | 表概况/单点；schema5，id=0–15按电压升序 |
| CAL POINT START id | 手动DAC校准并开启输出，保留电流阈值 |
| CAL POINT STEP -5/-1/1/5 | 直接调码；负码升压、正码降压 |
| CAL POINT STATUS | 活动点/DAC/电压/ready，ready仅表示DAC已写入 |
| CAL POINT CAPTURE mV | 保存万用表值和DAC；先OFF再写Flash，上位机读回确认 |
| CAL POINT STOP | 关闭输出，恢复此前设定，不保存未采集数据 |
| CAL GET / RAW | 测量系数/原始读数 |
| CAL SET V/I gain offset / SAVE / RESET | 输出关闭时修改测量系数/保存/恢复默认；正常升级不要RESET |
| LOG ON / OFF | 遥测开关，故障事件仍回传 |
| LOG RATE 20 | 协议5固定20ms；文本兼容模式最短100ms |
| FW STATUS / BEGIN / DATA / END / APPLY / ABORT | 升级页管理，旧Bootloader兼容 |
| PING / HELP | 连通性/帮助 |

点位：3.2、3.3、3.5、3.8、4.0、4.2、5、7.2、8.4、9、12、18、20、24、28、32V。每个点独立保存后生效，不必全部完成。保存检查实测电压区间顺序和Flash写入，不以贴近目标或INA一致作为采集限制。

START所在通道持有校准，STEP/CAPTURE只允许该通道；其他通道可STOP或关闭输出。普通设定、测量校准、升级须先STOP，Flash忙时返回错误。超时不等于保存成功，应读取状态确认。

V6.2.0没有PID命令、恒流控制、独立低压SHORT判据。协议号36保留，当前APP不提供REBOOT命令。
