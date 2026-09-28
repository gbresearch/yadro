<#
.SYNOPSIS
    Checks that every Yadro header compiles on its own, and that the library sources compile warning-free, with the
    flags a consumer such as the AI orchestrator uses.

.DESCRIPTION
    Compiles (syntax and semantic check only, /Zs) these translation units:
      - one per header under util\, container\, archive\ and async\, containing only #include <dir/header.h>. The
        test project includes everything through include\yadro.h, so a header that relies on another header having
        been included first is invisible there;
      - a GB_TEST with and without its optional policy argument, since the macro must expand to a valid call under
        both preprocessors;
      - each library .cpp under algorithm\, container\, simulator\ and util\.

    Flags: /std:c++latest /permissive- /W4 /WX /utf-8 /EHsc /Zc:__cplusplus /DUNICODE /D_UNICODE
    /D_WIN32_WINNT=0x0A00, in two modes: legacy-pp (the legacy preprocessor), and conforming-pp (/Zc:preprocessor,
    GBWINDOWS and GB_YADRO_ENABLE_AXE_JSON defined, AXE as an external include, as in the orchestrator). Each mode
    runs for each requested platform.

    Prints every failure with its first diagnostics and exits with 1 if any translation unit failed, 0 otherwise.

.PARAMETER Platforms
    Target platforms: x64, x86 or both (the default).

.PARAMETER AxeInclude
    AXE include directory, needed by the conforming-pp mode. Defaults to the sibling ..\axe\include.

.PARAMETER VcVarsAll
    Path to vcvarsall.bat. Defaults to the latest Visual Studio found by vswhere.

.PARAMETER Jobs
    Number of compilers run at once. Defaults to the number of logical processors.

.PARAMETER Filter
    Wildcard on the relative path of the header or source (for example 'util/*'), to check a subset.

.PARAMETER AllowWarnings
    Compile without /WX, so that only errors fail; the number of translation units with warnings is reported.
    Consumers that include Yadro through /external:I with /external:W0 do not see warnings from its headers.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_headers.ps1
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_headers.ps1 -Platforms x64 -Filter 'util/win_pipe.h'
#>
param(
    [ValidateSet('x64', 'x86')]
    [string[]]$Platforms = @('x64', 'x86'),
    [string]$AxeInclude,
    [string]$VcVarsAll,
    [int]$Jobs = [Environment]::ProcessorCount,
    [string]$Filter = '*',
    [switch]$AllowWarnings
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $AxeInclude) { $AxeInclude = Join-Path $root '..\axe\include' }
if (-not (Test-Path (Join-Path $AxeInclude 'axe.h'))) {
    throw "AXE include directory not found: $AxeInclude (pass -AxeInclude)"
}
$AxeInclude = (Resolve-Path $AxeInclude).Path

if (-not $VcVarsAll) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) { $VcVarsAll = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat' }
    }
}
if (-not $VcVarsAll -or -not (Test-Path $VcVarsAll)) { throw 'vcvarsall.bat not found (pass -VcVarsAll)' }

function Get-RelativeFiles([string[]]$dirs, [string]$pattern) {
    foreach ($dir in $dirs) {
        Get-ChildItem -Path (Join-Path $root $dir) -Filter $pattern -File | ForEach-Object { "$dir/$($_.Name)" }
    }
}

# name -> generated translation unit text, or $null for a library source compiled as it is
$units = [ordered]@{}
foreach ($header in Get-RelativeFiles 'util', 'container', 'archive', 'async' '*.h' | Sort-Object) {
    if ($header -like $Filter) { $units[$header] = "#include <$header>`r`n" }
}
if ($units.Contains('util/gbtest.h')) {
    $units['GB_TEST usage'] = "#include <util/gbtest.h>`r`nGB_TEST(header_check, without_policy) {}`r`n" +
                              "GB_TEST(header_check, with_policy, std::launch::async) {}`r`n"
}
foreach ($source in Get-RelativeFiles 'algorithm', 'container', 'simulator', 'util' '*.cpp' | Sort-Object) {
    if ($source -like $Filter) { $units[$source] = $null }
}
if ($units.Count -eq 0) { throw "nothing matches '$Filter'" }

$common = @('/nologo', '/Zs', '/std:c++latest', '/permissive-', '/W4', '/utf-8', '/EHsc', '/Zc:__cplusplus',
            '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', "/I`"$root`"")
if (-not $AllowWarnings) { $common += '/WX' }
$modes = [ordered]@{
    'legacy-pp'     = @()
    'conforming-pp' = @('/Zc:preprocessor', '/DGBWINDOWS', '/DGB_YADRO_ENABLE_AXE_JSON',
                        "/external:I`"$AxeInclude`"", '/external:W0')
}

$work = Join-Path $root 'obj\header_check'
$baseline = [Environment]::GetEnvironmentVariables()
$failures = New-Object System.Collections.Generic.List[object]
$warned = New-Object System.Collections.Generic.List[string]
$total = 0
$clock = [Diagnostics.Stopwatch]::StartNew()

foreach ($platform in $Platforms) {
    # take the compiler environment from vcvarsall for this platform, starting from the original environment;
    # the compilers started below inherit it
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        [Environment]::SetEnvironmentVariable($name, $baseline[$name])
    }
    $envLines = & cmd.exe /c "`"$VcVarsAll`" $platform >nul 2>nul && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvarsall $platform failed" }
    foreach ($line in $envLines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1)) }
    }
    $cl = (Get-Command cl.exe).Source

    $queue = New-Object System.Collections.Generic.Queue[object]
    foreach ($mode in $modes.Keys) {
        $dir = Join-Path $work "$platform\$mode"
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        foreach ($name in $units.Keys) {
            if ($null -eq $units[$name]) {
                $source = Join-Path $root $name
            } else {
                $source = Join-Path $dir (($name -replace '[/\\ ]', '__') + '.cpp')
                [IO.File]::WriteAllText($source, $units[$name])
            }
            $queue.Enqueue([pscustomobject]@{ Name = $name; Mode = $mode; Platform = $platform
                                              Args = (($common + $modes[$mode]) + "`"$source`"") -join ' ' })
        }
    }

    $running = New-Object System.Collections.Generic.List[object]
    while ($queue.Count -gt 0 -or $running.Count -gt 0) {
        while ($queue.Count -gt 0 -and $running.Count -lt $Jobs) {
            $item = $queue.Dequeue()
            $psi = New-Object Diagnostics.ProcessStartInfo $cl, $item.Args
            $psi.UseShellExecute = $false
            $psi.RedirectStandardOutput = $true
            $psi.RedirectStandardError = $true
            $psi.CreateNoWindow = $true
            $psi.WorkingDirectory = $dir
            $process = [Diagnostics.Process]::Start($psi)
            $item | Add-Member Process $process
            $item | Add-Member Out $process.StandardOutput.ReadToEndAsync()
            $item | Add-Member Err $process.StandardError.ReadToEndAsync()
            $running.Add($item)
        }
        $done = @($running | Where-Object { $_.Process.HasExited })
        if ($done.Count -eq 0) { Start-Sleep -Milliseconds 50; continue }
        foreach ($item in $done) {
            [void]$running.Remove($item)
            $item.Process.WaitForExit()
            $total++
            $diagnostics = @(($item.Out.Result + $item.Err.Result) -split "`r?`n" |
                Where-Object { $_ -match '(error|warning) [A-Z]+\d+' })
            if ($diagnostics -match 'warning [A-Z]+\d+') { $warned.Add("$($item.Platform) $($item.Mode) $($item.Name)") }
            if ($item.Process.ExitCode -ne 0) {
                $failures.Add([pscustomobject]@{ Name = $item.Name; Mode = $item.Mode; Platform = $item.Platform
                                                 Diagnostics = $diagnostics })
                Write-Host "FAIL  $($item.Platform) $($item.Mode) $($item.Name)" -ForegroundColor Red
            }
        }
    }
}

$elapsed = [math]::Round($clock.Elapsed.TotalSeconds)
$scope = "$($units.Count) units, $($Platforms -join ' and '), $($modes.Keys -join ' and '); ${elapsed}s"
if ($AllowWarnings -and $warned.Count -gt 0) {
    Write-Host "note: $($warned.Count) translation units compiled with warnings (run without -AllowWarnings to see them)"
}
if ($failures.Count -eq 0) {
    Write-Host "check_headers: all $total translation units compiled ($scope)" -ForegroundColor Green
    exit 0
}

Write-Host ''
foreach ($failure in $failures) {
    Write-Host "=== $($failure.Name)  [$($failure.Platform), $($failure.Mode)]" -ForegroundColor Red
    $failure.Diagnostics | Where-Object { -not $AllowWarnings -or $_ -match 'error [A-Z]+\d+' } |
        Select-Object -First 10 | ForEach-Object { Write-Host "    $_" }
}
Write-Host ''
$names = @($failures | ForEach-Object { $_.Name } | Sort-Object -Unique)
Write-Host "check_headers FAILED: $($failures.Count) of $total translation units ($($names -join ', ')); $scope" -ForegroundColor Red
exit 1
