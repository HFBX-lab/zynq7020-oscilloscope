# 系统结构与代码导航

## 数据通路

```text
双 ADC 10 bit × 50 MSa/s
        │
        ▼
PL：时钟/输入采样 → RAW 或 CIC3 抽取 → 硬件触发环形缓冲
        │                     4096 × 32 bit / 双通道记录
        ▼ AXI
PS：记录与原采样率/校准元数据 → 统计/时域测量/4096 FFT
        │
        ├─ LVGL/FreeRTOS → RGB 显示与触屏
        ├─ RAM 暂存/回放与参数槽（掉电清空）
        └─ FatFs / SD0 → SCWv2 / SCP1（手动保存/恢复）

QSPI → FSBL → PL bitstream → FreeRTOS 应用
```

PS/PL 数据身份由 ABI、记录 ID、采集 epoch、原记录率与校准 profile 绑定。暂停、回放和 FFT 使用独立记录缓存，避免改变实时采集记录；回放显示原记录采样率，不套用当前 live 的采样率。记录采用两路各占半字的 32 bit 打包格式，10 bit ADC 码保留在字段中。

## 主要源码

| 路径 | 职责 |
|---|---|
| `hardware/adc_live` | 50M 前端、CIC、AXI、触发环形缓冲及 RTL 仿真 |
| `hardware/adc`, `ps`, `touch_soc`, `rtl` | ADC 共用核、显示/PS 连接、触屏引脚与 LCD 启动 |
| `hardware/storage` | SD0/QSPI 元数据生成原脚本 |
| `src/scope_acquisition.*`, `scope_trigger.*` | 采集率、时基、触发模型与状态 |
| `src/scope_measurement.*`, `scope_cursor.*`, `scope_autoset.*` | 记录测量、游标与自动设置 |
| `src/scope_spectrum.*` | 4096 点 FFT、窗函数及频谱投影 |
| `src/scope_record.*`, `scope_settings.*` | SCW 波形、SCP 参数编解码与 RAM 管理 |
| `src/scope_storage.*`, `scope_storage_ui.*` | 文件持久事务、列表/分页、UI 状态及失败保护 |
| `src/scope_calibration*` | 每通道工程校准、profile 身份与记录兼容 |
| `src/scope_ui.*`, `fonts` | 中英界面、通道/触发/测量/FFT/存储交互及字体 |
| `firmware/lvgl` | 板端 LVGL 适配、视频呈现、FreeRTOS 应用与诊断 |
| `firmware/adc_live`, `touch`, `storage` | ADC、触屏与 FatFs/SDPS 板端适配 |
| `tests` | 模型、UI、格式、数值、接口与来源审计测试 |
| `scripts` | 开发期离线审计/参考计算及历史板测脚本 |
| `repro/application` | 验收应用当次实际使用源/链接文件/匹配 BSP（权威复现快照） |
| `repro/fsbl` | 实际 FSBL 源与 BSP、原生 CMake 构建 |
| `repro/hardware` | 冻结 BD、PS init 与模块来源 |

PL 采样侧与 AXI/显示侧跨时钟域；源码保留相关约束和时序检查。验收在工程条件下完成，不意味着所有外部脉宽/PVT 组合均被覆盖。

## 存储与启动边界

TF 文件位于 FAT32 卡 `SP2SCOPE` 目录；新编号创建避免覆盖旧文件。波形存储保存原记录/采样率/系数，参数存储与波形互不替代。文件恢复是用户主动操作，开机不自动载入任意参数。QSPI 启动镜像只有启动分区，不能替代整片 Flash 回滚备份。公开包刻意不包含用户记录、个人聊天及整片 Flash。
