# git-bash.ps1 picks Git for Windows' bash and never the WSL launcher under the Windows system
# directory, whatever order the candidates or PATH come in.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'git-bash.ps1')

$failed = 0
$total = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    $script:total++
    if ($ok) { Write-Host "  PASS  $name" }
    else { $script:failed++; Write-Host "  FAIL  $name"; if ($detail) { Write-Host "        $detail" } }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('camel-gitbash-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
try {
    $sys32 = Join-Path $env:SystemRoot 'System32'
    $fakeGit = Join-Path $scratch 'Git\usr\bin'
    New-Item -ItemType Directory -Path $fakeGit -Force | Out-Null
    $gitBash = Join-Path $fakeGit 'bash.exe'
    Set-Content -LiteralPath $gitBash -Value 'x'

    # The launcher listed first is passed over even though it exists on this machine.
    $launcher = Join-Path $sys32 'bash.exe'
    $viaCandidates = Select-GitBash @($launcher, $gitBash)
    Check 'a candidate list with the system launcher first yields the other one' ($viaCandidates -eq $gitBash) "$viaCandidates"
    Check 'a list holding only the launcher yields nothing' ($null -eq (Select-GitBash @($launcher)))
    Check 'forward slashes and a different case do not get past the filter' `
        ($null -eq (Select-GitBash @(($launcher -replace '\\', '/').ToUpper())))
    Check 'an empty list yields nothing' ($null -eq (Select-GitBash @()))
    Check 'a candidate that does not exist is skipped' ((Select-GitBash @((Join-Path $scratch 'none\bash.exe'), $gitBash)) -eq $gitBash)

    # A fake launcher first on PATH is not what Get-GitBash returns.
    $fakeSys = Join-Path $scratch 'Windows\System32'
    New-Item -ItemType Directory -Path $fakeSys -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fakeSys 'bash.exe') -Value 'x'
    $oldPath = $env:PATH
    try {
        $env:PATH = $fakeSys + ';' + $env:PATH
        $found = Get-GitBash
    } finally { $env:PATH = $oldPath }
    Check 'Get-GitBash ignores a launcher first on PATH' (($null -eq $found) -or (($found -notlike "$fakeSys*") -and ($found -notlike "$sys32*"))) "$found"
    if ($found) {
        Check 'what Get-GitBash returns exists and is a bash.exe' ((Test-Path -LiteralPath $found) -and ((Split-Path $found -Leaf) -eq 'bash.exe')) "$found"
    } else {
        Write-Host '  SKIP  no Git for Windows on this machine'
    }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$total checks, $failed failed"
if ($failed) { exit 1 }
