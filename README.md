# 洗衣机 HIL 仿真平台 (WashingMachineHIL)

洗衣机**硬件在环 (Hardware-in-the-Loop)** 仿真与测试平台。PC 端运行物理仿真模型，通过串口向 STM32 下位机发送模拟传感器信号，下位机将其转换为真实电信号喂给被测洗衣机主控板；同时采集主控板的驱动输出回灌到 PC 端仿真模型，形成完整闭环。

```
┌─────────────────────────────────────────────────────────────┐
│                    PC 上位机 (Qt Quick)                       │
│                                                             │
│  SimulationEngine ──► IntegrationBridge ──► SerialManager   │
│  (物理模型/状态机)     (闭环桥/故障注入)      (串口/协议)     │
│                                                             │
│  QML UI: 3D 可视化 · 控制面板 · 数据监控 · 串口调试          │
└──────────────────────────┬──────────────────────────────────┘
                           │ UART 115200 8N1
┌──────────────────────────▼──────────────────────────────────┐
│              STM32F103ZET6 下位机固件                         │
│                                                             │
│  PWM 输出 → 水位/转速传感器频率    DAC 输出 → NTC 温度模拟    │
│  GPIO 输出 → 门锁/进水/排水/加热   GPIO 输出 → 门开关信号     │
│  GPIO 输入 ← 主控驱动采集          ADC 输入 ← 电机 PWM 占空比 │
└─────────────────────────────────────────────────────────────┘
```

---

## 目录结构

```
vm-washing/
├── pc-app/                  # PC 上位机
│   ├── src/
│   │   ├── core/            # 仿真引擎、集成桥、故障注入、数据记录
│   │   ├── models/          # 物理模型 (水位/温度/电机/门锁/执行器)
│   │   ├── communication/   # 串口管理、协议编解码、CRC16
│   │   └── ui/qml/          # QML 界面
│   ├── config/              # 仿真参数 & 洗涤场景 JSON
│   └── tests/               # 单元测试 (Qt Test)
├── firmware/                # STM32 下位机固件
│   ├── Core/Src/            # C 源码
│   ├── Core/Inc/            # 头文件
│   └── startup/             # 启动文件
├── build/                   # 构建产物 (git-ignored)
├── CMakePresets.json         # 上位机 CMake 预设
├── all.bat                  # 统一入口脚本
├── build.bat                # 固件构建脚本
├── flash.bat                # 固件烧写脚本
└── debug.bat                # 固件调试脚本
```

---

## 环境依赖

| 依赖 | 版本/路径 | 用途 |
|------|-----------|------|
| Qt | 6.5.3 (`D:/Qt/6.5.3/mingw_64`) | 上位机框架 |
| MinGW | 11.2.0 (`D:/Qt/Tools/mingw1120_64`) | 上位机编译器 |
| CMake | ≥ 3.21 | 构建系统 |
| Ninja | (`D:/Qt/Tools/Ninja`) | 构建后端 |
| ARM GCC | 14.3 rel1 (`D:/arm-gnu-toolchain-14.3.rel1-*`) | 固件交叉编译器 |
| OpenOCD | (`D:/openocd-20260302/OpenOCD-20260302-0.12.0`) | SWD 烧写 & 调试 |
| ST-Link | V2 | 硬件调试器 |

> **提示**：工具链路径均写在 `CMakePresets.json` 和 `.bat` 脚本顶部，换版本只需改一处。

---

## 上位机 (PC) 调试指令

### 使用 CMake Presets（推荐）

```powershell
# ─── 配置 ───
cmake --preset mingw-debug              # Debug 配置
cmake --preset mingw-release            # Release 配置

# ─── 构建 ───
cmake --build --preset mingw-debug      # Debug 构建
cmake --build --preset mingw-release    # Release 构建

# ─── 运行上位机 ───
.\build\mingw-debug\pc-app\appWashingMachineHIL.exe

# ─── 单元测试 ───
ctest --preset mingw-debug              # 运行物理模型测试
```

### 使用 VSCode

按 `F5` 选择对应调试配置：

| 配置名 | 说明 |
|--------|------|
| **调试上位机 (appWashingMachineHIL)** | 构建 + 启动 Debug，支持 C++/QML 断点 |
| **调试物理模型单元测试 (tst_physicsmodels)** | 构建 + 运行单元测试并调试 |
| **调试固件 (OpenOCD + ST-Link SWD)** | 构建固件 + OpenOCD 烧写 + SWD 在线调试 |
| **挂载到运行中的固件 (不复位)** | Attach 到已运行的 MCU，不重置 |

### 快捷键

| 按键 | 功能 |
|------|------|
| `Space` | 暂停 / 继续仿真 |
| `R` | 重置所有状态 |
| `D` | 折叠 / 展开底部面板 |

---

## 下位机 (Firmware) 调试指令

### 方式一：批处理脚本

项目根目录下提供了一组 `.bat` 脚本，**双击或在终端运行即可**：

```cmd
:: 统一入口菜单（构建/烧写/调试/清理）
all.bat

:: 单独操作
build.bat               :: 构建固件（增量/全量自动判断）
flash.bat               :: 烧写固件（4 种模式可选）
debug.bat               :: 启动调试（OpenOCD / VSCode / GDB）
```

#### `all.bat` — 统一入口

```
 1. Build firmware        → 调用 build.bat
 2. Build and flash       → 先 build 再 flash
 3. Flash only            → 仅烧写
 4. Debug                 → 调用 debug.bat
 5. Clean                 → 删除 build 目录
 6. Exit
```

#### `build.bat` — 构建固件

自动检测增量/全量构建，产物位于 `build/` 目录：

```
build\fw-debug\washer_hil_fw.elf     ← Debug 版 (带符号)
build\fw-release\washer_hil_fw.elf   ← Release 版 (优化)
```

#### `flash.bat` — 烧写固件

提供 4 种烧写模式：

```
1. Flash and Run       → 烧写后立即运行
2. Flash only (halt)   → 烧写后停在入口（便于调试器接管）
3. Erase chip          → 全片擦除（需二次确认）
4. Flash and Reset     → 加锁后烧写并复位
```

#### `debug.bat` — 调试固件

```
1. Start OpenOCD server   → 仅启动 OpenOCD GDB Server (localhost:3333)
2. Start VSCode debug     → 打开 VSCode (配合 launch.json)
3. Command line GDB       → 命令行 arm-none-eabi-gdb 连接
4. Flash and debug        → 烧写 + 自动启动 GDB 调试
```

### 方式二：CMake Presets（命令行）

```powershell
# ─── 固件配置 ───
cd firmware
cmake --preset fw-debug             # Debug 配置 (-Og -g3)
cmake --preset fw-release           # Release 配置 (-O2)

# ─── 固件构建 ───
cmake --build --preset fw-debug -j  # Debug 构建
cmake --build --preset fw-release -j # Release 构建

# ─── 一键烧写 (CMake 内置 flash target) ───
cmake --build --preset fw-release --target flash
```

### 方式三：VSCode Cortex-Debug

1. 安装扩展 **Cortex-Debug**
2. 按 `F5` → 选择 **"调试固件 (OpenOCD + ST-Link SWD)"**
3. 自动完成：构建 → 烧写 → 运行到 `main()` → 可设断点

> SVD 文件已配置为 `STM32F103.svd`，可在调试时查看外设寄存器。

---

## 通信协议

上位机与下位机通过自定义二进制帧协议通信：

```
+------+-----+-----+---------+-------+-------+------+
| 0xAA | CMD | LEN | PAYLOAD | CRC_L | CRC_H | 0x55 |
|  1B  | 1B  | 1B  | 0-250B  |  1B   |  1B   |  1B  |
+------+-----+-----+---------+-------+-------+------+
```

- CRC16-CCITT，覆盖 CMD + LEN + PAYLOAD
- 多字节数值统一小端序

| 命令 | 码 | 方向 | 载荷 | 说明 |
|------|-----|------|------|------|
| `BATCH_SET` | 0x05 | PC→MCU | 16B | 批量下发传感器+执行器状态 |
| `REPORT_INPUT` | 0x81 | MCU→PC | 16B | 上报主控继电器指令+ADC值 |
| `HEARTBEAT` | 0xFE | 双向 | 0B | 链路保活 (500ms) |

### BATCH_SET 载荷布局 (16B)

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | waterLevelFreq | 水位传感器频率 (Hz) |
| 4 | 2 | ntcDacValue | NTC DAC 值 (12bit) |
| 6 | 4 | hallFreq | Hall 频率 (Hz) |
| 10 | 1 | doorLock | 门锁 (0=解锁, 1=上锁) |
| 11 | 1 | inletValve | 进水阀 |
| 12 | 1 | drainPump | 排水泵 |
| 13 | 1 | heater | 加热器 |
| 14 | 1 | doorSignal1 | DOOR_1 (1=门开) |
| 15 | 1 | doorSignal2 | DOOR_2 (1=门关) |

### 链路时序

| 事件 | 频率/间隔 |
|------|-----------|
| STM32 上报 `REPORT_INPUT` | 100Hz (10ms) |
| PC 下发 `BATCH_SET` | 仿真运行时 10ms 周期 |
| 心跳 `HEARTBEAT` | 500ms |
| 断连超时 | 2s 无有效帧 |
| 自动重连 | 3s 间隔 |

---

## 洗涤场景配置

场景 JSON 位于 `pc-app/config/scenarios/`，构建时自动复制到可执行文件旁的 `config/` 目录。

示例 — 标准洗涤 (`standard_wash.json`)：

```json
{
  "name": "标准洗涤",
  "steps": [
    { "action": "lockDoor", "waitUntil": "doorLocked" },
    { "actuators": { "inletValveOpen": true }, "waitUntil": "waterVolume >= 30.0" },
    { "actuators": { "heaterOn": true }, "waitUntil": "temperature >= 40.0" },
    { "motor": { "pwmDuty": 0.35, "reverseIntervalSec": 15 }, "durationSec": 900 },
    { "actuators": { "drainPumpOn": true }, "waitUntil": "waterVolume <= 1.0" },
    { "motor": { "targetSpeedRpm": 1200 }, "durationSec": 300 },
    { "action": "unlockDoor" }
  ]
}
```

新增场景：在 `config/scenarios/` 下添加 JSON 文件，界面会自动扫描并列出。

---

## 快速开始

```powershell
# 1. 构建上位机
cmake --preset mingw-debug
cmake --build --preset mingw-debug

# 2. 构建固件
cd firmware; cmake --preset fw-debug; cmake --build --preset fw-debug -j; cd ..

# 3. 烧写固件（连接 ST-Link 后）
flash.bat

# 4. 启动上位机
.\build\mingw-debug\pc-app\appWashingMachineHIL.exe

# 5. 在界面中选择串口号 → 连接 → 启动仿真
```

或直接用 `all.bat` 按菜单引导操作。
