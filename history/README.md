# 历史关键版本

按版本升序提供源码快照和对应APP。早期节点有各自阻值/输出级/存储前提；“关键版本”表示可供比较的留存发布节点，不表示全部完成带载验收。

| 版本 | 源码 | 硬件前提与变化 |
|---|---|---|
| 5.9.3 | [XIN_Power_V5.9.3_PublicSource.zip](XIN_Power_V5.9.3_PublicSource.zip) | 原控制硬件；完整BL/APP/Factory/Recovery/PC；风扇与通信节点 |
| 6.0.4 | [XIN_Power_V6.0.4_PublicSource.zip](XIN_Power_V6.0.4_PublicSource.zip) | 当时原阻值/输出级；直接DAC校准及3.6V保存修复 |
| 6.0.8 | [XIN_Power_V6.0.8_PublicSource.zip](XIN_Power_V6.0.8_PublicSource.zip) | R316=4.99kΩ；当时输出级；SaveFix1_UIFix，直接DAC、Flash保存及回读修复 |
| 6.1.5 | [XIN_Power_V6.1.5_PublicSource.zip](XIN_Power_V6.1.5_PublicSource.zip) | R316=4.99kΩ；当时SRC补丁；匹配DAC参数，仍为2–32V/17点试验 |
| 6.2.2 | [XIN_Power_V6.2.2_PublicSource.zip](XIN_Power_V6.2.2_PublicSource.zip) | 当前V2旁路硬件；3.2–32V；纯DAC、休眠、25Hz、有线恢复 |

6.2.2为纯DAC对照；6.2.3当前完整工程含PI。公开历史源码补齐字体许可/资源，6.2.2换OFL中文位图后重新构建。原始与公开HASH见[manifest.json](manifest.json)，归档内部还包含逐文件SOURCE_SHA256和PUBLIC_SNAPSHOT说明。

本次从留存发布包重建索引，没有制造原始开发提交日期。6.0.8明确选SaveFix1_UIFix；中间诊断包和原始个人CSV/串口会话未上传。
