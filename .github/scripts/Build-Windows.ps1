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

        if ( $DebugPreference -eq 'Continue' ) {
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
        if (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") {
            $vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                # 设置Visual Studio路径环境变量，帮助CMake找到Visual Studio
                $env:VSINSTALLDIR = $vsPath
                $env:VCTargetsPath = Join-Path $vsPath "MSBuild\Microsoft\VC\v170"
                Write-Host "Visual Studio found at: $vsPath" -ForegroundColor Green
                
                # 设置CMAKE_GENERATOR_INSTANCE环境变量
                $env:CMAKE_GENERATOR_INSTANCE = $vsPath
                
                # 将Visual Studio工具添加到PATH
                $vcToolsPath = Join-Path $vsPath "VC\Tools\MSVC"
                if (Test-Path $vcToolsPath) {
                    $vcVersionDirs = Get-ChildItem -Path $vcToolsPath -Directory | Sort-Object Name -Descending
                    if ($vcVersionDirs.Count -gt 0) {
                        $latestVcPath = $vcVersionDirs[0].FullName
                        $env:PATH = "$latestVcPath\bin\Hostx64\x64;$env:PATH"
                    }
                }
            }
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
