# DL-14542 温度显示计时器

15 分钟倒计时 → 实时温度显示 → 电量指示

基于九齐（Nyquest）NY8BM84A 单片机的便携式温显方案：上电自检 → 休眠待机 → 触摸唤醒后
显示实时温度、15 分钟倒计时与电池电量，并可随时插入充电器进入充电显示。

## 功能特性

- **15 分钟倒计时**：MM:SS 显示，长按启动/停止，剩余 5 分钟与倒计时结束时各有一次闪烁报警
- **实时温度显示**：NTC 3950/100K 分压采样，分辨率 0.1℃（3 位数码管显示 `27.8`）
- **电量指示**：电池图标（边框 + 5 格电量 + 状态灯），低电绿灯闪烁、充电红灯闪烁、充满绿灯常亮
- **温区 RGB 灯圈**：< 36.0℃ 蓝色、36.0~39.9℃ 绿色、≥ 40.0℃ 红色；报警/自检时三色循环
- **触摸按键**：短按唤醒/熄屏，长按启动或停止倒计时
- **低功耗**：亮屏 60s 无操作自动熄屏；无事件时进入 sleep，由看门狗每 60ms 唤醒轮询触摸与充电

## 硬件

| 项目 | 规格 |
| --- | --- |
| MCU | NY8BM84A（ROM 4K × 16bit，IRC 8MHz，指令时钟 2T） |
| 温度 | NTC 3950/100K/1% 与 100kΩ 分压 → PA2（AN2），10nF 滤波，PA1 控制分压供电 |
| 触摸 | CT8233LF 触摸芯片，QC 输出接 PA0（低电平 = 按下） |
| 充电 | TP4056：CHRG# → PA6，STDBY# → PA5 |
| 显示 | 3 位温度 + 4 位倒计时 + 冒号/℃/小数点符号 + 电池图标，12 相动态扫描 |
| 灯圈 | RGB 三色指示灯（PA4 / PB3 / PB2，低电平点亮） |
| 时基 | Timer0 1ms 中断（系统节拍 + 数码管扫描） |
| 看门狗 | 使能、Event = Interrupt、Time Base = 60ms（sleep 唤醒源） |

硬件资料见 [`project/Hardware/`](project/Hardware)：PCB 工程（`.PcbDoc`）与 UI 设计稿（`.pdf`）。

### 引脚分配

| 引脚 | 功能 | 说明 |
| --- | --- | --- |
| PA0 | TOUCH | 触摸输入（低电平 = 按下） |
| PA1 | NTC_EN | NTC 分压供电使能（输出高 = 通电） |
| PA2 | NTC / AN2 | 温度采样（12bit ADC，参考 VDD） |
| PA3 | SEG3 | 数码管段线 |
| PA4 | LEDR | 红色灯圈（低电平点亮） |
| PA5 | STDBY | TP4056 STDBY#（读高 = 已充满） |
| PA6 | CHRG | TP4056 CHRG#（读高 = 充电中） |
| PA7 | SEG2 | 数码管段线 |
| PB0 | SEG9 | 电池图标（边框 / 状态灯） |
| PB1 | SEG1 | 数码管段线 |
| PB2 | LEDB | 蓝色灯圈（低电平点亮） |
| PB3 | LEDG | 绿色灯圈（低电平点亮） |
| PB4 | SEG4 | 数码管段线 |
| PB5 | SEG5 | 数码管段线 |
| PB6 | SEG6 | 数码管段线 |
| PB7 | SEG7 | 数码管段线 |
| PC0 | SEG10 | 电池电量格 |
| PC1 | SEG8 | 数码管段线 |
| PC2~PC5 | — | 未使用（默认输出低） |

## 显示驱动

数码管为共阴驱动，SEG1~SEG10 按 12 相轮流扫描，每相 1ms，12ms 完成一轮刷新：

| 相 | 内容 | 公共端（拉低） | 段线（拉高） |
| --- | --- | --- | --- |
| 0 | 温度个位 `temp_buf[0]` | SEG1 | SEG2/3/4/5/6/7/8（A~G） |
| 1 | 温度十位 `temp_buf[1]` | SEG3 | SEG1/2/4/5/6/7/8 |
| 2 | 温度百位 `temp_buf[2]` | SEG4 | SEG1/2/3/5/6/7/8 |
| 3 | 倒计时秒个位 `timer_buf[0]` | SEG5 | SEG1/2/3/4/6/7/8 |
| 4 | 倒计时秒十位 `timer_buf[1]` | SEG6 | SEG1/2/3/4/5/7/8 |
| 5 | 倒计时分个位 `timer_buf[2]` | SEG7 | SEG1/2/3/4/5/6/8 |
| 6 | 倒计时分十位 `timer_buf[3]` | SEG8 | SEG1/2/3/4/5/6/7 |
| 7 | 附加符号（◴ / NC / 小数点） | SEG2 | SEG3/4/5 |
| 8 | 冒号 `:` 与温度符号 `℃` | SEG2 | SEG6/7（冒号受闪烁控制）、SEG1 |
| 9 | 电池图标边框 | SEG9 | SEG1/3/4/5/6 |
| 10 | 电池电量格 | SEG10 | SEG6（边框）+ SEG1~SEG5（按电量逐格点亮） |
| 11 | 电池状态灯 | SEG9 | SEG7（绿）/ SEG2（红） |

- 每相开始先把全部段线与灯圈引脚置为输入（高阻）释放上一相，避免残影
- 灯圈 LEDR/G/B 为独立引脚，仅在相 0/3/6/9 驱动（约 1/3 占空比）
- 充电模式（`DISPLAY_MODE_CHARGING`）下相 0~8 全部跳过，画面只剩电池图标
- 温度值以 0.1℃ 为单位（`278` = 27.8℃），倒计时为 `MM:SS`

## 交互说明

| 操作 | 场景 | 效果 |
| --- | --- | --- |
| 上电 | — | 自检 3s：显示 `888` + `88:88`，灯圈每 500ms 换色，随后熄屏休眠 |
| 短按 | 休眠中 | 唤醒显示温度与电量（不启动倒计时） |
| 长按 | 休眠中 | 唤醒并立即启动 15 分钟倒计时（进入报警动画） |
| 短按 | 倒计时中 | 熄屏 / 重新点亮（倒计时继续） |
| 短按 | 未倒计时 | 直接回到休眠 |
| 长按 | 未倒计时 | 启动 15 分钟倒计时（900s） |
| 长按 | 倒计时中 | 停止倒计时并复位显示 |
| 无人操作 | 亮屏 60s | 自动熄屏；若未在倒计时则同时回休眠 |
| 插入充电器 | 任意状态 | 切换到充电显示（仅电池图标） |
| 拔出充电器 | 充电中 | 关显示回到休眠 |

## 软件结构

```
User/
  main.c      主程序入口（初始化 + 主循环）
  system.c    系统初始化、1ms 节拍、任务调度、状态机、看门狗/休眠
  display.c   数码管 12 相扫描、段码缓冲、电池图标、RGB 灯圈、闪烁控制
  temp.c      NTC 温度采集（ADC 平均 + 查表插值 + 1/16 递推平均）
  battery.c   电量采集（1/4 VDD 通道 + 2V 基准，分段折算 + 1/8 递推平均）
  input.c     触摸与充电状态输入（消抖、短按/长按、充电状态判定）
  gpio.c      GPIO 方向/电平/上下拉/唤醒/模拟数字切换封装
  adc.c       ADC 驱动（通道、参考电压、时钟、阻塞式转换）
Include/
  *.h         各模块接口与引脚/参数定义
```

### 状态机

`system.sys_mode` 描述整轮流程，`system.sys_state` 保存位标志子状态，`system.sys_event` 缓存事件：

| 当前模式 | 事件 | 下一模式 |
| --- | --- | --- |
| INIT | 自检 3s 超时 | SLEEP |
| SLEEP | 短按唤醒 | TIMER（温度 + 电量，无倒计时） |
| SLEEP | 长按唤醒 | TIMER（并启动 15 分钟倒计时） |
| SLEEP | 检测到充电中 | CHARGING |
| SLEEP | 每次 WDT 唤醒后无按键 | SLEEP（再次 sleep） |
| TIMER | 短按（未倒计时） | SLEEP |
| TIMER | 亮屏 60s 超时（未倒计时） | SLEEP |
| TIMER | 插入充电器 | CHARGING |
| CHARGING | 拔出充电器 | SLEEP |

（TIMER 模式下短按熄屏、长按启停倒计时均不改变 `sys_mode`；倒计时由 `SYSTEM_STATE_TIMER_STARTUP`
标志与 `TASK_SEC_TIMER` 任务维持）

### 周期任务（`TASK_PERIOD[]`，单位 ms）

| 任务 | 周期 | 说明 |
| --- | --- | --- |
| TASK_TOUCH_SCAN | 60 | 触摸扫描（短按 ≤1.5s、长按 3s） |
| TASK_DISPLAY_UPDATE | 100 | 把温度/倒计时/电量/电池状态送往显示模块 |
| TASK_CHARGE_SCAN | 100 | 充电状态扫描（500ms 消抖） |
| TASK_NTC_SAMPLE | 100 | NTC 采样并推进 IIR 滤波 |
| TASK_SEC_TIMER | 1000 | 倒计时递减 |
| TASK_TEMP_UPDATE | 2000 | 更新有效温度 |
| TASK_BATTERY_SCAN | 2000 | 更新电量与电池状态 |

## 参数速查

| 参数 | 值 | 位置 |
| --- | --- | --- |
| 系统节拍 / 扫描相周期 | 1ms | `system.c` Timer0_Init / `display.c` |
| 扫描帧周期 | 12ms（12 相） | `display.c` Display_Scan_ISR |
| 上电自检时长 | 3s | `SYSTEM_INIT_TIME` |
| 亮屏保持时间 | 60s | `SYSTEM_DISPLAY_TIME` |
| 倒计时时长 | 900s（15 分钟） | `system.c` SYSTEM_MODE_SLEEP / TIMER |
| 剩余 5 分钟提示阈值 | 300s | `SYSTEM_COUNTDOWN_5_MIN` |
| 报警动画时长 | 9s（18 × 500ms） | `display.c` DISPLAY_MODE_TIMER_ALARM |
| 温区阈值 | 36.0℃ / 40.0℃ | `TEMP_ZONE_NORMAL` / `TEMP_ZONE_HOT` |
| 低电阈值 | 30% | `BATTERY_LEVEL_L2` |
| 闪烁周期 | 500ms | `display.c` Display_Scan_ISR |
| 温度滤波 | 4 次平均 + 1/16 递推平均 | `temp.c` |
| 电量滤波 | 4 次平均 + 1/8 递推平均 | `battery.c` |
| ADC 分辨率 / 参考 | 12bit / VDD（温度）、2V（电量） | `temp.h` / `battery.h` |

## 构建

开发环境：**NYIDE 6.11 + NYC_NY8 2.31**（LLVM/Clang 22，CPU 目标 `ny8bm84a`）。

```powershell
# 将工具链加入 PATH（安装路径按实际环境调整）
$env:PATH = "C:\Nyquest\NYC_NY8\llvm\bin;C:\Nyquest\NYIDE\bin\toolchain;" + $env:PATH

cd project\App\DL-14542\DL-14542
make all      # 编译 + 链接，输出 DL-14542.elf / DL-14542.lst / DL-14542.map
make asm      # 额外输出汇编 DL-14542.s
make clean    # 清除 OBJ 与构建产物
```

也可以直接用 NYIDE 打开 [`DL-14542.naprj`](project/App/DL-14542/DL-14542/DL-14542.naprj) 编译下载；
配置字（WDT、LVR、振荡频率等）保存在 [`DL-14542.cb`](project/App/DL-14542/DL-14542/DL-14542.cb)，
编译后的检查清单见 `DL-14542.htm`。

VS Code 下的 IntelliSense / 语法检查配置（含 clangd 工具链路径与宏定义）见
[`.vscode/c_cpp_properties.json`](.vscode/c_cpp_properties.json)。

## 目录结构

```
├── project/
│   ├── App/DL-14542/            NYIDE 工程
│   │   ├── DL-14542.naprj       工程文件（芯片、选项、配置字）
│   │   ├── DL-14542.NySln       解决方案
│   │   ├── DL-14542.cb          配置字（WDT / LVR / IRC 等）
│   │   ├── makefile             clang/LLVM 构建脚本
│   │   └── DL-14542/
│   │       ├── User/            应用源码（main/system/display/temp/battery/input/gpio/adc）
│   │       └── Include/         模块头文件
│   └── Hardware/                PCB 工程与 UI 设计稿
├── .vscode/c_cpp_properties.json
├── LICENSE                      Apache License 2.0
└── README.md
```

## 许可证

[Apache License 2.0](LICENSE)

