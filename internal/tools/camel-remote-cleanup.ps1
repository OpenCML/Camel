# Deleting a branch is two deletions: the branch here, and the staging ref of the same name in the
# bare every run pushes through.
#
# The gate leaves that ref behind on purpose -- the next run of the same branch then pushes eleven
# objects instead of its whole history -- and `cleanup_staging_refs` in `camel-remote.ps1` reaps the
# refs whose branch is gone. That sweep only runs when a gate run does, though, and it cannot be the
# answer to "I am deleting this branch now": the ref would stay, and be swept later, only if a run
# happens to follow. So the step itself does both, which is what this file is.
#
# It is two-phase, because deleting a branch is not undoable and the list of what goes must be read
# before it does (rule 9): without `-Apply` it prints every branch, its tip, where its content also
# lives, and how many staging refs stand, and changes nothing. With `-Apply` it removes the staging
# refs in one ssh, then the branches, and prints the counts on both sides.
#
# What it refuses, and why:
#   * a branch whose worktree exists -- `git branch -D` would fail on it anyway, and the worktree is
#     the thing to remove first (`git worktree remove`, never --force);
#   * every branch of the list when the staging refs cannot be read -- half a deletion is worse than
#     none, and the read is what the whole step is for.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.ps1 -Branch <branch>
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.ps1 -Branch <branch> -Apply
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.ps1 -Branch <branch> -Apply -KeepLocalBranch
#
# Exit codes: 0 done (or a plan printed), 2 usage, 3 refused.

[CmdletBinding()]
param(
    # Branch(es) to delete: the local branch and its staging ref in the host's bare.
    [Parameter(Mandatory = $true, Position = 0)]
    [string[]]$Branch,
    # A worktree of the repository the branches belong to. Every worktree of one repository shares
    # its refs, so the tool's own tree is the default and any other is the same repository.
    [string]$Path = '',
    # Delete. Without it the plan is printed and nothing is touched.
    [switch]$Apply,
    # Leave the local branch: only its staging ref goes.
    [switch]$KeepLocalBranch,
    # Path of the machine-local settings file; empty means the one beside this file.
    [string]$LocalConfig = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')

function Stop-Usage {
    param([string]$Message)
    Write-Host "[camel-remote-cleanup] $Message"
    exit 2
}

# The staging bare and how to reach it: both are this machine's, so both come from the settings file.
$cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $PSScriptRoot
if (-not $cfg.sshHost) { Stop-Usage "no sshHost in $($PSScriptRoot)\camel-gate.local.json" }
$sshHost = $cfg.sshHost
$hostBare = $cfg.hostBareRepo

$repo = if ($Path) { (Resolve-Path -LiteralPath $Path).Path } else { (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path }
$branchNames = @($Branch | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($branchNames.Count -eq 0) { Stop-Usage 'no branch named' }

# The reading this step is judged by. A failed one is not an empty one: `git -C <bare>` that cannot
# be read would otherwise look like "there are no staging refs" and the plan would say so.
$stagingRefs = @()
$stagingReadWhy = ''
try {
    $stagingRefs = @(git -C $repo ls-remote "$sshHost`:$hostBare" 'refs/heads/*' 2>&1 |
        ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { $stagingReadWhy = "git exited $LASTEXITCODE" }
    $stagingRefs = @($stagingRefs | Where-Object { $_ -match '^[0-9a-f]{40,}\s' } |
        ForEach-Object { ($_ -split '\s+')[1] -replace '^refs/heads/', '' })
} catch {
    $stagingRefs = @()
    $stagingReadWhy = "$_"
}

# The worktrees this repository has, so a branch that one of them holds is refused before anything
# is deleted rather than failing halfway through.
$checkedOut = @()
try {
    $checkedOut = @(git -C $repo worktree list --porcelain 2>$null |
        Where-Object { $_ -like 'branch refs/heads/*' } |
        ForEach-Object { $_.Substring(('branch refs/heads/').Length) })
} catch {
    $checkedOut = @()
}

# Where each branch's content also lives. The rule the repository already keeps (rule 9) is that a
# branch may go once its content is somewhere else; this prints the answer instead of deciding it
# silently. `-D` is the deletion this uses, because a zero-content branch is exactly the one `-d`
# refuses (it is not an ancestor -- there is nothing to be an ancestor of).
$contentRefs = @('refs/heads/develop', 'refs/heads/integration', 'refs/heads/main',
    'refs/remotes/origin/develop', 'refs/remotes/origin/integration', 'refs/remotes/origin/main')
function Get-ContentHome {
    param([string]$Name)
    $homes = @()
    foreach ($r in $contentRefs) {
        git -C $repo rev-parse --verify --quiet "$r^{commit}" *> $null
        if ($LASTEXITCODE -ne 0) { continue }
        git -C $repo merge-base --is-ancestor "refs/heads/$Name" $r 2>$null
        if ($LASTEXITCODE -eq 0) { $homes += ($r -replace '^refs/(heads|remotes)/', '') }
    }
    foreach ($t in @(git -C $repo tag --list 'archive/*' 2>$null)) {
        git -C $repo merge-base --is-ancestor "refs/heads/$Name" "refs/tags/$t" 2>$null
        if ($LASTEXITCODE -eq 0) { $homes += "tag $t" }
    }
    return @($homes)
}

Write-Host ("[camel-remote-cleanup] worktree {0}" -f $repo)
Write-Host ("[camel-remote-cleanup] staging bare {0}:{1}" -f $sshHost, $hostBare)
Write-Host ("[camel-remote-cleanup] local branches: {0}" -f @(git -C $repo for-each-ref --format='%(refname:lstrip=2)' refs/heads 2>$null).Count)
if ($stagingReadWhy) {
    Write-Host ("[camel-remote-cleanup] not reading the staging refs: {0}; nothing will be deleted" -f $stagingReadWhy)
} else {
    Write-Host ("[camel-remote-cleanup] staging refs: {0}" -f $stagingRefs.Count)
}

$refused = 0
$plan = @()
foreach ($n in $branchNames) {
    $tip = (git -C $repo rev-parse --verify --quiet "refs/heads/$n^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $tip) {
        Write-Host ("[camel-remote-cleanup] {0}: no such local branch" -f $n)
        $refused++
        continue
    }
    $tip = "$tip".Trim()
    $subject = (git -C $repo log -1 --format='%s' $tip 2>$null)
    if ($checkedOut -contains $n) {
        Write-Host ("[camel-remote-cleanup] {0}: refused, a worktree has it checked out; remove that worktree first (git worktree remove, never --force)" -f $n)
        $refused++
        continue
    }
    $homes = @(Get-ContentHome $n)
    $where = if ($homes.Count -gt 0) { $homes -join ', ' } else { 'nowhere I can see -- archive it first, or say in the record where it went' }
    $refState = if ($stagingRefs -contains $n) { 'present' } else { 'absent' }
    Write-Host ("[camel-remote-cleanup] {0}" -f $n)
    Write-Host ("    tip      {0} {1}" -f $tip.Substring(0, [Math]::Min(12, $tip.Length)), $subject)
    Write-Host ("    content  {0}" -f $where)
    Write-Host ("    plan     {0}" -f $(if ($KeepLocalBranch) { "delete staging ref ($refState); the branch stays" } else { "delete staging ref ($refState) and the local branch" }))
    $plan += $n
}

if ($refused -gt 0) {
    Write-Host ("[camel-remote-cleanup] {0} of {1} refused; nothing was changed" -f $refused, $branchNames.Count)
    exit 3
}
if (-not $Apply) {
    Write-Host '[camel-remote-cleanup] plan only: nothing was changed. Add -Apply to delete.'
    exit 0
}

# The staging refs first, in one connection: if that fails nothing local has been deleted yet, and
# a ref left standing is harmless (the next run of the branch overwrites it). The counts either side
# are printed by the host itself, in one ssh, so they are the two sides of this deletion and not of
# some other run's.
$sh = 'b="$HOME/' + $hostBare + '"; echo "staging refs before: $(git -C "$b" for-each-ref refs/heads/ | wc -l)"; '
foreach ($n in $plan) {
    $sh += 'git -C "$b" update-ref -d "refs/heads/' + $n + '" 2>/dev/null && echo "removed refs/heads/' + $n + '"; '
}
$sh += 'echo "staging refs after: $(git -C "$b" for-each-ref refs/heads/ | wc -l)"'
$hostOut = @(ssh $sshHost $sh 2>&1)
foreach ($line in $hostOut) { Write-Host "[camel-remote-cleanup] $line" }

if (-not $KeepLocalBranch) {
    foreach ($n in $plan) {
        git -C $repo branch -D $n 2>&1 | ForEach-Object { Write-Host "[camel-remote-cleanup] $_" }
        if ($LASTEXITCODE -ne 0) { Write-Host ("[camel-remote-cleanup] could not delete the local branch {0}" -f $n) }
    }
}
Write-Host ("[camel-remote-cleanup] local branches: {0}" -f @(git -C $repo for-each-ref --format='%(refname:lstrip=2)' refs/heads 2>$null).Count)
exit 0