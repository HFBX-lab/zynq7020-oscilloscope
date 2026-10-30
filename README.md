# 基于zynq7020的示波器

以 Zynq-7020 为核心的双通道数字示波器工程。PL 实现 ADC 采样、CIC 抽取、环形记录和硬件触发；PS 运行 FreeRTOS 与 LVGL，完成触屏界面、测量、频谱分析、TF 文件保存及恢复。项目已完成实板界面操作、TF 真断电留存和 QSPI 断电自动启动验证。

![真实板端时域画面](docs/images/time-domain.png)

以上为板端显存导出的真实界面帧，不是实物照片；实物照片与演示视频待补充。

## 已验证的功能

- 双通道 10 bit、50 MSa/s 转换；每条记录 4096 个双通道 32 bit 打包字。
- 21 档时基（100 ns/div 至 500 ms/div）；原始 50 MSa/s，以及 CIC3 抽取后的 6.25 MSa/s、625 kSa/s、62.5 kSa/s、6.25 kSa/s、625 Sa/s。
- 独立触发源、上升/下降沿、约 20% 预触发；AUTO、NORMAL、单次冻结。
- Vpp、RMS、频率、游标、占空比、正负脉宽、10–90% 上升/下降时间及自动设置。超出有效条件的测量显示无效。
- 4096 点 FFT 快照，Hann/矩形窗、去均值、独立谱通道；显示使用原记录采样率。
- RAM 波形与参数暂存；TF 的 SCWv2 波形文件和 SCP1 参数文件保存、读取与手动恢复。
- QSPI 上电自动启动；正常使用无需电脑下载。参数文件需要手动恢复，RAM 槽掉电清空。

测量以专业信号源设定值作工程参考，未使用独立参考示波器或精密电压表。5 MHz 已做工程幅频点检查，尚不代表全工况模拟带宽、精密计量、PVT 或温漂指标完成。小信号幅度余差、幅度相关 DC 偏差和纹波根因仍有边界，详见 [验证范围](docs/verified-specs.md)。本项目不提供连续 50 MSa/s 存盘或无死区采集保证。

## 阅读与运行

- [构建说明](BUILD.md)：从干净目录运行 Windows 主机模拟器和 33 项 CTest；离线重建 ARM 应用、FSBL、BOOT 及硬件工程。
- [系统结构与代码导航](docs/architecture.md)
- [演示步骤](docs/demo.md)
- [验证范围和已知限制](docs/verified-specs.md)
- [第三方许可与原创代码声明](NOTICE.md)

`release/` 提供经过验收的 application.elf、platform.bit、storage.xsa、fsbl.elf 和 BOOT.bin 及 SHA256。`repro/` 提供相应冻结应用、BSP、官方 FSBL、PS 初始化与 BD 源，不需要从历史缓存找回关键代码。编译器、AMD/Xilinx IP、Windows SDK/系统库不随仓库分发，见构建说明。

`scripts/` 中保留开发期间的历史测试和恢复脚本，以供评审代码；它们可能引用未公开的完整原始证据目录、历史版本和本机工具默认路径。新使用者应使用 `tools/build.py` 的参数化离线入口。任何板端下载、信号源控制及 Flash/SD 操作都应结合自己的硬件接线单独审查；此代码包构建验证没有运行这些操作。
