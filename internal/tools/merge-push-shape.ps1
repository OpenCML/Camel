# The shape every commit that enters a target branch must have, read immediately before the push.
#
# The ruling (D-1124-78): each commit that lands on `integration` is a MERGE commit whose first
# parent is the tip `origin/integration` held at that moment. The three seats of 2026-11-24 satisfied
# the weaker rule -- the push was a fast-forward -- by merging `integration` into the seat and then
# fast-forwarding the seat onto it. The history that produced has four lane commits and four
# "merge integration into the slots seat" commits on the first-parent chain, and every tool that
# reads its arrivals by first parent (the promotion report, the arrival count) reads those four lane
# commits as four arrivals. Nothing was pushed that was not measured; the chain was wrong.
#
# So the check is here, beside both merge tools (`merge-lane.ps1`, `merge-batch.ps1`), and it is the
# last thing read before a push rather than something a tool remembers it did: a shape checked only
# where the commit is made is a shape nobody checks at the moment it is published.
#
# It answers three questions in one reading and refuses on two of them:
#
#   'single parent'  the commit is not a merge -- a lane commit would enter the target directly;
#   'first parent'   the commit is a merge, but not of this tip: the target moved, or the merge was
#                    made against a tree origin no longer holds (the seat merged the target into
#                    itself and fast-forwarded);
#   'arrived'        a merge whose first parent is the tip origin holds now. Only this one may push.
#
# The refusal is an object, not an exit: the caller decides what to do with it. In `merge-lane.ps1`
# a target that moved under a documentation-only difference is still re-landed on the new tip (the
# recovery), and the commit it then pushes is made in the same shape, so this check passes on the
# second read as well; a difference that touches code stops and hands the decision to whoever started
# the merge.

function Test-PushFirstParent {
    param(
        [Parameter(Mandatory)][string] $Tree,
        [Parameter(Mandatory)][string] $Target,
        # The commit the caller is about to push -- not `HEAD` read again here, because the two can
        # be different commits and the whole point is to check the one that would travel.
        [Parameter(Mandatory)][string] $Sha
    )

    $sha = "$Sha".Trim()
    $shortSha = if ($sha.Length -gt 9) { $sha.Substring(0, 9) } else { $sha }

    # The tip is read from the remote, not from a local branch and not from a sha the caller wrote
    # down earlier: a run that started twenty minutes ago holds a reading about a tree that has since
    # been pushed to. Whatever survives here is true as of this fetch, and the push itself is the
    # remaining window.
    $fetch = @(& git -C $Tree fetch origin --prune 2>&1)
    if ($LASTEXITCODE -ne 0) {
        return [pscustomobject]@{
            Ok = $false; Kind = 'fetch failed'; Sha = $sha; Tip = ''; FirstParent = ''; Parents = @()
            Message = @("refusing to push $shortSha`: git fetch origin failed, so what origin/$Target holds cannot be read and the shape of the commit cannot be checked. Nothing was pushed.",
                ($fetch | ForEach-Object { "  $_" }))
        }
    }
    $tip = "$(& git -C $Tree rev-parse --verify --quiet "refs/remotes/origin/$Target^{commit}" 2>$null)".Trim()
    if (-not $tip) {
        return [pscustomobject]@{
            Ok = $false; Kind = 'no tip'; Sha = $sha; Tip = ''; FirstParent = ''; Parents = @()
            Message = @("refusing to push $shortSha`: origin/$Target does not resolve after the fetch, so there is no tip for a first parent to equal. Nothing was pushed.")
        }
    }
    $tipShort = $tip.Substring(0, 9)

    # One read answers both questions: the first token is the commit itself and the rest are its
    # parents in order, so `Parents[1]` IS `<sha>^1` -- the reading the ruling names -- and the count
    # is what tells a merge from a lane commit.
    $line = "$(& git -C $Tree rev-list --parents -n 1 $sha 2>$null)".Trim()
    $tokens = @($line -split '\s+' | Where-Object { $_ })
    $parents = if ($tokens.Count -gt 1) { @($tokens[1..($tokens.Count - 1)]) } else { @() }
    $first = if ($parents.Count -ge 1) { $parents[0] } else { '' }
    $firstShort = if ($first.Length -gt 9) { $first.Substring(0, 9) } else { $first }

    if ($parents.Count -lt 2) {
        return [pscustomobject]@{
            Ok = $false; Kind = 'single parent'; Sha = $sha; Tip = $tip; FirstParent = $first; Parents = $parents
            Message = @(
                "refusing to push $shortSha to $Target`: it is not a merge commit -- it has $($parents.Count) parent(s), and every arrival must be a merge whose first parent is the tip.",
                "  一笔车道提交直接进了 $Target (a lane commit went straight into $Target). A single-parent commit cannot say which lane it speaks for and the first-parent chain cannot tell it from a merge.",
                "  Redo it as a merge: git checkout -B tmp origin/$Target; git merge --no-ff <the lane>. Nothing was pushed.")
        }
    }

    if ($first -ne $tip) {
        return [pscustomobject]@{
            Ok = $false; Kind = 'first parent'; Sha = $sha; Tip = $tip; FirstParent = $first; Parents = $parents
            Message = @(
                "refusing to push $shortSha`: its first parent is $firstShort and origin/$Target is $tipShort.",
                "  你在把 $Target 合进席位，而不是把车道合进 $Target -- you are merging $Target into the seat and fast-forwarding the seat onto it, not merging the lane into $Target. Every arrival must be a merge of the lane onto the tip $Target holds at that moment.",
                "  Redo it on the tip:",
                "    git -C <the seat checkout> checkout -B tmp origin/$Target",
                "    git -C <the seat checkout> merge --no-ff <the lane, or the merge you already prepared>",
                '  and turn on `git config rerere.enabled true` first, so the conflict resolutions already made are reused instead of being solved again. Nothing was pushed.')
        }
    }

    return [pscustomobject]@{
        Ok = $true; Kind = 'arrived'; Sha = $sha; Tip = $tip; FirstParent = $first; Parents = $parents
        Message = @("$shortSha is a merge whose first parent is origin/$Target at $tipShort")
    }
}