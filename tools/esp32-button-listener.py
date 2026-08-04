#!/usr/bin/env python3
"""
ESP32-S3 USB HID Button Listener

监听 ESP32-S3 发送的 F13-F16 按键事件，并执行相应的动作。
这个脚本运行在 PC/Linux 端，接收来自 ESP32-S3 的 USB HID 按键事件。

使用方法:
    sudo python3 esp32-button-listener.py

依赖:
    pip install evdev

systemd 服务配置见下方注释
"""

import evdev
import subprocess
import logging
import time
import sys

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - [ESP32-BUTTON] %(message)s',
    handlers=[
        logging.FileHandler('/var/log/esp32-buttons.log'),
        logging.StreamHandler()
    ]
)

# 按键码定义（与 ESP32 固件对应）
KEY_F13 = 0x68
KEY_F14 = 0x69
KEY_F15 = 0x6A
KEY_F16 = 0x6B

# 按键动作配置
BUTTON_ACTIONS = {
    KEY_F13: {
        'name': 'Boot Button Single Click',
        'command': '/usr/local/bin/boot-single-click.sh'
    },
    KEY_F14: {
        'name': 'Boot Button Long Press',
        'command': '/usr/local/bin/boot-long-press.sh'
    },
    KEY_F15: {
        'name': 'Power Button Single Click',
        'command': '/usr/local/bin/power-single-click.sh'
    },
    KEY_F16: {
        'name': 'Power Button Long Press',
        'command': '/usr/local/bin/power-long-press.sh'
    },
}

def find_esp32_hid_device():
    """
    查找 ESP32-S3 HID 设备
    返回 evdev.InputDevice 对象，如果未找到则返回 None
    """
    devices = [evdev.InputDevice(path) for path in evdev.list_devices()]

    for dev in devices:
        logging.debug(f"检查设备: {dev.name} ({dev.path})")

        # ESP32-S3 HID 设备的可能名称
        keywords = ['esp32', 'hid', 'keyboard', 'generic']
        if any(keyword in dev.name.lower() for keyword in keywords):
            logging.info(f"找到 HID 设备: {dev.name} ({dev.path})")
            return dev

    return None

def execute_command(command):
    """
    执行按键关联的命令
    """
    logging.info(f"执行命令: {command}")

    try:
        subprocess.Popen(
            command,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
    except Exception as e:
        logging.error(f"执行命令失败: {e}")

def main():
    """主监听循环"""
    logging.info("=" * 60)
    logging.info("ESP32-S3 USB HID 按键监听服务启动")
    logging.info("=" * 60)

    # 查找设备
    device = find_esp32_hid_device()

    if not device:
        logging.error("未找到 ESP32-S3 HID 设备")
        logging.error("请检查:")
        logging.error("  1. ESP32-S3 已通过 USB 连接到 PC")
        logging.error("  2. ESP32-S3 固件已启用 USB HID")
        logging.error("  3. 运行 'lsusb' 查看设备是否识别")
        return 1

    logging.info(f"监听设备: {device.name}")
    logging.info(f"设备路径: {device.path}")
    logging.info("")
    logging.info("按键映射:")
    for key_code, config in BUTTON_ACTIONS.items():
        logging.info(f"  F{key_code - 0x67 + 12} (0x{key_code:02X}) → {config['name']}")
    logging.info("")
    logging.info("按 Ctrl+C 停止监听")
    logging.info("-" * 60)

    try:
        for event in device.read_loop():
            if event.type == evdev.ecodes.EV_KEY:
                key_code = event.code
                key_value = event.value

                # 仅处理按下事件 (value=1)
                if key_value == 1 and key_code in BUTTON_ACTIONS:
                    config = BUTTON_ACTIONS[key_code]
                    logging.info(f">>> {config['name']}")
                    execute_command(config['command'])

    except KeyboardInterrupt:
        logging.info("\n服务停止（用户中断）")
    except Exception as e:
        logging.error(f"监听异常: {e}")
        import traceback
        logging.error(traceback.format_exc())

    return 0

if __name__ == "__main__":
    sys.exit(main())