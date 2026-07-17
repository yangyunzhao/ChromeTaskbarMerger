[CmdletBinding()]
param(
    [ValidateSet('Enabled', 'ReadyToLogout', 'AfterLogin', 'Cleanup')]
    [string]$Stage = 'Enabled'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))
$portableDirectory = Join-Path $repositoryRoot 'dist\ChromeTaskbarMerger'
$executablePath = [System.IO.Path]::GetFullPath(
    (Join-Path $portableDirectory 'ChromeTaskbarMerger.exe'))
$configurationPath = Join-Path $portableDirectory 'ChromeTaskbarMerger.ini'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValueName = 'ChromeTaskbarMerger'
$expectedCommand = '"' + $executablePath + '" --autostart'
$checks = [System.Collections.Generic.List[object]]::new()

function Add-Check {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Passed,
        [Parameter(Mandatory)][string]$Detail
    )

    $checks.Add([pscustomobject]@{
        Name   = $Name
        Passed = $Passed
        Detail = $Detail
    })
}

function Read-ConfigurationValue {
    param([Parameter(Mandatory)][string]$Name)

    if (-not (Test-Path -LiteralPath $configurationPath -PathType Leaf)) {
        return $null
    }
    $pattern = '^\s*' + [regex]::Escape($Name) + '\s*=\s*(.*?)\s*$'
    $matches = @(
        Get-Content -LiteralPath $configurationPath -Encoding utf8 |
            Where-Object { $_ -match $pattern } |
            ForEach-Object { $Matches[1] }
    )
    if ($matches.Count -ne 1) {
        return $null
    }
    return $matches[0]
}

$executableExists = Test-Path -LiteralPath $executablePath -PathType Leaf
Add-Check `
    -Name 'Portable executable exists' `
    -Passed $executableExists `
    -Detail $executablePath

$provider = Read-ConfigurationValue -Name 'tab_provider'
Add-Check `
    -Name 'Built-in provider selected' `
    -Passed ($provider -eq 'builtin') `
    -Detail ("tab_provider={0}" -f $provider)

$startWithWindows = Read-ConfigurationValue -Name 'start_with_windows'
$expectEnabled = $Stage -ne 'Cleanup'
Add-Check `
    -Name 'INI startup setting' `
    -Passed ($startWithWindows -eq $(if ($expectEnabled) { 'true' } else { 'false' })) `
    -Detail ("start_with_windows={0}; expected={1}" -f `
        $startWithWindows, $(if ($expectEnabled) { 'true' } else { 'false' }))

$registeredCommand = $null
try {
    $registeredCommand = Get-ItemPropertyValue `
        -LiteralPath $runKey `
        -Name $runValueName `
        -ErrorAction Stop
} catch [System.Management.Automation.ItemNotFoundException] {
    $registeredCommand = $null
} catch [System.Management.Automation.PSArgumentException] {
    $registeredCommand = $null
}

if ($expectEnabled) {
    Add-Check `
        -Name 'Run entry exists' `
        -Passed ($null -ne $registeredCommand) `
        -Detail $(if ($null -eq $registeredCommand) { 'missing' } else { 'present' })
    Add-Check `
        -Name 'Run command is exact' `
        -Passed ($registeredCommand -ceq $expectedCommand) `
        -Detail ("expected={0}; actual={1}" -f $expectedCommand, $registeredCommand)
} else {
    Add-Check `
        -Name 'Run entry removed' `
        -Passed ($null -eq $registeredCommand) `
        -Detail $(if ($null -eq $registeredCommand) { 'missing' } else { $registeredCommand })
}

$processQuerySucceeded = $true
$processes = @()
try {
    $processes = @(
        Get-CimInstance Win32_Process `
            -Filter "Name='ChromeTaskbarMerger.exe'" `
            -ErrorAction Stop
    )
} catch {
    $processQuerySucceeded = $false
}
Add-Check `
    -Name 'Process query succeeded' `
    -Passed $processQuerySucceeded `
    -Detail ("process_count={0}" -f $processes.Count)

if ($Stage -eq 'ReadyToLogout') {
    Add-Check `
        -Name 'Manager exited before logout' `
        -Passed ($processes.Count -eq 0) `
        -Detail ("process_count={0}; expected=0" -f $processes.Count)
}

if ($Stage -eq 'AfterLogin') {
    Add-Check `
        -Name 'Exactly one manager process' `
        -Passed ($processes.Count -eq 1) `
        -Detail ("process_count={0}; expected=1" -f $processes.Count)

    $matchingPath = @(
        $processes | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_.ExecutablePath) -and
            [string]::Equals(
                [System.IO.Path]::GetFullPath($_.ExecutablePath),
                $executablePath,
                [System.StringComparison]::OrdinalIgnoreCase)
        }
    )
    Add-Check `
        -Name 'Login process uses portable EXE' `
        -Passed ($matchingPath.Count -eq 1) `
        -Detail ("matching_processes={0}; expected_path={1}" -f `
            $matchingPath.Count, $executablePath)

    $autostartProcesses = @(
        $processes | Where-Object {
            $_.CommandLine -match '(?i)(^|\s)--autostart(\s|$)'
        }
    )
    Add-Check `
        -Name 'Login process has --autostart' `
        -Passed ($autostartProcesses.Count -eq 1) `
        -Detail ("matching_processes={0}" -f $autostartProcesses.Count)
}

Write-Host ''
Write-Host ("ChromeTaskbarMerger login-startup verification: {0}" -f $Stage)
Write-Host ("Repository: {0}" -f $repositoryRoot)
Write-Host ''
foreach ($check in $checks) {
    $label = if ($check.Passed) { 'PASS' } else { 'FAIL' }
    $color = if ($check.Passed) { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1}" -f $label, $check.Name) -ForegroundColor $color
    Write-Host ("       {0}" -f $check.Detail)
}

$failed = @($checks | Where-Object { -not $_.Passed })
Write-Host ''
if ($failed.Count -eq 0) {
    Write-Host 'RESULT: PASS' -ForegroundColor Green
    exit 0
}

Write-Host ("RESULT: FAIL ({0} check(s))" -f $failed.Count) `
    -ForegroundColor Red
exit 1
