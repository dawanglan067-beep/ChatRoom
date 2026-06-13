param(
    [string]$SourceDir = "C:/Qt/ChatRoom",
    [string]$BuildDir = "C:/Qt/ChatRoom/build/diag-min",
    [string]$QtPrefix = "C:/Qt/6.8.3/mingw_64",
    [string]$CMakePath = "C:/Qt/Tools/CMake_64/bin/cmake.exe",
    [string]$NinjaPath = "C:/Qt/Tools/Ninja/ninja.exe",
    [string]$MingwMakePath = "C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe",
    [string]$GccPath = "C:/Qt/Tools/mingw1310_64/bin/g++.exe",
    [string]$MingwBinPath = "",
    [int]$TimeoutSec = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$cmdExe = "C:/Windows/System32/cmd.exe"
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$reportDir = Join-Path $SourceDir ("build/diag-report_" + $timestamp)
$logDir = Join-Path $reportDir "logs"

New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

function Test-RequiredFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required file not found: $Path"
    }
}

function Invoke-Step {
    param(
        [string]$Name,
        [string]$Command,
        [string]$WorkingDir,
        [int]$TimeoutSeconds
    )

    $safeName = ($Name -replace "[^A-Za-z0-9_.-]", "_")
    $logFile = Join-Path $logDir ($safeName + ".log")
    $wrapped = "cd /d `"$WorkingDir`" && $Command > `"$logFile`" 2>&1"

    $start = Get-Date
    $proc = Start-Process -FilePath $cmdExe -ArgumentList "/c", $wrapped -PassThru -WindowStyle Hidden
    $finished = $proc.WaitForExit($TimeoutSeconds * 1000)

    $timedOut = $false
    $exitCode = -1
    if ($finished) {
        $exitCode = $proc.ExitCode
    } else {
        $timedOut = $true
        try {
            Stop-Process -Id $proc.Id -Force
        } catch {
        }
        $exitCode = 124
    }

    $durationMs = [int]((Get-Date) - $start).TotalMilliseconds

    [PSCustomObject]@{
        Name      = $Name
        ExitCode  = $exitCode
        TimedOut  = $timedOut
        DurationMs = $durationMs
        LogFile   = $logFile
        Command   = $Command
    }
}

function Add-ReportLine {
    param([string]$Text)
    Add-Content -Path (Join-Path $reportDir "summary.txt") -Value $Text
}

Test-RequiredFile $cmdExe
Test-RequiredFile $CMakePath
Test-RequiredFile $NinjaPath
Test-RequiredFile $MingwMakePath
Test-RequiredFile $GccPath
Test-RequiredFile (Join-Path $SourceDir "CMakeLists.txt")

$gccDir = Split-Path -Path $GccPath -Parent
$resolvedMingwBin = if ($MingwBinPath.Trim().Length -gt 0) { $MingwBinPath } else { $gccDir }
Test-RequiredFile $resolvedMingwBin
$gccCPath = Join-Path $gccDir "gcc.exe"
Test-RequiredFile $gccCPath

if (-not ($env:PATH -split ";" | Where-Object { $_.Trim().ToLower() -eq $resolvedMingwBin.Trim().ToLower() })) {
    $env:PATH = $resolvedMingwBin + ";" + $env:PATH
}

$steps = New-Object System.Collections.Generic.List[object]

Add-ReportLine "Build diagnostics report"
Add-ReportLine "Generated at: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Add-ReportLine "SourceDir: $SourceDir"
Add-ReportLine "BuildDir: $BuildDir"
Add-ReportLine "QtPrefix: $QtPrefix"
Add-ReportLine "MingwBinPath: $resolvedMingwBin"
Add-ReportLine "MingwMakePath: $MingwMakePath"
Add-ReportLine ""

$probeFile = Join-Path $reportDir "write_probe.tmp"
Set-Content -Path $probeFile -Value "ok" -Encoding ASCII
Add-ReportLine "Temp write probe: PASS ($probeFile)"

$smokeCpp = Join-Path $reportDir "smoke.cpp"
$smokeObj = Join-Path $reportDir "smoke.obj"
Set-Content -Path $smokeCpp -Encoding ASCII -Value @'
#include <iostream>
int main() {
    std::cout << "ok";
    return 0;
}
'@

$qtSmokeCpp = Join-Path $reportDir "qt_smoke.cpp"
$qtSmokeObj = Join-Path $reportDir "qt_smoke.obj"
Set-Content -Path $qtSmokeCpp -Encoding ASCII -Value @'
#include <QString>
int main() {
    QString s("ok");
    return s.size() == 2 ? 0 : 1;
}
'@

$stepDefs = @(
    @{
        Name = "cmake_version"
        Command = "`"$CMakePath`" --version"
        WorkingDir = $SourceDir
        Timeout = 30
    },
    @{
        Name = "ninja_version"
        Command = "`"$NinjaPath`" --version"
        WorkingDir = $SourceDir
        Timeout = 30
    },
    @{
        Name = "mingw_make_version"
        Command = "`"$MingwMakePath`" --version"
        WorkingDir = $SourceDir
        Timeout = 30
    },
    @{
        Name = "gpp_version"
        Command = "`"$GccPath`" --version"
        WorkingDir = $SourceDir
        Timeout = 30
    },
    @{
        Name = "gpp_smoke_compile"
        Command = "`"$GccPath`" -std=c++17 -c `"$smokeCpp`" -o `"$smokeObj`""
        WorkingDir = $SourceDir
        Timeout = 60
    },
    @{
        Name = "gpp_qt_smoke_compile"
        Command = "`"$GccPath`" -std=c++17 -DUNICODE -D_UNICODE -isystem `"$QtPrefix/include/QtCore`" -isystem `"$QtPrefix/include`" -isystem `"$QtPrefix/mkspecs/win32-g++`" -c `"$qtSmokeCpp`" -o `"$qtSmokeObj`""
        WorkingDir = $SourceDir
        Timeout = 90
    },
    @{
        Name = "cmake_configure"
        Command = "`"$CMakePath`" -S `"$SourceDir`" -B `"$BuildDir`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$NinjaPath`" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=`"$QtPrefix`" -DCMAKE_C_COMPILER=`"$gccCPath`" -DCMAKE_CXX_COMPILER=`"$GccPath`""
        WorkingDir = $SourceDir
        Timeout = $TimeoutSec
    },
    @{
        Name = "cmake_build_chatroom_tests"
        Command = "`"$CMakePath`" --build `"$BuildDir`" --target ChatRoomUnitTests -- -j1 -v"
        WorkingDir = $SourceDir
        Timeout = $TimeoutSec
    },
    @{
        Name = "cmake_configure_mingw_make"
        Command = "`"$CMakePath`" -S `"$SourceDir`" -B `"$BuildDir-mingwmake`" -G `"MinGW Makefiles`" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=`"$QtPrefix`" -DCMAKE_C_COMPILER=`"$gccCPath`" -DCMAKE_CXX_COMPILER=`"$GccPath`""
        WorkingDir = $SourceDir
        Timeout = $TimeoutSec
    }
)

foreach ($def in $stepDefs) {
    $result = Invoke-Step -Name $def.Name -Command $def.Command -WorkingDir $def.WorkingDir -TimeoutSeconds $def.Timeout
    $steps.Add($result)
}

Add-ReportLine ""
Add-ReportLine "Step results:"
foreach ($s in $steps) {
    Add-ReportLine ("- {0}: exit={1}, timeout={2}, durationMs={3}, log={4}" -f $s.Name, $s.ExitCode, $s.TimedOut, $s.DurationMs, $s.LogFile)
}

$ninjaConfigure = $steps | Where-Object { $_.Name -eq "cmake_configure" } | Select-Object -First 1
$mingwConfigure = $steps | Where-Object { $_.Name -eq "cmake_configure_mingw_make" } | Select-Object -First 1

if ($null -ne $ninjaConfigure -and $null -ne $mingwConfigure) {
    Add-ReportLine ""
    Add-ReportLine "Recommendation:"
    if ($ninjaConfigure.ExitCode -ne 0 -and $mingwConfigure.ExitCode -eq 0) {
        Add-ReportLine "- Ninja generator failed while MinGW Makefiles configured successfully."
        Add-ReportLine "- Prefer generator: MinGW Makefiles on this machine."
    } elseif ($ninjaConfigure.ExitCode -eq 0 -and $mingwConfigure.ExitCode -ne 0) {
        Add-ReportLine "- Ninja generator configured successfully; MinGW Makefiles failed."
        Add-ReportLine "- Prefer generator: Ninja on this machine."
    } elseif ($ninjaConfigure.ExitCode -eq 0 -and $mingwConfigure.ExitCode -eq 0) {
        Add-ReportLine "- Both generators configured successfully."
    } else {
        Add-ReportLine "- Both generators failed; inspect logs for toolchain/runtime issues."
    }
}

$failed = $steps | Where-Object { $_.ExitCode -ne 0 }
if ($failed.Count -gt 0) {
    Add-ReportLine ""
    Add-ReportLine "Failure snapshots (last 40 lines each):"
    foreach ($f in $failed) {
        Add-ReportLine ("### {0}" -f $f.Name)
        if (Test-Path -LiteralPath $f.LogFile) {
            $tail = Get-Content -Path $f.LogFile -Tail 40
            foreach ($line in $tail) {
                Add-ReportLine $line
            }
        } else {
            Add-ReportLine "(log file missing)"
        }
        Add-ReportLine ""
    }
}

$summaryPath = Join-Path $reportDir "summary.txt"
Write-Output ("REPORT_DIR=" + $reportDir)
Write-Output ("SUMMARY=" + $summaryPath)
Write-Output "RESULTS:"
$steps | Select-Object Name, ExitCode, TimedOut, DurationMs, LogFile | Format-Table -AutoSize

if ($failed.Count -gt 0) {
    exit 1
}

exit 0
