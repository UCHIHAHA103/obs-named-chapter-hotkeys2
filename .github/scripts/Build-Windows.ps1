[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $SkipAll,
    [switch] $SkipBuild,
    [switch] $SkipDeps
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.0.0' ) {
    Write-Warning 'The obs-deps PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    # 检测并确保MSBuild可用
    $msbuildPath = Get-Command "msbuild.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
    if (-not $msbuildPath) {
        # 尝试使用vswhere查找Visual Studio 2022
        $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswherePath) {
            $vsPath = & $vswherePath -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
                if (Test-Path $msbuildPath) {
                    $env:PATH = "$(Split-Path $msbuildPath);$env:PATH"
                    Write-Host "MSBuild found at: $msbuildPath" -ForegroundColor Green
                }
            }
        }
    }
    
    if (-not $msbuildPath) {
        Write-Warning "MSBuild not found. CMake may fail to find Visual Studio."
    } else {
        Write-Host "MSBuild is available at: $msbuildPath" -ForegroundColor Green
    }

    if ( ! $SkipDeps ) {
        Install-BuildDependencies -WingetFile "${ScriptHome}/.Wingetfile"
    }

    Push-Location -Stack BuildTemp
    if ( ! ( ( $SkipAll ) -or ( $SkipBuild ) ) ) {
        Ensure-Location $ProjectRoot

        $CmakeArgs = @()
        $CmakeBuildArgs = @()
        $CmakeInstallArgs = @()

        if ( $VerbosePreference -eq 'Continue' ) {
            $CmakeBuildArgs += ('--verbose')
            $CmakeInstallArgs += ('--verbose')
        }

        # 在CI环境中总是启用调试输出，以便诊断问题
        if ( $Env:CI -ne $null ) {
            $CmakeArgs += ('--debug-output')
            $CmakeArgs += ('--trace-expand')
            Write-Host "CI环境检测到，启用CMake调试输出" -ForegroundColor Yellow
        } elseif ( $DebugPreference -eq 'Continue' ) {
            $CmakeArgs += ('--debug-output')
        }

        $Preset = "windows-$(if ( $Env:CI -ne $null ) { 'ci-' })${Target}"

        $CmakeArgs += @(
            '--preset', $Preset
        )

        $CmakeBuildArgs += @(
            '--build'
            '--preset', $Preset
            '--config', $Configuration
            '--parallel'
            '--', '/consoleLoggerParameters:Summary', '/noLogo'
        )

        $CmakeInstallArgs += @(
            '--install', "build_${Target}"
            '--prefix', "${ProjectRoot}/release/${Configuration}"
            '--config', $Configuration
        )

        # 确保Visual Studio环境已设置，以便CMake能找到生成器
        Write-Host "=== Visual Studio环境检测 ===" -ForegroundColor Cyan
        
        # 方法1：尝试使用VsDevCmd.bat设置环境
        $vsDevCmdPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
        if (!(Test-Path $vsDevCmdPath)) {
            $vsDevCmdPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
        }
        if (!(Test-Path $vsDevCmdPath)) {
            $vsDevCmdPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
        }
        
        if (Test-Path $vsDevCmdPath) {
            Write-Host "找到VsDevCmd.bat: $vsDevCmdPath" -ForegroundColor Green
            # 使用VsDevCmd.bat设置环境变量
            cmd.exe /c "`"$vsDevCmdPath`" -arch=x64 -host_arch=x64 && set" | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") {
                    $name = $matches[1]
                    $value = $matches[2]
                    [Environment]::SetEnvironmentVariable($name, $value)
                }
            }
            Write-Host "已使用VsDevCmd.bat设置Visual Studio环境" -ForegroundColor Green
        } else {
            Write-Host "未找到VsDevCmd.bat，使用vswhere检测" -ForegroundColor Yellow
        }
        
        # 方法2：使用vswhere检测Visual Studio（备用方法）
        if (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") {
            $vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                Write-Host "Visual Studio found at: $vsPath" -ForegroundColor Green
                
                # 设置Visual Studio路径环境变量，帮助CMake找到Visual Studio
                $env:VSINSTALLDIR = $vsPath
                $env:VCTargetsPath = Join-Path $vsPath "MSBuild\Microsoft\VC\v170"
                $env:CMAKE_GENERATOR_INSTANCE = $vsPath
                
                # 将Visual Studio工具添加到PATH
                $vcToolsPath = Join-Path $vsPath "VC\Tools\MSVC"
                if (Test-Path $vcToolsPath) {
                    $vcVersionDirs = Get-ChildItem -Path $vcToolsPath -Directory | Sort-Object Name -Descending
                    if ($vcVersionDirs.Count -gt 0) {
                        $latestVcPath = $vcVersionDirs[0].FullName
                        $env:PATH = "$latestVcPath\bin\Hostx64\x64;$env:PATH"
                        Write-Host "已添加VC工具到PATH: $latestVcPath\bin\Hostx64\x64" -ForegroundColor Green
                    }
                }
            } else {
                Write-Warning "vswhere未找到Visual Studio安装"
            }
        } else {
            Write-Warning "未找到vswhere.exe"
        }
        
        # 打印关键环境变量用于调试
        Write-Host "`n=== 关键环境变量 ===" -ForegroundColor Cyan
        $envVars = @("VSINSTALLDIR", "VCTargetsPath", "CMAKE_GENERATOR_INSTANCE", "PATH")
        foreach ($var in $envVars) {
            $value = [Environment]::GetEnvironmentVariable($var)
            if ($value) {
                Write-Host "$var = $value" -ForegroundColor Gray
            } else {
                Write-Host "$var = (未设置)" -ForegroundColor Yellow
            }
        }
        
        # 检查CMake是否能找到Visual Studio生成器
        Write-Host "`n=== 测试CMake生成器 ===" -ForegroundColor Cyan
        try {
            $genInfo = cmake --help | Select-String "Visual Studio"
            if ($genInfo) {
                Write-Host "CMake支持以下Visual Studio生成器:" -ForegroundColor Green
                $genInfo | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
            } else {
                Write-Warning "CMake未找到Visual Studio生成器"
            }
        } catch {
            Write-Warning "无法检查CMake生成器: $_"
        }

        Log-Group "Configuring ${ProductName}..."
        Invoke-External cmake @CmakeArgs

        Log-Group "Building ${ProductName}..."
        Invoke-External cmake @CmakeBuildArgs
    }
    Log-Group "Install ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
