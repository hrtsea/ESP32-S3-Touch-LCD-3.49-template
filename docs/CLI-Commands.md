# CLI 命令参考

本项目通过 USB-Serial-JTAG 提供一个交互式命令行界面（REPL），用于调试和配置。串口参数为 115200 8N1，可通过 `idf.py monitor` 或任意串口终端访问。

提示符为 `esp> `，输入 `help` 可查看所有可用命令。

源码实现：[main/utils/cli.c](file:///f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/utils/cli.c)

---

## 命令一览

| 命令 | 参数 | 作用 |
|------|------|------|
| `help` | 无 | 列出所有已注册命令（ESP-IDF 内置） |
| `mem` | 无 | 显示堆内存使用情况 |
| `wifi` | 无 | 显示当前 Wi-Fi 连接信息 |
| `wifi_connect` | `<ssid> [pass]` | 保存凭据并连接 Wi-Fi |
| `wifi_clear` | 无 | 清除所有已保存的 Wi-Fi 凭据 |
| `audio_off` | 无 | 关闭音频子系统 |
| `sd_info` | 无 | 显示 SD 卡容量信息 |
| `sd_format` | 无 | 格式化 SD 卡 |
| `bl` | `<0..255>` | 设置屏幕背光亮度 |
| `no_sleep` | 无 | 禁用屏幕自动熄屏 |
| `lang` | `[idx]` | 显示或设置 UI 语言 |

---

## 命令详解

### help

```
esp> help
```

列出所有已注册的命令及其简要帮助。该命令由 ESP-IDF 的 `esp_console` 组件自动注册，无需手动实现。

---

### mem

```
esp> mem
```

显示三类堆内存的空闲字节数与最大可用连续块大小：

- **internal** — 内部 RAM（`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`）
- **DMA** — 可用于 DMA 的内存（`MALLOC_CAP_DMA`）
- **PSRAM** — 外部 SPI RAM（`MALLOC_CAP_SPIRAM`）

**示例输出：**

```
internal: free=8220023 largest=8214536  DMA: free=66899 largest=65536  PSRAM: free=8145336 largest=8145336
```

**用途：** 排查内存泄漏、确认 PSRAM 是否正常工作、检查 DMA 缓冲区是否可分配。

---

### wifi

```
esp> wifi
```

显示当前 Wi-Fi STA 连接状态，包括：

- **ssid** — 已关联的 AP SSID
- **rssi** — 信号强度（dBm）
- **ip** — 分配到的 IPv4 地址

若未关联，输出 `not associated`。

**示例输出：**

```
ssid=dd-wrt rssi=-41 ip=192.168.3.138
```

**用途：** 快速确认 Wi-Fi 连接是否成功、信号强度、IP 地址分配情况。

---

### wifi_connect

```
esp> wifi_connect <ssid> [pass]
```

将 SSID 和密码保存到 NVS，并触发 Wi-Fi 关联。

| 参数 | 必填 | 说明 |
|------|------|------|
| `<ssid>` | 是 | Wi-Fi SSID |
| `[pass]` | 否 | Wi-Fi 密码（开放网络可省略） |

**示例：**

```
esp> wifi_connect MyHomeAP mypassword
wifi_connect: ssid=MyHomeAP pass_len=10
```

**用途：** 在不重启进入配网流程的情况下，直接通过串口配置 Wi-Fi。

---

### wifi_clear

```
esp> wifi_clear
```

清除 NVS 中 `wifi` 命名空间下的所有 Wi-Fi 凭据，并断开当前连接。重启后设备将进入配网模式。

**示例输出：**

```
clearing wifi credentials...
wifi credentials cleared, restart to enter provisioning mode
```

**用途：** 切换网络环境、排查配网问题、恢复出厂 Wi-Fi 配置。

---

### audio_off

```
esp> audio_off
```

调用 `audio_min_shutdown()` 关闭音频子系统，释放 I2S 驱动和编解码器资源。

**示例输出：**

```
calling audio_min_shutdown()
ok
```

**用途：** 释放音频占用的内存和 I2S 总线、排查音频冲突导致的崩溃。

---

### sd_info

```
esp> sd_info
```

显示已挂载 SD 卡（`/sdcard`）的总容量和剩余空间。

**示例输出：**

```
sd: total=7632 MB free=7584 MB (99.4%)
```

若 SD 卡未挂载，输出 `sd: not mounted`。

**用途：** 确认 SD 卡是否正常挂载、检查剩余空间。

---

### sd_format

```
esp> sd_format
```

格式化 SD 卡并重新创建 `/sdcard/recordings` 目录。该操作会删除 SD 卡上所有数据，耗时约 30-90 秒。

**示例输出：**

```
formatting SD (this can take 30-90s)...
format -> ESP_OK
```

**用途：** 恢复 SD 卡到初始状态、排查文件系统损坏。

> **警告：** 该操作不可逆，会清除 SD 卡上的所有数据。

---

### bl

```
esp> bl <0..255>
```

设置屏幕背光亮度，并持久化到配置。

| 参数 | 范围 | 说明 |
|------|------|------|
| `<value>` | 0 - 255 | 0 为全暗，255 为全亮 |

**示例：**

```
esp> bl 128
brightness=128
```

**用途：** 调试背光驱动、夜间降低亮度、测试背光 PWM 是否工作。

---

### no_sleep

```
esp> no_sleep
```

禁用屏幕自动变暗和自动熄屏功能，将 `dim_s` 和 `off_s` 都设为 0。

**示例输出：**

```
dim_s=0 off_s=0 (never dim/off)
```

**用途：** 调试时保持屏幕常亮、演示场景下防止息屏。

---

### lang

```
esp> lang [idx]
```

显示或设置 UI 语言。

| 参数 | 说明 |
|------|------|
| 无参数 | 显示当前语言索引 |
| `<idx>` | 设置语言：0=English, 1=中文, 2=日本語, 3=한국어 |

**示例：**

```
esp> lang
lang = 0

esp> lang 1
lang -> 1 (reboot or re-enter menu to refresh labels)
```

**用途：** 切换界面语言、测试多语言翻译。

> **注意：** 设置后需要重启或重新进入菜单才能刷新界面文字。

---

## 访问方式

CLI 通过 USB-Serial-JTAG 接口提供，无需额外 USB-UART 芯片。访问方式：

1. **idf.py monitor**（推荐）：
   ```
   idf.py monitor
   ```

2. **串口终端**：使用任意串口工具（如 PuTTY、Tera Term、screen）连接到对应的 COM 端口，波特率 115200。

3. **输入命令**：启动后会出现 `esp> ` 提示符，直接输入命令并回车。

依赖配置项 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`（在 `sdkconfig.defaults` 中已设置）。
