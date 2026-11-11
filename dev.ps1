<#
.SYNOPSIS
    Build helper for Tactus on Windows.

.EXAMPLE
    .\dev.ps1 bootstrap   # one-time: fetch JUCE into third_party\
    .\dev.ps1 build       # configure (if needed) and build everything
    .\dev.ps1 test        # build, then run the test suite
    .\dev.ps1 run         # build, then start the application
    .\dev.ps1 clean       # delete the build folder

    Add -Config Debug for a debug build (default: Release).
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('bootstrap', 'build', 'test', 'run', 'clean')]
    [string]$Command = 'build',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root "build\$Config"
$juceTag = '9.0.5'

function Import-MsvcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio (or Build Tools) with the C++ workload is required.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio installation with the C++ toolset was found.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw "Could not load the MSVC environment from $vcvars" }
}

function Invoke-Checked {
    # Native tools write notices to stderr (e.g. CMake's "GLOB mismatch"). With 'Stop', Windows
    # PowerShell would treat such a line as a fatal error; only the exit code decides here.
    $ErrorActionPreference = 'Continue'
    & $args[0] @($args | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

function Invoke-Bootstrap {
    $juce = Join-Path $root 'third_party\JUCE'
    if (Test-Path (Join-Path $juce 'CMakeLists.txt')) { Write-Host "JUCE already present in $juce"; return }
    Invoke-Checked git clone --depth 1 --branch $juceTag https://github.com/juce-framework/JUCE.git $juce
}

function Invoke-Build {
    Import-MsvcEnvironment
    if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
        Invoke-Checked cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
    }
    Invoke-Checked cmake --build $buildDir
}

switch ($Command) {
    'bootstrap' { Invoke-Bootstrap }
    'build'     { Invoke-Build }
    'test'      { Invoke-Build; Invoke-Checked (Join-Path $buildDir 'tests\tactus_tests.exe') }
    'run'       { Invoke-Build; Start-Process (Join-Path $buildDir "app\Tactus_artefacts\$Config\Tactus.exe") }
    'clean'     { if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir } }
}
