# ============================================================
# idf.ps1 - ESP-IDF 编译 / 烧录 / 监控 一键脚本
#
# 用法（在项目根目录或任意位置执行）：
#   .\idf.ps1                # 默认：编译 + 烧录 + 监控 全流程
#   .\idf.ps1 build          # 仅编译
#   .\idf.ps1 flash          # 仅烧录（自动先编译）
#   .\idf.ps1 monitor        # 仅串口监控
#   .\idf.ps1 clean          # 全量清理（idf.py fullclean）
#   .\idf.ps1 menuconfig     # 打开 Kconfig 菜单
#   .\idf.ps1 size           # 查看固件体积占用
#   .\idf.ps1 build -Port COM5   # 指定串口
#
# 说明：
#   - 自动激活 ESP-IDF 环境（C:\esp\v5.5.4\esp-idf）
#   - 已在 IDF 终端中时可用 -SkipExport 跳过重复激活
#   - 不指定 -Port 时让 idf.py 自动检测串口（ESP32-S3 走 USB-Serial-JTAG）
# ============================================================

param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'flash', 'monitor', 'buildflash', 'clean', 'menuconfig', 'size', 'all')]
    [string]$Action = 'all',

    [string]$Port = '',          # 串口名，例如 COM5；留空则自动检测
    [switch]$SkipExport          # 已在 IDF 终端中时跳过环境激活
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot

# ---- 1. 确定 IDF 路径并激活环境 -----------------------------------
$IdfPath = $env:IDF_PATH
if (-not $IdfPath) { $IdfPath = 'C:\esp\v5.5.4\esp-idf' }

if (-not $SkipExport) {
    if (-not $env:IDF_PATH) {
        Write-Host "==> 激活 ESP-IDF 环境: $IdfPath" -ForegroundColor Cyan
        & "$IdfPath\export.ps1" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'export.ps1 激活失败，请检查 IDF_PATH' }
    } elseif ($env:IDF_PATH -ne $IdfPath) {
        Write-Host "==> 当前环境 IDF_PATH=$env:IDF_PATH，切换至 $IdfPath" -ForegroundColor Yellow
        & "$IdfPath\export.ps1" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'export.ps1 激活失败，请检查 IDF_PATH' }
    }
}

Set-Location $ProjectDir

# ---- 2. 组装参数并执行 -------------------------------------------
$commonArgs = @()
if ($Port) { $commonArgs += @('-p', $Port) }

switch ($Action) {
    'all' {
        Write-Host "==> 全流程：编译 + 烧录 + 监控" -ForegroundColor Cyan
        & idf.py @commonArgs flash monitor
        break
    }
    'build'      { & idf.py build; break }
    'flash'      { & idf.py @commonArgs flash; break }
    'monitor'    { & idf.py @commonArgs monitor; break }
    'buildflash' { & idf.py @commonArgs flash; break }
    'clean'      { & idf.py fullclean; break }
    'menuconfig' { & idf.py menuconfig; break }
    'size'       { & idf.py size; break }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "==> 命令失败，退出码 $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "==> 完成" -ForegroundColor Green
