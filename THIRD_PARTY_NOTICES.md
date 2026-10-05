# 来源与第三方声明

## XIN Power

维护者：LDO75。沿用仓库已有GPLv3许可证，见根目录LICENSE。第三方文件的原有许可和版权不被根目录许可替代。

## 原电源工程参考

立创开源用户cyxsnbb666：[Type-C可调电源原工程](https://oshwhub.com/cyxsnbb666/typec-adjustable-power-supply-wi)。开发中参考其功率电路与STM32F1工程。当前公开目录是XIN Power的PY32工程及开发节点，没有重新打包原作者完整源码或替原作者选择许可。硬件参考图的来源关系在hardware/README及开发文章说明。

## 器件库

- Puya PY32F403 HAL和设备文件：保留各文件Puya/ST版权与BSD-3-Clause等原始声明；许可内容以文件头和所附文件为准。
- ARM CMSIS：保留Drivers/CMSIS/LICENSE.txt及相应文件版权说明。
- 上位机依赖PySide6/Qt、pyserial与Python：依赖库保留自身许可；requirements列出源码运行所需包，Windows可执行包的依赖通知按打包内容保留。

## 字体

- Oxanium：位图源自Oxanium，可重生成；原字体和OFL见02_Application/ThirdParty/Oxanium。
- 思源黑体：从Adobe官方source-han-sans的release提交`a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`获取未修改的`SubsetOTF/CN/SourceHanSansCN-Regular.otf`。原字体及OFL见02_Application/ThirdParty/SourceHanSans。官方来源：[Adobe仓库](https://github.com/adobe-fonts/source-han-sans)、[OFL文本](https://github.com/adobe-fonts/source-han-sans/blob/release/LICENSE.txt)。
- 公开副本使用派生位图 **XIN UI Heiti**，由Tools/generate_heiti_font.py生成，派生位图仍适用OFL-1.1。没有把OFL字体更改为GPL许可。
- Windows SimHei只存在于原私有交付版本中；本次公开当前/相关历史副本的字库均替换为上述OFL派生位图，没有分发Windows字体文件。

历史节点添加的字体许可证和生成资源在索引中标注；保留原始包SHA256用于辨别本次公开整理与原交付快照的差异。
