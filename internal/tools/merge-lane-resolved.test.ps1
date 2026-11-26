# `merge-lane.ps1 -MergeCommit`: the answers its parent assertion owes.
#
# Why it exists: lanes waiting to land tonight need a resolution at merge time (one keeps its own
# version of six files a revert had removed, one appends eleven backlog rows where integration also
# appends). `merge-lane` makes its own merge, so a seat that has already merged and resolved has
# nothing to hand it -- re-running the tool repeats the conflict, and handing it a pre-merged HEAD
# trips the start-point assertion (`HEAD is not origin/<Target>`). The choices were a person running
# the tool's four stages from memory, or the tool being told which merge to read. The first is not a
# reading, it is an understanding of one.
#
# What is checked here is the assertion and only the assertion, in a throwaway repository with a bare
# origin beside it: the shape that passes, and the three that do not -- first parent not origin,
# second parent not the lane, HEAD not the commit -- plus the regression that a run without
# `-MergeCommit` still asserts the old start point. Every run stops with `-WhatIf` right after the
# merge checks, so this test never reaches the host, the build, or this repository's checkout.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-resolved.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-resolved.test.ps1 -Keep   (leave the fixture)

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$script:checked = 0
$script:failed = 0

function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    $script:checked++
    if ($Ok) {
        Write-Host "  PASS  $Name"
    } else {
        $script:failed++
        Write-Host "  FAIL  $Name"
        if ($Detail) { Write-Host "        $Detail" }
    }
}

# Not named `Git`: PowerShell resolves function names case-insensitively, so a helper called `Git`
# takes over `& git` inside itself and recurses until the call depth overflows.
function Invoke-RepoGit {
    param([string]$Command)
    $out = & git -C $script:work @($Command -split ' ') 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $Command failed: $($out -join ' ')" }
    return "$out".Trim()
}

$script:root = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-resolved-$PID")
$script:work = Join-Path $script:root 'work'
$tool = Join-Path $PSScriptRoot 'merge-lane.ps1'

Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $script:root | Out-Null

& git init -q --bare (Join-Path $script:root 'origin.git') | Out-Null
& git clone -q (Join-Path $script:root 'origin.git') $script:work 2>&1 | Out-Null
& git -C $script:work config user.email 'merge-resolved@example.invalid'
& git -C $script:work config user.name 'merge resolved test'
& git -C $script:work config commit.gpgsign false

[IO.File]::WriteAllText((Join-Path $script:work 'a.txt'), "one`n")
Invoke-RepoGit 'add -A' | Out-Null
Invoke-RepoGit 'commit -q -m base' | Out-Null
Invoke-RepoGit 'branch -m integration' | Out-Null
Invoke-RepoGit 'push -q origin integration' | Out-Null
Invoke-RepoGit 'checkout -q -b lane/x' | Out-Null
[IO.File]::WriteAllText((Join-Path $script:work 'b.txt'), "two`n")
Invoke-RepoGit 'add -A' | Out-Null
Invoke-RepoGit 'commit -q -m lane' | Out-Null
Invoke-RepoGit 'checkout -q integration' | Out-Null
Invoke-RepoGit 'merge --no-ff -q lane/x -m merge-lane-x' | Out-Null

$merge = Invoke-RepoGit 'rev-parse HEAD'
$origin = Invoke-RepoGit 'rev-parse origin/integration'
$lane = Invoke-RepoGit 'rev-parse lane/x'

function Run-MergeLane {
    param([string]$MergeCommit = '', [string]$Lane = 'lane/x')
    $laneArgs = @('-NoProfile', '-File', $tool, '-Branch', $Lane, '-Target', 'integration',
        '-MainCheckout', $script:work, '-WhatIf')
    if ($MergeCommit) { $laneArgs += @('-MergeCommit', $MergeCommit) }
    $out = & pwsh @laneArgs 2>&1
    return @{ Exit = $LASTEXITCODE; Text = (($out | ForEach-Object { "$_" }) -join "`n") }
}

# 1. The shape a merge this tool would have made: first parent origin, second parent the lane, HEAD
#    the commit. It passes the assertion and reaches the -WhatIf stop, which is the positive reading.
$ok = Run-MergeLane -MergeCommit $merge
Check 'the merge this tool would have made passes' ($ok.Exit -eq 0) "exit $($ok.Exit): $($ok.Text)"
Check 'and it says the parents are the shape this tool would have made' ($ok.Text -match 'is already a merge of') $ok.Text

# 2. A first parent that is not origin/<Target>: the push would not be a fast-forward, and the reading
#    would be about a tree that does not land.
Invoke-RepoGit 'checkout -q -b elsewhere lane/x' | Out-Null
[IO.File]::WriteAllText((Join-Path $script:work 'c.txt'), "three`n")
Invoke-RepoGit 'add -A' | Out-Null
Invoke-RepoGit 'commit -q -m elsewhere' | Out-Null
Invoke-RepoGit 'merge --no-ff -q integration -m merge-integration-into-elsewhere' | Out-Null
$firstBad = Invoke-RepoGit 'rev-parse HEAD'
$wrongFirst = Run-MergeLane -MergeCommit $firstBad
Check 'a merge whose first parent is not origin is refused' ($wrongFirst.Exit -ne 0) "exit $($wrongFirst.Exit)"
Check 'and the refusal names the first parent and origin' (($wrongFirst.Text -match 'first parent') -and ($wrongFirst.Text -match [regex]::Escape($origin.Substring(0, 9)))) $wrongFirst.Text

# 3. A second parent that is not the lane: the record would name a lane that did not come in.
Invoke-RepoGit 'checkout -q integration' | Out-Null
# Case 2 moved local integration past origin; the shape under test in case 3 is a merge whose FIRST
# parent is origin, so the branch is put back before the second-parent question is asked.
Invoke-RepoGit 'reset --hard -q origin/integration' | Out-Null
Invoke-RepoGit 'checkout -q -b lane/other' | Out-Null
[IO.File]::WriteAllText((Join-Path $script:work 'd.txt'), "four`n")
Invoke-RepoGit 'add -A' | Out-Null
Invoke-RepoGit 'commit -q -m other' | Out-Null
Invoke-RepoGit 'checkout -q integration' | Out-Null
Invoke-RepoGit 'merge --no-ff -q lane/other -m merge-lane-other' | Out-Null
$secondBad = Invoke-RepoGit 'rev-parse HEAD'
$wrongSecond = Run-MergeLane -MergeCommit $secondBad -Lane 'lane/x'
Check 'a merge of another lane is refused' ($wrongSecond.Exit -ne 0) "exit $($wrongSecond.Exit)"
Check 'and the refusal names the second parent and the lane' (($wrongSecond.Text -match 'second parent') -and ($wrongSecond.Text -match [regex]::Escape($lane.Substring(0, 9)))) $wrongSecond.Text

# 4. A commit that is not HEAD: what would be measured and pushed is not what was checked.
$wrongHead = Run-MergeLane -MergeCommit $firstBad
Check 'a merge commit that is not HEAD is refused' ($wrongHead.Exit -ne 0) "exit $($wrongHead.Exit)"

# 5. The regression: without -MergeCommit the start-point assertion is what it always was. HEAD here is
#    not origin/integration, so the run must stop with the old refusal and not with a new one.
$plain = Run-MergeLane
Check 'without -MergeCommit the start point is still asserted' (($plain.Exit -ne 0) -and ($plain.Text -match 'is not origin')) "exit $($plain.Exit): $($plain.Text)"

# 6. The shape the arrival check is about: the seat merged integration into itself and made its merge on top,
#    so the first parent is an OLDER origin/<Target>. That is a different mistake from starting from a
#    base origin never had, and the refusal has to say which one it is -- the repair differs.
Invoke-RepoGit 'push -q origin elsewhere:integration' | Out-Null
Invoke-RepoGit "checkout -q -B oldtip $origin" | Out-Null
Invoke-RepoGit 'merge --no-ff -q lane/x -m merge-lane-on-an-old-tip' | Out-Null
$staleTip = Invoke-RepoGit 'rev-parse HEAD'
$mergedIn = Run-MergeLane -MergeCommit $staleTip
Check 'a merge made on an older origin tip is refused' ($mergedIn.Exit -ne 0) "exit $($mergedIn.Exit)"
Check 'and the refusal says integration was merged into the seat, not the lane into integration' ($mergedIn.Text -match 'merging integration into the seat') $mergedIn.Text
if (-not $Keep) { Remove-Item -LiteralPath $script:root -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host "$script:checked checks, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0