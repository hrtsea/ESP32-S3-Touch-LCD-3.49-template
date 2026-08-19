# ESP32-S3 一键构建/烧录/监控脚本
# 用法:
#   .\scripts\flash.ps1 build          编译
#   .\scripts\flash.ps1 flash          烧录(自动探测端口)
#   .\scripts\flash.ps1 monitor        串口监控
#   .\scripts\flash.ps1 build-flash    编译 + 烧录
#   .\scripts\flash.ps1 all            编译 + 烧录 + 监控
#   .\scripts\flash.ps1 flash -Port COM5   指定端口烧录
#   .\scripts\flash.ps1 flash -Erase   烧录前先擦除
#
# 依赖: ESP-IDF v5.5.4 (IDF_PATH 指向 C:\esp\v5.5.4\esp-idf)

param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'flash', 'monitor', 'build-flash', 'all')]
    [string]$Action = 'all',

    [string]$Port = '',
    [switch]$Erase
)

$ErrorActionPreference = 'Stop'

# ---------- 配置 ----------
$IDF_ROOT   = 'C:\esp\v5.5.4\esp-idf'
$EXPORT_PS1 = Join-Path $IDF_ROOT 'export.ps1'
$PROJECT    = $PSScriptRoot | Split-Path -Parent
$BUILD_DIR  = Join-Path $PROJECT 'build'
$ESPTOOL    = Join-Path $IDF_ROOT 'components\esptool_py\esptool\esptool.py'

# ---------- 初始化 ESP-IDF 环境 ----------
if (-not (Test-Path $EXPORT_PS1)) {
    Write-Error "找不到 ESP-IDF 环境脚本: $EXPORT_PS1"
    exit 1
}
Write-Host "==> 初始化 ESP-IDF 环境 ($IDF_ROOT)" -ForegroundColor Cyan
# export.ps1 会设置 IDF_PATH / PATH 等, 通过点源方式在当前作用域生效
. $EXPORT_PS1 *>$null

Set-Location $PROJECT

# ---------- 端口探测 ----------
function Find-Port {
    $candidates = @()
    try {
        $candidates = (Get-WmiObject Win32_SerialPort -ErrorAction SilentlyContinue).DeviceID
    } catch { }
    if (-not $candidates) {
        $candidates = @('COM3', 'COM4', 'COM5', 'COM6', 'COM7', 'COM8')
    }
    foreach ($p in $candidates) {
        try {
            $out = & python $ESPTOOL --port $p --no-stamp chip-id 2>&1
            if ($out -match 'MAC' -or $out -match 'chip') {
                Write-Host "==> 检测到 ESP32 设备: $p" -ForegroundColor Green
                return $p
            }
        } catch { }
    }
    return ''
}

function Resolve-Port {
    if ($Port) { return $Port }
    $p = Find-Port
    if (-not $p) {
        Write-Error "未能自动探测到 ESP32 端口, 请用 -Port COMx 指定"
        exit 1
    }
    return $p
}

# ---------- 动作 ----------
function Do-Build {
    Write-Host '==> 编译中...' -ForegroundColor Cyan
    & idf.py build
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }
}

function Do-Flash([string]$p) {
    $flashArgs = Join-Path $BUILD_DIR 'flash_args'
    if (-not (Test-Path $flashArgs)) {
        Write-Error "找不到 $flashArgs, 请先 build"
        exit 1
    }
    if ($Erase) {
        Write-Host "==> 擦除 flash ($p)" -ForegroundColor Cyan
        & python $ESPTOOL --port $p --chip esp32s3 erase-flash
    }
    Write-Host "==> 烧录到 $p ..." -ForegroundColor Cyan
    # 从 flash_args 读取地址/文件名(跳过首行选项), 转为 build 目录下的绝对路径
    $lines = Get-Content $flashArgs | Where-Object { $_ -match '^0x' }
    $argsList = @('--port', $p, '--chip', 'esp32s3', 'write-flash',
                  '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '16MB')
    foreach ($line in $lines) {
        $addr, $rel = $line.Trim() -split '\s+'
        $abs = Join-Path $BUILD_DIR $rel
        if (-not (Test-Path $abs)) { Write-Error "缺少镜像文件: $abs"; exit 1 }
        $argsList += @($addr, $abs)
    }
    & python $ESPTOOL @argsList
    if ($LASTEXITCODE -ne 0) { throw "烧录失败" }
    Write-Host '==> 烧录完成' -ForegroundColor Green
}

function Do-Monitor([string]$p) {
    Write-Host "==> 打开串口监控 $p (Ctrl+] 退出)" -ForegroundColor Cyan
    & idf.py -p $p monitor
}

# ---------- 执行 ----------
switch ($Action) {
    'build'       { Do-Build }
    'flash'       { Do-Flash (Resolve-Port) }
    'monitor'     { Do-Monitor (Resolve-Port) }
    'build-flash' { Do-Build; Do-Flash (Resolve-Port) }
    'all'         {
        Do-Build
        $p = Resolve-Port
        Do-Flash $p
        Do-Monitor $p
    }
}
