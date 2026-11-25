# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
#
# The parent's hard line for calling a workstation window "quiet":
#
#     load 1m < 15% of the core count   (28 cores => < 4.2 busy cores)
#     for 60 continuous seconds, with no reading above it
#
# Anything else is contended -- "a bit better than a moment ago" is not quiet. This gate samples
# busy cores -- the sum of every process's CPU seconds, differenced over a fixed interval -- because
# Windows has no /proc/loadavg and a run-queue length is not what the rule is about: the rule is
# about how much of the machine someone else is using.
#
# Usage:  pwsh -NoProfile -File load-gate.ps1 [-Seconds 60] [-Step 2] [-ThresholdPercent 15]

param(
    [int]$Seconds = 60,
    [int]$Step = 2,
    [double]$ThresholdPercent = 15
)

$cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$threshold = [math]::Round($cores * $ThresholdPercent / 100.0, 2)

function Sample-BusyCores([int]$seconds) {
    $before = @{}
    Get-Process | ForEach-Object { if ($null -ne $_.CPU) { $before[$_.Id] = $_.CPU } }
    Start-Sleep -Seconds $seconds
    $total = 0.0
    Get-Process | ForEach-Object {
        if ($null -ne $_.CPU -and $before.ContainsKey($_.Id)) { $total += ($_.CPU - $before[$_.Id]) }
    }
    return [math]::Round($total / $seconds, 2)
}

$samples = @()
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    $samples += Sample-BusyCores -seconds $Step
}

$max = ($samples | Measure-Object -Maximum).Maximum
$avg = [math]::Round((($samples | Measure-Object -Average).Average), 2)
$over = @($samples | Where-Object { $_ -ge $threshold }).Count
$verdict = if ($over -eq 0) { 'QUIET' } else { 'CONTENDED' }

Write-Output ("[load] host={0} cores={1} window={2}s step={3}s samples={4}" -f $env:COMPUTERNAME, $cores, $Seconds, $Step, $samples.Count)
Write-Output ("[load] busy_cores avg={0} max={1} samples=[{2}]" -f $avg, $max, ($samples -join ', '))
Write-Output ("[load] quiet_line={0} busy_cores (<{1}%) -> {2} (readings at or above the line: {3} of {4})" -f $threshold, $ThresholdPercent, $verdict, $over, $samples.Count)
if ($verdict -eq 'QUIET') { exit 0 } else { exit 1 }