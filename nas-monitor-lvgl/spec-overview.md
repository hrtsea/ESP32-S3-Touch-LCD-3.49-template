# NAS 监控屏 Overview 页面设计规范

## 1. 画布规格

| 属性 | 值 |
|---|---|
| 尺寸 | 640 × 172 px |
| 设备类型 | freeSize |
| 背景色 | `--nas-background` (#000000) |
| 前景色 | `--nas-foreground` (#ffffff) |
| 默认字体 | `--nas-font-sans`，12px |
| 默认等宽字体 | `--nas-font-mono`，12px |

---

## 2. 色彩系统

| Token | 色值 | 用途 |
|---|---|---|
| `--nas-background` | #000000 | 画布背景 |
| `--nas-foreground` | #ffffff | 主文字色 |
| `--nas-primary` | #40E0D0 (青绿) | 主色调、进度条、仪表弧线 |
| `--nas-muted` | #333333 | 分隔线、进度条底色、HDD 背景 |
| `--nas-muted-foreground` | #A0A0A0 | 次要文字 |
| `--nas-ink-3` | #666666 | 图标默认色 |
| `--nas-state-success` | #00FF00 | HDD 状态圆点（正常） |
| `--nas-state-warning` | #FF8C00 | HDD 状态圆点（警告）、温度仪表弧线 |
| `--nas-state-error` | #FF0000 | 错误状态 |
| `--nas-line` | #333333 | 线条 |
| `--nas-radius-small` | 2px | 小圆角 |
| `--nas-radius-medium` | 4px | 中圆角 |
| `--nas-radius-large` | 8px | 大圆角 |

---

## 3. 布局结构

```
screen-frame (640×172)
├── status-bar (0, 0, 640×35)
│   ├── 标题文字 "NAS Monitor"
│   ├── 时间 "14:32:10" (等宽)
│   ├── 上行速率 "▲ 0.00KB/s"
│   ├── 下行速率 "▼ 0.00KB/s"
│   ├── IP 地址 "IP: --"
│   ├── 蓝牙图标 (16×16)
│   ├── WiFi 图标 (16×16)
│   └── 分隔线 (640×2, y=35)
├── cpu-container (0, 37, 240×100)
│   ├── meter-cpu → CPU 仪表 (90×90)
│   └── meter-temp → 温度仪表 (90×90)
├── md-container (260, 37, 380×100)
│   ├── mem-container → 内存进度条 + 标签
│   └── disk-container → 磁盘进度条 + 标签
└── hdd-container (0, 137, 640×35)
    ├── hdd-0 ～ hdd-5 → HDD 标签 (71×35 × 6)
    └── hdd-6 ～ hdd-8 → M.2 标签 (71×35 × 3)
```

### 3.1 坐标汇总

| 元素 | data-dom-id | (x, y) | 尺寸 (w×h) | 右下角 |
|---|---|---|---|---|
| **状态栏** | `status-bar` | (0, 0) | 640 × 35 | (640, 35) |
| NAS Monitor | — | (5, 9.5) | 文字 | — |
| 14:32:10 | — | (110, 9.5) | 等宽文字 | — |
| ▲ 上行 | — | (250, 9.5) | 文字 | — |
| ▼ 下行 | — | (390, 9.5) | 文字 | — |
| IP: -- | — | (495, 9.5) | 文字 | — |
| 蓝牙 | — | (585, 9.5) | 16 × 16 | (601, 25.5) |
| WiFi | — | (608, 9.5) | 16 × 16 | (624, 25.5) |
| 分隔线 | — | (0, 35) | 640 × 2 | (640, 37) |
| **仪表面板** | `cpu-container` | (0, 37) | 240 × 100 | (240, 137) |
| CPU 仪表 | `meter-cpu` | (12, 40) | 90 × 90 | (102, 130) |
| 温度仪表 | `meter-temp` | (138, 40) | 90 × 90 | (228, 130) |
| **指标栏** | `md-container` | (260, 37) | 380 × 100 | (640, 137) |
| 内存 | `mem-container` | ~(272, 42) | ~356 × 30 | — |
| 磁盘 | `disk-container` | ~(272, 72) | ~356 × 30 | — |
| **磁盘条** | `hdd-container` | (0, 137) | 640 × 35 | (640, 172) |
| HDD1 | `hdd-0` | (0, 135) | 71 × 35 | (71, 170) |
| HDD2 | `hdd-1` | (71, 135) | 71 × 35 | (142, 170) |
| HDD3 | `hdd-2` | (142, 135) | 71 × 35 | (213, 170) |
| HDD4 | `hdd-3` | (213, 135) | 71 × 35 | (284, 170) |
| HDD5 | `hdd-4` | (284, 135) | 71 × 35 | (355, 170) |
| HDD6 | `hdd-5` | (355, 135) | 71 × 35 | (426, 170) |
| M.21 | `hdd-6` | (426, 135) | 71 × 35 | (497, 170) |
| M.22 | `hdd-7` | (497, 135) | 71 × 35 | (568, 170) |
| M.23 | `hdd-8` | (568, 135) | 71 × 35 | (639, 170) |

---

## 4. 字号规范

| 区域 | 字号 | 说明 |
|---|---|---|
| screen-frame 默认 | 12px | 全局继承 |
| status-bar 文字 | 12px | 与全局统一 |
| 仪表数值 | 12px，font-weight: 600 | 居中显示 |
| 进度条标签 | 12px | MEMORY / DISK |
| HDD 标签 | 12px | 居中显示 |

---

## 5. 仪表盘规格

### 5.1 圆弧仪表 (CPU / 温度)

| 属性 | 值 |
|---|---|
| 尺寸 | 90 × 90 px |
| 圆心 | (45, 45) |
| 半径 | 40 |
| 线宽 | 6 |
| 开口方向 | 向下 (rotate 135°) |
| 弧长 | 270° 圆弧 (缺口 90° 朝下) |
| 底色弧 | `--nas-muted`，stroke-dasharray: 188.50 251.33 |
| 数值弧 | `--nas-primary`(CPU) / `--nas-state-warning`(温度) |
| 指针 | 从圆心到 r=11 处，线宽 2 |
| 数值文字 | 12px，font-weight: 600，居中偏下 (top: 55%) |

### 5.2 进度条

| 属性 | 值 |
|---|---|
| 宽度 | 228px |
| 高度 | 18px |
| 底色 | `--nas-muted` |
| 填充色 | `--nas-primary` |
| 圆角 | 5px |

### 5.3 HDD 磁盘标签

| 属性 | 值 |
|---|---|
| 单个宽度 | 71px (640 ÷ 9) |
| 高度 | 35px |
| 背景 | `--nas-muted` |
| 圆角 | 3px |
| 状态圆点 | 14×14px，绝对定位右上角 |
| 正常 | `--nas-state-success` (#00FF00) |
| 警告 | `--nas-state-warning` (#FF8C00) |
| 离线 | `--nas-ink-3` (#666666) |

---

## 6. 对齐规则

- status-bar 内所有元素：`top: 50%; transform: translateY(-50%)`，垂直居中于 35px 高度
- 图标与文字：同基线对齐，图标 16×16 与 12px 文字中心线一致
- meter-panel 与 metrics-bar：top 均为 35px，与 status-bar 分隔线底部对齐
- disk-strip：bottom = 2px，距画布底边 2px
- disk-strip 内 9 个 HDD 卡：均分 640px 宽度，无间距，紧密排列

---

## 7. 页面交互

| 触发元素 | domId | 目标页面 |
|---|---|---|
| CPU 仪表 | `meter-cpu` | System Detail — CPU |
| 温度仪表 | `meter-temp` | System Detail — CPU |
| 内存条 | `mem-container` | System Detail — Memory |
| HDD1~9 | `hdd-0` ~ `hdd-8` | Disk Detail |

---

## 8. 元素关系

### 8.1 层级树

```
screen-frame (640×172)                     ← 根容器
├── status-bar        (0,  0,  640×35)     ← 第1层：顶部状态栏
│   ├── 标题 / 时间 / 上行 / 下行 / IP     ← 平级文字节点
│   ├── 蓝牙 / WiFi 图标                   ← 平级图标节点
│   └── 分隔线                              ← 视觉分隔
├── cpu-container       (0,  37, 240×100)    ← 第2层左：仪表盘
│   ├── meter-cpu      (12, 40, 90×90)     ← 圆弧仪表
│   └── meter-temp     (138,40, 90×90)     ← 圆弧仪表
├── md-container       (260,37, 380×100)    ← 第2层右：指标条
│   ├── mem-container                       ← 内存进度条
│   └── disk-container                      ← 磁盘进度条
└── hdd-container        (0,  137,640×35)     ← 第3层：底部磁盘条
    ├── hdd-0 ~ hdd-5  (71×35 × 6)         ← 机械硬盘标签
    └── hdd-6 ~ hdd-8  (71×35 × 3)         ← M.2 固态标签
```

### 8.2 垂直分区

```
y=0   ┌──────────────────────────────────┐
      │          status-bar (35px)        │  ← 信息展示区
y=35  ├──────────────────────────────────┤  ← 分隔线 2px
      │ cpu-container    │  md-container │  ← 仪表 + 指标区
      │   (240px)        │   (380px)     │     (100px)
y=137 ├──────────────────────────────────┤
      │          hdd-container (35px)     │  ← 磁盘列表区
y=172 └──────────────────────────────────┘
```

### 8.3 水平分区

cpu-container 与 md-container 之间存在 20px 间隙 (240 + 20 = 260)，保证视觉呼吸感。

### 8.4 交互链路

```
Overview 首页
├── 点击 meter-cpu ──────────→ System Detail — CPU
├── 点击 meter-temp ─────────→ System Detail — CPU
├── 点击 mem-container ──────→ System Detail — Memory
└── 点击 hdd-0~hdd-8 ────────→ Disk Detail
```

---

## 9. 字体

| Token | 栈 |
|---|---|
| `--nas-font-sans` | "Inter", "Noto Sans SC", "PingFang SC", "Microsoft YaHei", system-ui, sans-serif |
| `--nas-font-mono` | "JetBrains Mono", "SF Mono", "Fira Code", ui-monospace, monospace |