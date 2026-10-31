# 许可与来源

本仓库公开展示“基于zynq7020的示波器”的原创实现与工程复现材料。公开可见不等于授予额外开源许可。除明确标注第三方许可的部分外，原创代码的权利由其权利人保留；原创部分暂未采用 MIT、Apache 或其他开源许可证。如需复制、修改、再分发原创部分，应取得相应授权。

第三方组件按各自许可证使用，原创代码声明不改变以下授权：

| 组件 | 来源与许可位置 |
|---|---|
| LVGL 9.2.2 | https://github.com/lvgl/lvgl/tree/v9.2.2；MIT：`third_party/lvgl-9.2.2/LICENCE.txt`。保留完整必要 `src` 和公共头；锁定下载源与归档 SHA 见 dependencies.lock.json。 |
| SP2 UI Sans 字体子集及生成字体 C | 源于 Noto Sans SC；SIL OFL 1.1：`assets/fonts/OFL.txt`；来源、版本和 SHA 见 `assets/fonts/font-provenance.json`。子集已更名以遵守保留字体名要求。上游 https://github.com/notofonts/noto-cjk 。 |
| Xilinx FSBL、standalone、设备驱动、xilffs/BSP | 随 Vitis 2020.2 生成；版权和许可保留于 `repro/fsbl`、`repro/application/bsp/libsrc` 文件头及内部许可目录。Xilinx 源头 https://github.com/Xilinx/embeddedsw 。 |
| FreeRTOS 内核及 Xilinx 适配 | `repro/application/bsp/libsrc/freertos10_xilinx_v1_7/src/License/license.txt` 和各源文件头。 |
| FatFs | xilffs 源中保留原作者版权及再分发条款，见各 `ff.*` 文件头。 |
| 自动生成的 GPL PS 初始化替代源码 | `repro/hardware/ps7_init_gpl.*` 文件头的 GPL 条款；它们是生成器随附的独立替代版本，实际验收 FSBL 使用 `repro/fsbl/source/ps7_init.c`。 |

AMD/Xilinx 工具链、器件/IP 库并未随包分发；使用它们仍须遵守其官方条款。历史脚本中的 SHA、硬件地址和算法常数保留；个人目录名、开发用网络地址、板卡电缆序列号及 TF CID 样本在仓库中以占位值表示，使用相关脚本时需配置实际设备信息。
