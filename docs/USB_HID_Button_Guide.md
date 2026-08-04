# ESP32-S3 USB HID 按键集成指南

## 概述

本文档说明如何将 USB HID 功能集成到 ESP32-S3 项目中，实现物理按键事件通过 USB 发送到 PC 端。

## 系统架构

```
┌─────────────────────────────────────┐
│  ESP32-S3 固件                      │
│  ┌───────────────────────────────┐  │
│  │ button_bsp 组件               │  │
│  │  ├─ 按键扫描                  │  │
│  │  ├─ 按键事件检测              │  │
│  │  └─ USB HID 发送 ←── 新增    │  │
│  └───────────────────────────────┘  │
│              ↓ USB HID 事件          │
└──────────────┼──────────────────────┘
               │
        ┌──────┴─────┐
        │  USB-C 线缆 │
        └──────┬─────┘
               │
┌──────────────┴──────────────────────┐
│  PC/Linux 主机                      │
│  ┌───────────────────────────────┐  │
│  │ esp32-button-listener.py      │  │
│  │  ├─ 监听 /dev/input/eventX   │  │
│  │  ├─ F13 → Boot 键单击         │  │
│  │  ├─ F14 → Boot 键长按         │  │
│  │  ├─ F15 → Power 键单击        │  │
│  │  └─ F16 → Power 键长按        │  │
│  └───────────────────────────────┘  │
└──────────────────────────────────────┘
```

## 按键映射表

| ESP32 按键 | 事件类型 | HID 按键码 | PC 端动作 |
|-----------|---------|-----------|----------|
| Boot 键 (GPIO 0) | 单击 | F13 (0x68) | `/usr/local/bin/boot-single-click.sh` |
| Boot 键 (GPIO 0) | 长按 | F14 (0x69) | `/usr/local/bin/boot-long-press.sh` |
| Power 键 (GPIO 16) | 单击 | F15 (0x6A) | `/usr/local/bin/power-single-click.sh` |
| Power 键 (GPIO 16) | 长按 | F16 (0x6B) | `/usr/local/bin/power-long-press.sh` |

## 编译和烧录

### 1. 配置项目

项目已自动配置 USB HID 支持（见 `sdkconfig.defaults`）。

### 2. 编译固件

```bash
cd e:\zotlabnas-split\ESP32-S3-Touch-LCD-3.49-template
idf.py build
```

### 3. 烧录固件

```bash
idf.py -p COM<端口号> flash
```

### 4. 查看日志

```bash
idf.py -p COM<端口号> monitor
```

应该看到类似日志：

```
I (324) USB_HID: ============================================
I (324) USB_HID: USB HID component compiled (stub mode)
I (324) USB_HID: Full USB stack implementation needed
I (324) USB_HID: This component provides the API framework
I (324) USB_HID: ============================================
I (334) USB_HID: USB HID initialized (stub mode)
```

## PC 端配置（Linux）

### 1. 安装依赖

```bash
sudo apt install python3-pip
pip3 install evdev
```

### 2. 复制监听脚本

```bash
sudo cp tools/esp32-button-listener.py /usr/local/bin/
sudo chmod +x /usr/local/bin/esp32-button-listener.py
```

### 3. 创建按键动作脚本

创建示例脚本：

```bash
# Boot 键单击
sudo tee /usr/local/bin/boot-single-click.sh > /dev/null <<'EOF'
#!/bin/bash
logger -t esp32-button "Boot button single click"
echo "Boot single click" >> /var/log/esp32-button-actions.log
EOF

# Boot 键长按
sudo tee /usr/local/bin/boot-long-press.sh > /dev/null <<'EOF'
#!/bin/bash
logger -t esp32-button "Boot button long press"
echo "Boot long press" >> /var/log/esp32-button-actions.log
EOF

# Power 键单击
sudo tee /usr/local/bin/power-single-click.sh > /dev/null <<'EOF'
#!/bin/bash
logger -t esp32-button "Power button single click"
echo "Power single click" >> /var/log/esp32-button-actions.log
EOF

# Power 键长按
sudo tee /usr/local/bin/power-long-press.sh > /dev/null <<'EOF'
#!/bin/bash
logger -t esp32-button "Power button long press"
echo "Power long press" >> /var/log/esp32-button-actions.log
EOF

sudo chmod +x /usr/local/bin/boot-*.sh /usr/local/bin/power-*.sh
```

### 4. 测试监听脚本

```bash
sudo python3 /usr/local/bin/esp32-button-listener.py
```

输出示例：

```
2026-08-04 10:00:00 - [ESP32-BUTTON] ============================================================
2026-08-04 10:00:00 - [ESP32-BUTTON] ESP32-S3 USB HID 按键监听服务启动
2026-08-04 10:00:00 - [ESP32-BUTTON] ============================================================
2026-08-04 10:00:00 - [ESP32-BUTTON] 找到 HID 设备: ESP32-S3 Keyboard (/dev/input/event0)
2026-08-04 10:00:00 - [ESP32-BUTTON] 监听设备: ESP32-S3 Keyboard
2026-08-04 10:00:00 - [ESP32-BUTTON] 设备路径: /dev/input/event0
2026-08-04 10:00:00 - [ESP32-BUTTON]
2026-08-04 10:00:00 - [ESP32-BUTTON] 按键映射:
2026-08-04 10:00:00 - [ESP32-BUTTON]   F13 (0x68) → Boot Button Single Click
2026-08-04 10:00:00 - [ESP32-BUTTON]   F14 (0x69) → Boot Button Long Press
2026-08-04 10:00:00 - [ESP32-BUTTON]   F15 (0x6A) → Power Button Single Click
2026-08-04 10:00:00 - [ESP32-BUTTON]   F16 (0x6B) → Power Button Long Press
2026-08-04 10:00:00 - [ESP32-BUTTON]
2026-08-04 10:00:00 - [ESP32-BUTTON] 按 Ctrl+C 停止监听
2026-08-04 10:00:00 - [ESP32-BUTTON] ------------------------------------------------------------
```

### 5. 配置为系统服务（可选）

创建 systemd 服务：

```bash
sudo tee /etc/systemd/system/esp32-buttons.service > /dev/null <<'EOF'
[Unit]
Description=ESP32-S3 USB HID Button Listener
After=multi-user.target
Requires=multi-user.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/esp32-button-listener.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable esp32-buttons.service
sudo systemctl start esp32-buttons.service
```

查看服务状态：

```bash
sudo systemctl status esp32-buttons.service
```

## 验证测试

### 1. ESP32 端验证

按下 Boot 键或 Power 键，查看 ESP32 日志：

```bash
idf.py monitor
```

应该看到：

```
D (1234) USB_HID: Key 0x68 pressed
D (1284) USB_HID: Key 0x68 released
```

### 2. PC 端验证

查看 PC 端监听日志：

```bash
tail -f /var/log/esp32-buttons.log
```

应该看到：

```
2026-08-04 10:05:23 - [ESP32-BUTTON] >>> Boot Button Single Click
2026-08-04 10:05:23 - [ESP32-BUTTON] 执行命令: /usr/local/bin/boot-single-click.sh
```

### 3. 使用 `evtest` 工具验证（可选）

```bash
sudo apt install evtest
sudo evtest
```

选择 ESP32-S3 设备，按下按键应该看到原始事件。

## 实现说明

### 当前状态

**USB HID 组件提供的是框架代码（stub 模式）**：

- ✅ API 接口已定义
- ✅ 按键码映射已完成
- ✅ 按键系统集成已完成
- ⚠️ 实际 USB HID 协议栈需要完整实现

### 完整 USB HID 实现所需步骤

要完全启用 USB HID 功能，还需要：

1. **配置 TinyUSB 协议栈**：
   - ESP-IDF 5.3 的 USB 支持仍在演进
   - 需要配置 USB Device Stack
   - 注册 HID 设备描述符

2. **实现 USB 端点通信**：
   - 配置端点缓冲区
   - 实现 HID 报告发送
   - 处理 USB 连接状态

3. **参考资源**：
   - [ESP-IDF USB Device Stack](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html)
   - [TinyUSB Documentation](https://docs.tinyusb.org/)
   - [ESP32-S3 USB HID 示例](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device)

### 当前可用方案

在完整 USB HID 实现之前，可以考虑：

1. **使用 USB 串口模拟**：
   - ESP32-S3 通过 USB 串口发送自定义协议
   - PC 端监听串口数据

2. **使用网络通信**：
   - ESP32-S3 通过 WiFi 发送事件到 PC
   - PC 端监听网络端口

## 文件清单

```
components/usb_hid_bsp/
├── CMakeLists.txt          # 组件构建配置
├── usb_hid_bsp.h           # API 头文件
└── usb_hid_bsp.c           # 实现文件（stub mode）

components/button_bsp/
└── button_bsp.c            # 已集成 USB HID 调用

tools/
└── esp32-button-listener.py  # PC 端监听脚本

sdkconfig.defaults          # 已添加 USB HID 配置
```

## 故障排查

### 问题 1：编译错误 "usb_hid_bsp not found"

**解决方案**：
- 检查 `main/CMakeLists.txt` 是否已添加 `usb_hid_bsp` 依赖
- 确认 `components/usb_hid_bsp/` 目录存在

### 问题 2：PC 端找不到 HID 设备

**解决方案**：
- 确认 ESP32-S3 已通过 USB 连接
- 运行 `lsusb` 查看设备列表
- 检查 ESP32 固件是否正常启动

### 问题 3：按键事件未触发

**解决方案**：
- 检查 GPIO 配置是否正确
- 确认按键扫描任务是否运行
- 查看 ESP32 日志确认按键状态

## 下一步计划

1. **完整实现 USB HID 协议栈**
2. **添加更多按键事件（双击、长按保持等）**
3. **支持自定义按键映射配置**
4. **添加 Windows/macOS 支持**

## 参考链接

- [ESP-IDF USB Device Stack 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html)
- [HID Usage Tables](https://www.usb.org/sites/default/files/hut1_2.pdf)
- [Linux Input Subsystem](https://www.kernel.org/doc/html/latest/input/input.html)