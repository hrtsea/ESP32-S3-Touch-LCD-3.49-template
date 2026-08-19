# ============================================================
# sim.ps1 - LVGL PC 模拟器 编译 / 运行 一键脚本
#
# 用法（在项目根目录执行）：
#   .\sim.ps1              # 默认：增量编译
#   .\sim.ps1 build        # 增量编译
#   .\sim.ps1 run          # 编译后启动模拟器
#   .\sim.ps1 configure    # 首次配置 / 重新生成 CMake 构建
#   .\sim.ps1 clean        # 清理构建目录
#   .\sim.ps1 rebuild      # 清理 + 重新配置 + 编译
#
# 说明：
#   - 自动将 C:\msys64\mingw64\bin 与 ucrt64\bin 加入 PATH，
#     否则 gcc 的 cc1 内部工具加载 DLL 失败会静默编译失败
#   - 输出 exe: sim/build/nas_monitor_sim.exe
# ============================================================

param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'run', 'configure', 'clean', 'rebuild')]
    [string]$Action = 'build'
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot
$SimDir = Join-Path $ProjectDir 'sim'
$BuildDir = Join-Path $SimDir 'build'
$Exe = Join-Path $BuildDir 'nas_monitor_sim.exe'

# ---- 工具链 PATH（关键：cc1 的 DLL 依赖 mingw64/bin）---------
$env:PATH = 'C:\msys64\mingw64\bin;C:\msys64\ucrt64\bin;' + $env:PATH
$Cmake = 'C:\msys64\ucrt64\bin\cmake.exe'

function Invoke-SimBuild {
    & $Cmake --build $BuildDir -- -j8
    if ($LASTEXITCODE -ne 0) { throw 'sim 编译失败' }
}

function Invoke-SimConfigure {
    if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }
    Push-Location $BuildDir
    try {
        & $Cmake -G 'MinGW Makefiles' $SimDir
        if ($LASTEXITCODE -ne 0) { throw 'cmake 配置失败' }
    } finally {
        Pop-Location
    }
}

switch ($Action) {
    'build' {
        Write-Host '==> 增量编译 sim' -ForegroundColor Cyan
        Invoke-SimBuild
        break
    }
    'run' {
        Write-Host '==> 编译并运行 sim' -ForegroundColor Cyan
        Invoke-SimBuild
        Start-Process -FilePath $Exe -WorkingDirectory $BuildDir
        break
    }
    'configure' {
        Write-Host '==> 生成 CMake 构建' -ForegroundColor Cyan
        Invoke-SimConfigure
        break
    }
    'clean' {
        Write-Host '==> 清理 sim 构建目录' -ForegroundColor Cyan
        if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
        break
    }
    'rebuild' {
        Write-Host '==> 清理并重新构建 sim' -ForegroundColor Cyan
        if (Test-Path $BuildDir) { Remove-Item $BuildDir -Recurse -Force }
        Invoke-SimConfigure
        Invoke-SimBuild
        break
    }
}

Write-Host '==> 完成' -ForegroundColor Green
