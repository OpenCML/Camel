# Waits for origin/fsh-dev to move and then exits, so the session is told at once instead of
# waiting for the next scheduled check.
#
# The 15-minute reminder stays as the fallback: this watcher exists to make "fsh-dev moved" arrive
# within about a minute, not to replace the check that reads the task board afterwards. A watcher
# that ends without a change is normal -- it is capped at -Hours so it cannot run forever, and a new
# one is started after each notification.
#
#   pwsh -NoProfile -File internal/tools/watch-fsh-dev.ps1 -Hours 8
#   pwsh -NoProfile -File internal/tools/watch-fsh-dev.ps1 -Repo <checkout> -Hours 2
#
# Exit 0 and a line starting with "fsh-dev moved" when the branch moved; "watch ended" when the cap
# was reached; anything else (a missing baseline) means the remote was unreachable when it started.

param(
    [int]$IntervalSeconds = 60,
    [int]$Hours = 8,
    # The checkout to read. Empty means the local settings file's `watchRepo`.
    [string]$Repo = '',
    # The local settings file; empty means camel-gate.local.json beside this script.
    [string]$LocalConfig = ''
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')
$cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $PSScriptRoot -Override @{ watchRepo = $Repo }
if (-not $cfg.watchRepo) { Stop-CamelGateConfig 'watch-fsh-dev' $cfg 'watchRepo' }
$repo = (Resolve-Path $cfg.watchRepo).Path

function Get-RemoteSha([string]$ref) {
    $out = git -C $repo ls-remote origin $ref 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $out) { return $null }
    return ($out -split '\s+')[0]
}

$base = $null
for ($attempt = 0; $attempt -lt 5 -and -not $base; $attempt++) {
    $base = Get-RemoteSha 'refs/heads/fsh-dev'
    if (-not $base) { Start-Sleep -Seconds 20 }
}
if (-not $base) {
    Write-Output "watch failed: origin unreachable, fsh-dev baseline unknown"
    exit 2
}

$started = Get-Date
$deadline = $started.AddHours($Hours)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds $IntervalSeconds
    $sha = Get-RemoteSha 'refs/heads/fsh-dev'
    if (-not $sha) { continue }
    if ($sha -ne $base) {
        Write-Output ("fsh-dev moved {0} -> {1} (seen {2:o}, watching for {3:N0} min)" -f `
            $base.Substring(0, 8), $sha.Substring(0, 8), (Get-Date), ((Get-Date) - $started).TotalMinutes)
        # Fetch and list what arrived, so the notification is actionable on its own instead of only
        # saying that something changed.
        git -C $repo fetch --quiet origin 2>&1 | Out-Null
        $commits = git -C $repo log --oneline "develop..$sha" 2>$null
        if ($commits) {
            Write-Output 'new commits:'
            $commits | ForEach-Object { Write-Output ("  {0}" -f $_) }
        } else {
            Write-Output 'no commits relative to local develop (already merged?)'
        }
        exit 0
    }
}

Write-Output ("watch ended after {0} h with fsh-dev still at {1}" -f $Hours, $base.Substring(0, 8))
exit 3