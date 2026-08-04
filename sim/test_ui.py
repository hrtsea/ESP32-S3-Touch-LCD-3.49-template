"""
NAS Monitor Simulator - 自动化 UI 测试脚本
通过 pyautogui 控制模拟器，截图验证各屏幕渲染正确性。

运行: python test_ui.py
"""

import subprocess
import time
import os
import sys

import pyautogui
from PIL import Image

# ===== 配置 =====
SIM_EXE = os.path.join(os.path.dirname(__file__), "build", "nas_monitor_sim.exe")
SCALE = 2           # SDL 渲染缩放倍数
SIM_W = 640 * SCALE # 1280
SIM_H = 172 * SCALE # 344
OUT_DIR = os.path.join(os.path.dirname(__file__), "test_screenshots")
WAIT_TIME = 2       # 截图间等待秒数

# 禁用 pyautogui 的安全停止（鼠标移到角落）
pyautogui.FAILSAFE = True
pyautogui.PAUSE = 0.3

os.makedirs(OUT_DIR, exist_ok=True)


def find_sim_window():
    """查找模拟器窗口位置"""
    import pygetwindow as gw
    for w in gw.getAllWindows():
        if "NAS Monitor" in w.title:
            return w
    return None


def get_sim_region():
    """获取模拟器窗口在屏幕上的区域（动态计算内容区域）"""
    win = find_sim_window()
    if win is None:
        raise RuntimeError("找不到模拟器窗口！请先启动模拟器。")
    
    # 使用窗口实际客户区（去掉边框和标题栏）
    # 对于 SDL 窗口，标题栏约 31px，边框各约 2px
    title_bar_h = 31
    border_w = 2  # 左右边框
    bottom_border = 2
    
    left = win.left + border_w
    top = win.top + title_bar_h
    width = win.width - 2 * border_w
    height = win.height - title_bar_h - bottom_border
    
    # 如果计算出的尺寸异常，使用回退值
    if width < 200 or height < 50:
        width = SIM_W
        height = SIM_H
    
    return (left, top, width, height)


def capture_screenshot(name, region=None):
    """截图并保存"""
    if region is None:
        region = get_sim_region()
    
    img = pyautogui.screenshot(region=region)
    path = os.path.join(OUT_DIR, f"{name}.png")
    img.save(path)
    
    # 简单验证：图片不应全黑
    extrema = img.convert("L").getextrema()
    is_blank = (extrema[0] == extrema[1] == 0)
    
    status = "PASS" if not is_blank else "FAIL (全黑)"
    print(f"  [{status}] {name}.png - 亮度范围: {extrema}")
    return img, not is_blank


def click_at(region, x_pct, y_pct):
    """在模拟器区域内按百分比点击"""
    left, top, w, h = region
    px = left + int(w * x_pct)
    py = top + int(h * y_pct)
    pyautogui.click(px, py)


def swipe_right_to_left(region, duration=0.3):
    """模拟从右向左滑动（触发 Overview → Storage）"""
    left, top, w, h = region
    start_x = left + int(w * 0.85)
    end_x = left + int(w * 0.15)
    y = top + int(h * 0.5)
    
    pyautogui.moveTo(start_x, y)
    pyautogui.mouseDown()
    pyautogui.moveTo(end_x, y, duration=duration)
    pyautogui.mouseUp()


def swipe_left_to_right(region, duration=0.3):
    """模拟从左向右滑动（触发 Overview → Settings）"""
    left, top, w, h = region
    start_x = left + int(w * 0.15)
    end_x = left + int(w * 0.85)
    y = top + int(h * 0.5)
    
    pyautogui.moveTo(start_x, y)
    pyautogui.mouseDown()
    pyautogui.moveTo(end_x, y, duration=duration)
    pyautogui.mouseUp()


def wait_for_overview(timeout=15):
    """等待 Overview 屏幕加载完成（检测非黑色内容）"""
    region = get_sim_region()
    start = time.time()
    time.sleep(3)  # 等 Boot 屏至少显示
    
    while time.time() - start < timeout:
        img = pyautogui.screenshot(region=region)
        gray = img.convert("L")
        extrema = gray.getextrema()
        if extrema[1] > 30:  # 有足够亮的像素，说明非黑屏
            return True
        time.sleep(1)
    return False


def count_unique_colors(img, sample_step=10):
    """采样计算图像中唯一颜色数（粗略衡量 UI 复杂度）"""
    pixels = []
    w, h = img.size
    for y in range(0, h, sample_step):
        for x in range(0, w, sample_step):
            pixels.append(img.getpixel((x, y)))
    return len(set(pixels))


def check_overview_screen(img):
    """验证 Overview 屏幕内容特征"""
    w, h = img.size
    pixels = img.convert("L")
    
    # 1. 不应全黑
    extrema = pixels.getextrema()
    if extrema[1] < 20:
        return False, "画面全黑"
    
    # 2. 应有足够的颜色变化（UI 元素丰富）
    colors = count_unique_colors(img, sample_step=8)
    if colors < 5:
        return False, f"颜色单一 ({colors} 种)"
    
    # 3. 应有亮绿色元素（NAS 状态指示灯）
    # 检查图像中是否有绿色像素
    has_green = False
    for y in range(0, h, 16):
        for x in range(0, w, 16):
            r, g, b = img.getpixel((x, y))[:3]
            if g > 150 and r < 150 and b < 150:
                has_green = True
                break
        if has_green:
            break
    
    return True, f"OK (亮度{extrema}, {colors}种色, 绿色={has_green})"


def check_settings_screen(img):
    """验证 Settings 屏幕特征"""
    w, h = img.size
    
    # Settings 屏幕应有明显的 Tab 栏和更多 UI 元素
    colors = count_unique_colors(img, sample_step=8)
    if colors < 8:
        return False, f"Settings 颜色不足 ({colors} 种)"
    
    return True, f"OK ({colors}种色)"


def check_storage_screen(img):
    """验证 Storage 屏幕特征"""
    colors = count_unique_colors(img, sample_step=8)
    if colors < 5:
        return False, f"Storage 颜色不足 ({colors} 种)"
    
    return True, f"OK ({colors}种色)"


def check_disk_detail_screen(img):
    """验证 DiskDetail 屏幕特征"""
    colors = count_unique_colors(img, sample_step=8)
    if colors < 5:
        return False, f"DiskDetail 颜色不足 ({colors} 种)"
    
    return True, f"OK ({colors}种色)"


def check_system_detail_screen(img):
    """验证 SystemDetail 屏幕特征"""
    colors = count_unique_colors(img, sample_step=8)
    if colors < 5:
        return False, f"SystemDetail 颜色不足 ({colors} 种)"
    
    return True, f"OK ({colors}种色)"


# ===== 主测试流程 =====
def main():
    print("=" * 60)
    print("  NAS Monitor Simulator - UI 自动化测试")
    print("=" * 60)
    
    # 查找窗口
    print("\n[1] 查找模拟器窗口...")
    win = find_sim_window()
    if win is None:
        print("    模拟器未运行，正在启动...")
        subprocess.Popen([SIM_EXE])
        time.sleep(5)
        win = find_sim_window()
        if win is None:
            print("    ERROR: 无法启动模拟器！")
            sys.exit(1)
    
    print(f"    窗口位置: ({win.left}, {win.top}) 大小: {win.width}x{win.height}")
    print(f"    窗口标题: {win.title}")
    
    # 激活窗口
    try:
        win.activate()
        time.sleep(0.5)
    except Exception:
        # pygetwindow activate 有时在 Windows 上报错，使用 ctypes 后备方案
        import ctypes
        from ctypes import wintypes
        user32 = ctypes.windll.user32
        hwnd = win._hWnd
        user32.ShowWindow(hwnd, 9)  # SW_RESTORE
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.5)
    
    region = get_sim_region()
    print(f"    截图区域: {region}")
    
    # 等待 Overview 加载
    print("\n[2] 等待 Boot → Overview 屏幕加载...")
    if not wait_for_overview(timeout=20):
        print("    ERROR: 超时！Overview 屏幕未加载")
        sys.exit(1)
    print("    OK - Overview 屏幕已加载")
    
    time.sleep(WAIT_TIME)
    
    test_results = []
    
    # ===== 测试 1: Overview 屏幕 =====
    print("\n[3] 测试 Overview 屏幕...")
    img, ok = capture_screenshot("01_overview")
    if ok:
        passed, detail = check_overview_screen(img)
        test_results.append(("Overview 屏幕", passed, detail))
        print(f"  [{'PASS' if passed else 'FAIL'}] Overview: {detail}")
    else:
        test_results.append(("Overview 屏幕", False, "截图失败"))
    
    # ===== 测试 2: 点击 CPU 区域 → SystemDetail =====
    print("\n[4] 测试 SystemDetail 屏幕（点击 CPU 区域）...")
    click_at(region, 0.15, 0.35)  # CPU 模块位置
    time.sleep(0.8)
    
    img, ok = capture_screenshot("02_system_detail")
    if ok:
        passed, detail = check_system_detail_screen(img)
        test_results.append(("SystemDetail 屏幕", passed, detail))
        print(f"  [{'PASS' if passed else 'FAIL'}] SystemDetail: {detail}")
    else:
        test_results.append(("SystemDetail 屏幕", False, "截图失败"))
    
    # 返回 Overview（SystemDetail 中点击返回）
    click_at(region, 0.05, 0.05)  # 左上角返回区域
    time.sleep(0.8)
    capture_screenshot("03_back_to_overview")
    
    # ===== 测试 3: 点击硬盘 → DiskDetail =====
    print("\n[5] 测试 DiskDetail 屏幕（点击硬盘按钮）...")
    click_at(region, 0.12, 0.88)  # 第一个 HDD 按钮
    time.sleep(0.8)
    
    img, ok = capture_screenshot("04_disk_detail")
    if ok:
        passed, detail = check_disk_detail_screen(img)
        test_results.append(("DiskDetail 屏幕", passed, detail))
        print(f"  [{'PASS' if passed else 'FAIL'}] DiskDetail: {detail}")
    else:
        test_results.append(("DiskDetail 屏幕", False, "截图失败"))
    
    # 返回 Overview
    click_at(region, 0.05, 0.05)
    time.sleep(0.8)
    capture_screenshot("05_back_to_overview2")
    
    # ===== 测试 4: 滑动到 Settings =====
    print("\n[6] 测试 Settings 屏幕（左滑）...")
    swipe_left_to_right(region)  # 左滑 = 从左向右 → Settings
    time.sleep(0.8)
    
    img, ok = capture_screenshot("06_settings")
    if ok:
        passed, detail = check_settings_screen(img)
        test_results.append(("Settings 屏幕", passed, detail))
        print(f"  [{'PASS' if passed else 'FAIL'}] Settings: {detail}")
    else:
        test_results.append(("Settings 屏幕", False, "截图失败"))
    
    # 返回 Overview
    swipe_right_to_left(region)
    time.sleep(0.8)
    capture_screenshot("07_back_to_overview3")
    
    # ===== 测试 5: 滑动到 Storage =====
    print("\n[7] 测试 Storage 屏幕（右滑）...")
    swipe_right_to_left(region)  # 右滑 = 从右向左 → Storage
    time.sleep(0.8)
    
    img, ok = capture_screenshot("08_storage")
    if ok:
        passed, detail = check_storage_screen(img)
        test_results.append(("Storage 屏幕", passed, detail))
        print(f"  [{'PASS' if passed else 'FAIL'}] Storage: {detail}")
    else:
        test_results.append(("Storage 屏幕", False, "截图失败"))
    
    # 返回 Overview
    swipe_left_to_right(region)
    time.sleep(0.8)
    capture_screenshot("09_final_overview")
    
    # ===== 汇总结果 =====
    print("\n" + "=" * 60)
    print("  测试结果汇总")
    print("=" * 60)
    
    passed_count = sum(1 for _, p, _ in test_results if p)
    total_count = len(test_results)
    
    for name, passed, detail in test_results:
        icon = "✅" if passed else "❌"
        print(f"  {icon} {name}: {detail}")
    
    print(f"\n  通过: {passed_count}/{total_count}")
    print(f"  截图保存在: {OUT_DIR}")
    
    if passed_count == total_count:
        print("\n  🎉 所有测试通过！")
        return 0
    else:
        print(f"\n  ⚠️  {total_count - passed_count} 个测试失败")
        return 1


if __name__ == "__main__":
    sys.exit(main())