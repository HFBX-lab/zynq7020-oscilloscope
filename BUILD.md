# 构建与复现

所有新入口均离线工作，不访问串口、JTAG、发生器、SD 或 Flash。正常 QSPI 开机不需要执行任何构建脚本。

## Windows 主机模拟器

需要 Python 3、CMake ≥3.20、Ninja 和 MinGW-w64 UCRT GCC。LVGL 9.2.2 必要源、字体子集及生成的字体 C 已随包提供，无须下载、npm 或重新生成字体。示例中的工具路径可以替换为自己的位置；输出目录应位于源码之外，推荐无空格 ASCII 路径。

```powershell
python tools/build.py verify
python tools/build.py host --output D:/fpga-build/host `
  --cmake D:/msys64/ucrt64/bin/cmake.exe `
  --ninja D:/Python/Scripts/ninja.exe `
  --gcc D:/msys64/ucrt64/bin/gcc.exe
D:/fpga-build/host/host/bin/scope_sim.exe
```

入口会配置、编译并执行 CTest。模拟器可鼠标操作，但使用合成输入，不能用其画面替代实板测量结果。主机工程依赖 Win32 GDI/User32，不是 Linux 原生 GUI 工程。

## 冻结 ARM 应用与 FSBL

原验收工具链为 Vitis 2020.2 的 `arm-none-eabi-gcc`（Cortex-A9，hard-float VFPv3）、CMake 与 Ninja。`repro/application/` 包括实际编译的 board/ui/ADC/touch/storage 源和匹配 SD0+QSPI XSA 的 BSP 头文件、库以及完整 libsrc；SDPS 的 CID 与超时修复已包含在冻结源与 `libstorage_sdps.a` 中。`repro/fsbl/` 包括对应官方 FSBL、BSP 及 CMake 源。第三方授权保留在源码头、License 目录及本包 NOTICE 中。

**Vitis/Vivado 2020.2 的工具 wrapper 无法可靠处理中文或含空格路径。** 请将本工程解压到例如 `D:/FPGA_Competition/zynq7020-oscilloscope`，并使用英文无空格输出目录。主机模拟器可在中文及含空格路径构建；本次两种情况分别验证。新入口会在 ARM/硬件构建前检查路径并给出明确错误，不移动原工程、不修改系统盘符。

```powershell
python tools/build.py application --output D:/fpga-build/application `
  --cmake D:/msys64/ucrt64/bin/cmake.exe --ninja D:/Python/Scripts/ninja.exe `
  --arm-gcc D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe
python tools/build.py fsbl --output D:/fpga-build/fsbl `
  --cmake D:/msys64/ucrt64/bin/cmake.exe --ninja D:/Python/Scripts/ninja.exe `
  --arm-gcc D:/Xilinx_2020.2/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe
```

这些入口使用冻结 BSP 库，不会偷偷生成不同的 BSP。BSP 原始可构建源均在 libsrc；若要全量重生成 BSP，使用已安装 Vitis 2020.2 XSCT、`firmware/storage/bsp_storage.tcl` 和 `build_storage_bsp.ps1`，显式修改工具路径并传入 `release/storage.xsa` 与独立 BuildRoot。历史脚本默认工具路径仍需本地调整，新的 BSP 必须重新核对 FreeRTOS/xparameters/FatFs 配置、SDPS 两处修复和库链接优先级。

不同绝对路径会改变 ELF 调试信息，不能要求搬目录后的 ELF 全文件 SHA 与原验收 ELF 相同。重新构建不等于新固件已上板验收；`release/` 内原验收二进制的 hash 是独立的基准。

## BOOT 镜像

```powershell
python tools/build.py boot --output D:/fpga-build/boot `
  --bootgen D:/Xilinx_2020.2/Vitis/2020.2/bin/bootgen.bat
```

从 `release/` 的固定三分区输入生成 BOOT.bin，不烧写。QSPI 还包含必须保留的既有尾部数据，因此本仓库没有自动 Flash 擦写入口，也未公开原板整片 Flash。不要把 BOOT.bin 直接当作本机整片 Flash 备份。

## PL 与 SD0/QSPI 平台

需要 Vivado 2020.2、xc7z020clg484-1 器件支持及随 Vivado 提供的官方 IP。

```powershell
python tools/build.py pl --output D:/fpga-build/pl --vivado D:/Xilinx_2020.2/Vivado/2020.2/bin/vivado.bat
python tools/build.py platform --output D:/fpga-build/platform --vivado D:/Xilinx_2020.2/Vivado/2020.2/bin/vivado.bat
```

`pl` 从随包 RTL/Tcl/XDC 重建 50M PL；`platform` 从冻结原 BD 与模块源生成启用 QSPI(MIO1..6)、SD0(MIO40..45)、CD(MIO47) 的平台元数据。最终已验收 BD、原 BD、PS 初始化源及准确匹配的 storage.xsa 均保留。平台元数据构建不改变原验收 PL 字节。公开包整理时没有重跑昂贵的 Vivado 实现，新生成的 bit/XSA 仍需离线时序/CDC/脉宽审计和实板验证，不能继承旧版验收。

## 不随包分发的外部依赖

- Python、MinGW-w64、CMake、Ninja；Windows GDI/User32。
- Vitis/Vivado 2020.2 编译器、设备库、官方 IP、bootgen 与 XSCT。完整安装包及商业开发软件不随源码分发。
- 可选开发期测试：Python numpy/Pillow、Icarus Verilog；可选重新生成字体：fontTools 4.63.0、Node.js、lv_font_conv 1.5.3、原 Noto Sans SC 字体。常规主机/ARM 构建使用已保存字体，不需要这些字体生成工具。
- 信号源及板卡控制脚本涉及 pyserial/实际硬件；本包的离线入口不调用它们。

`repro/source-provenance.json` 记录复制前来源 SHA 和公开副本的匿名化范围；`release/SHA256SUMS.json` 锁定验收二进制；`docs/package-validation.json` 记录本次实际运行结果。历史完整 raw、用户波形、聊天、整片 Flash 和庞大 build/cache 不随包公开。
