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

        # GitHub Actions的windows-2022 runner已经预装了Visual Studio 2022并正确配置了环境
        # 我们不需要手动设置环境变量，CMake应该能够自动检测到Visual Studio
        Write-Host "=== Visual Studio环境检测 ===" -ForegroundColor Cyan
        Write-Host "GitHub Actions windows-2022 runner已预装Visual Studio 2022" -ForegroundColor Green
        
        # 简单验证CMake是否能找到Visual Studio生成器
        Write-Host "`n=== 验证CMake生成器 ===" -ForegroundColor Cyan
        try {
            $genInfo = cmake --help | Select-String "Visual Studio 17 2022"
            if ($genInfo) {
                Write-Host "✓ CMake支持Visual Studio 17 2022生成器" -ForegroundColor Green
            } else {
                Write-Warning "CMake未找到Visual Studio 17 2022生成器"
                # 列出所有可用的生成器用于调试
                Write-Host "可用的CMake生成器:" -ForegroundColor Yellow
                cmake --help | Select-String "^\s*\*" | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
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
