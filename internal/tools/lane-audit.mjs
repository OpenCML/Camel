// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Which `lane/*` branches are already inside a merged tree, and which ones are really unmerged.
 *
 * The branch gate refuses a new lane while more than a score of lanes wait, and the number it counts
 * is not the number of branches somebody would have to merge: a lane merged hours ago keeps its
 * branch, and a lane can carry the same change under another commit (a squash, a cherry-pick, two
 * lanes that made the same edit). Nobody can tell the two apart by looking at a count. This program
 * is the table that does.
 *
 * Four numbers per branch, and each one is a command the reader can run against the same tip:
 *
 *   ahead      git rev-list --count <base>..<lane>              commits of the lane the base lacks
 *   behind     git rev-list --count <lane>..<base>              commits of the base the lane lacks
 *   contained  git merge-base --is-ancestor <lane> <base>       the tip itself is on the base's history
 *   equiv      git diff <lane>..<base> -- <paths the lane changed>   empty: same content on those paths
 *
 * The paths for `equiv` are the ones the lane changed since it forked from the base
 * (`git diff --name-only <base>...<lane>`), so the comparison asks the only question worth asking:
 * on the files this branch is about, does the base already read the same? When that set is empty
 * there is nothing to compare and `equiv` is reported as vacuous rather than as a yes.
 *
 * Three judgements, and the reasons this program only reports:
 *
 *  - `ahead == 0` is not by itself "this branch can be deleted". In this table `ahead` is the plain
 *    `rev-list --count`, so a zero here does mean the tip is on the base's history -- that is what
 *    `contained` says, and the two agree by construction. The zero other tools quote is different:
 *    `git cherry` and the gate's own `--cherry-pick` count count *patches* missing from the line, and
 *    they read zero for a branch whose commits were squashed into a commit the base has. Such a
 *    branch still sits on commits the base never held, and every commit it sits on is a commit the
 *    base's history does not own. So bucket 1 states its reason per row instead of leaning on a
 *    count, and the row carries the tip sha so a deletion can be undone from the table alone.
 *
 *  - `equiv == yes` is not "already merged". Two sides reach the same text independently all the
 *    time, and a branch whose own files read the same on the base can still hold the tests, the docs
 *    or the second half of a change its author wants reviewed. So an `equiv` yes with commits the
 *    base lacks goes to bucket 2, is named "suspect", and is printed with the files it compared.
 *    This program never moves such a row into bucket 1.
 *
 *  - This program deletes no ref, creates none, and pushes nothing. It only prints. Deleting is the
 *    coordinator's judgement plus the agreement of the lane's owner, and the value of this table is
 *    that it makes that conversation about named rows instead of about a number.
 *
 * Readings are taken from the local refs and nothing is fetched: a branch whose remote moved is shown
 * as this checkout last saw it, and the report says so on its first line. A tree that has no
 * `seat/merge-rehearsal` -- the synthetic tree the merge seats build -- falls back to `develop` and
 * says that on the first line too, because "the base is tonight's rehearsal" and "the base is
 * develop" are not the same reading.
 *
 * "Contained in the base" is not "already published", and the difference is the one mistake that has
 * already cost this project work: on 2026-11-24 six lane branches were deleted whose content lived on
 * a single unmerged branch, and three of them were written up as done. A rehearsal tree is built on
 * one machine and is not on the remote yet -- the one measured here is 162 commits ahead of
 * `origin/develop` -- so every row also records whether `origin/develop` holds it, and a bucket 1 row
 * whose content is still unpublished says so on its own line. Deleting such a branch leaves the base
 * as the only ref holding those commits.
 *
 * As a self-check the program reads `refs/heads/lane/*` before and after it measures and fails
 * (exit 4) if any sha moved, so "this tool is read-only" is a reading it takes rather than a claim it
 * makes. Exit codes:
 *
 *   0   a table was produced
 *   2   asked for something it could not read (no repository, no base, no lane ref matches)
 *   3   `--check` and bucket 3 holds more branches than `--limit` (default 20) -- for the gate
 *   4   a ref moved while the program was reading (the read-only assertion failed)
 */

import { execFileSync } from 'node:child_process'
import process from 'node:process'

const DEFAULT_BASE = 'seat/merge-rehearsal'
const FALLBACK_BASE = 'develop'
const DEFAULT_RANGE = 'refs/heads/lane/*'
const DEFAULT_LIMIT = 20
// The line a merge lands on and a push publishes. Read from the local refs, like everything else, and
// used only to mark rows: it decides no bucket, because a table that refused to name a deletable row
// until a push happened would be answering a different question than the one it is asked.
const PUBLISHED = 'origin/develop'

const USAGE =
    'usage: node internal/tools/lane-audit.mjs [--base <ref>] [--range <glob>] [--limit <n>] [--check] [--json] [--repo <dir>]'

// GIT_OPTIONAL_LOCKS=0: a reader must not take the index lock. `git status` and friends refresh the
// index by default, and a merge that runs into that lock fails with "Unable to write index" -- so a
// background reader that took one would be a reader that stopped the merging it is reporting on.
const GIT_ENV = { ...process.env, GIT_OPTIONAL_LOCKS: '0', GIT_TERMINAL_PROMPT: '0', LC_ALL: 'C' }

function git(repo, args) {
    try {
        const out = execFileSync('git', ['-C', repo, ...args], {
            encoding: 'utf8',
            env: GIT_ENV,
            windowsHide: true,
            maxBuffer: 64 * 1024 * 1024,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        return { code: 0, out: String(out).replace(/\r\n/g, '\n').trim(), err: '' }
    } catch (error) {
        return {
            code: typeof error.status === 'number' ? error.status : 1,
            out: String(error.stdout || '').replace(/\r\n/g, '\n').trim(),
            err: String(error.stderr || '').replace(/\r\n/g, '\n').trim(),
        }
    }
}

const short = (sha) => sha.slice(0, 9)

/// The name a person types. The snapshot is keyed by the full ref -- two branches of one short name
/// would make a second reading ambiguous -- but every row, every command and every line of the report
/// names the branch the way the lane owner does.
const branchName = (ref) => ref.replace(/^refs\/heads\//, '')

function fail(message, code = 2) {
    process.stderr.write(`lane-audit: ${message}\n${USAGE}\n`)
    process.exit(code)
}

function parseArgs(argv) {
    const options = {
        base: DEFAULT_BASE,
        range: DEFAULT_RANGE,
        limit: DEFAULT_LIMIT,
        check: false,
        json: false,
        repo: process.cwd(),
    }
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index]
        const value = () => {
            const next = argv[index + 1]
            if (next === undefined || next.startsWith('--')) fail(`${arg} needs a value`)
            index += 1
            return next
        }
        switch (arg) {
            case '--base':
                options.base = value()
                break
            case '--range':
                options.range = value()
                break
            case '--limit': {
                const raw = value()
                const parsed = Number(raw)
                if (!Number.isInteger(parsed) || parsed < 0)
                    fail(`--limit takes a whole number, not '${raw}'`)
                options.limit = parsed
                break
            }
            case '--repo':
                options.repo = value()
                break
            case '--check':
                options.check = true
                break
            case '--json':
                options.json = true
                break
            case '--help':
            case '-h':
                process.stdout.write(`${USAGE}\n`)
                process.exit(0)
                break
            default:
                fail(`unknown argument '${arg}'`)
        }
    }
    return options
}

/// The `refs/heads/lane/*` refs and their tips, by full ref name. Read twice by `main` so the two
/// readings can be compared: a map keyed by the full name, not the short one, because the short name
/// is what a second branch of the same name would make ambiguous.
function snapshot(repo, range) {
    const listed = git(repo, ['for-each-ref', '--format=%(objectname) %(refname)', range])
    if (listed.code !== 0) fail(`git for-each-ref ${range} failed: ${listed.err || listed.out}`)
    const refs = new Map()
    for (const line of listed.out.split('\n')) {
        if (!line) continue
        const space = line.indexOf(' ')
        if (space < 0) continue
        refs.set(line.slice(space + 1), line.slice(0, space))
    }
    return refs
}

function resolveCommit(repo, ref) {
    const resolved = git(repo, ['rev-parse', '--verify', '--quiet', `${ref}^{commit}`])
    if (resolved.code !== 0 || !resolved.out) return null
    return resolved.out.split('\n')[0]
}

function count(repo, range) {
    const result = git(repo, ['rev-list', '--count', range])
    if (result.code !== 0) return null
    const parsed = Number(result.out)
    return Number.isFinite(parsed) ? parsed : null
}

/// One branch, four readings and the commands they came from.
function auditOne(repo, baseRef, baseSha, laneRef, tip, publishedRef) {
    const lane = branchName(laneRef)
    const ahead = count(repo, `${baseRef}..${lane}`)
    const behind = count(repo, `${lane}..${baseRef}`)
    const isAncestor = git(repo, ['merge-base', '--is-ancestor', lane, baseRef])
    const contained = isAncestor.code === 0
    // A non-zero exit from --is-ancestor other than 1 means git could not answer; 1 means "no".
    const containedKnown = isAncestor.code === 0 || isAncestor.code === 1

    // Whether the published line holds it too. A row contained in a rehearsal tree but not published
    // is deletable only once that tree lands, and the table has to say which row that is.
    const published = publishedRef
        ? git(repo, ['merge-base', '--is-ancestor', lane, publishedRef])
        : null
    const containedInPublished = !published ? null : published.code === 0

    // The paths this branch is about: what it changed since it forked from the base.
    const nameOnly = git(repo, ['diff', '--name-only', `${baseRef}...${lane}`])
    const changedPaths =
        nameOnly.code === 0 && nameOnly.out ? nameOnly.out.split('\n').filter(Boolean) : []

    let equiv = null
    let equivBasis = ''
    if (changedPaths.length === 0) {
        // Nothing diverges on this branch, so there is no path to compare and a "yes" here would be a
        // vacuous one. Reported as unknown, never as agreement.
        equivBasis = contained
            ? 'vacuous: the tip is already on the baseline, so the branch has no changed path of its own to compare'
            : 'vacuous: the branch changed no path relative to the baseline it forked from'
    } else {
        const compared = git(repo, ['diff', `${lane}..${baseRef}`, '--', ...changedPaths])
        if (compared.code !== 0) {
            equivBasis = `unreadable: git diff exited ${compared.code}`
        } else {
            equiv = compared.out === ''
            equivBasis = `${changedPaths.length} path(s) the branch changed, compared against ${baseRef}`
        }
    }

    const commands = {
        ahead: `git rev-list --count ${baseRef}..${lane}`,
        behind: `git rev-list --count ${lane}..${baseRef}`,
        contained: `git merge-base --is-ancestor ${lane} ${baseRef}`,
        equiv: changedPaths.length
            ? `git diff ${lane}..${baseRef} -- ${changedPaths.map((path) => (path.includes(' ') ? `'${path}'` : path)).join(' ')}`
            : null,
        containedInPublished: publishedRef
            ? `git merge-base --is-ancestor ${lane} ${publishedRef}`
            : null,
    }

    // Bucket 1 needs a reason of its own per row; the two reasons are kept apart because a reader
    // checking the table will look at one of them, not at a count.
    let bucket
    let reason
    if (contained) {
        bucket = 1
        reason = `contained：tip 是 ${baseRef} 的祖先，它的每一个提交都在基线里（ahead=0 与之一致）`
    } else if (ahead === 0 && equiv === true) {
        bucket = 1
        reason = `ahead=0 且 equiv=yes：提交都在基线上，它改过的路径内容也与基线一致`
    } else if (equiv === true) {
        bucket = 2
        reason = `equiv=yes 但 ahead=${ahead}：内容相同、提交不同，需车道主人确认`
    } else {
        bucket = 3
        reason =
            changedPaths.length === 0
                ? `ahead=${ahead}、behind=${behind}；它相对基线没有改动任何路径`
                : `ahead=${ahead}、behind=${behind}；它改过的 ${changedPaths.length} 个路径在基线上内容不同`
    }

    return {
        branch: lane,
        ref: laneRef,
        tip,
        ahead,
        behind,
        contained: containedKnown ? contained : null,
        containedInPublished,
        equiv,
        equivBasis,
        changedPaths,
        bucket,
        reason,
        commands,
    }
}

const pad = (text, width) => String(text).padEnd(width)
const num = (value) => (value === null || value === undefined ? '?' : String(value))

function reportJson(payload) {
    process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`)
}

function reportText(payload) {
    const lines = []
    const base = payload.base
    const fallbackNote = base.fellBack
        ? ` · 基线已退回 ${base.ref}：${base.requested} 不存在`
        : ''
    lines.push(
        `lane-audit · 读数基于本地 refs，未 fetch${fallbackNote} · 本工具不改任何 ref（只读：for-each-ref / rev-list / merge-base / diff）`,
    )
    lines.push(
        `base    ${base.ref} @ ${short(base.sha)}${base.fellBack ? `   （请求的 ${base.requested} 不存在，退回 ${FALLBACK_BASE}）` : '   （本地 ref）'}`,
    )
    const versus = payload.baseVsPublished
    if (versus) {
        lines.push(
            `已发布  ${versus.ref} @ ${short(versus.sha)}   （本地视图，未 fetch；基线领先它 ${num(versus.ahead)} 笔、落后 ${num(versus.behind)} 笔）`,
        )
    }
    lines.push(`range   ${payload.range}   共 ${payload.counts.branches} 条`)
    lines.push('')

    const first = payload.deletable
    lines.push(`① 可删（contained=yes，或 ahead=0 且 equiv=yes）—— ${first.length} 条`)
    if (first.length === 0) lines.push('   （无）')
    for (const row of first) {
        const unpublished = row.containedInPublished === false ? '   ← 还没上已发布线' : ''
        lines.push(`   ${pad(row.branch, 30)} ${short(row.tip)}  ${row.reason}${unpublished}`)
    }
    const unpublishedCount = first.filter((row) => row.containedInPublished === false).length
    if (unpublishedCount > 0) {
        lines.push(
            `   ⇒ 其中 ${unpublishedCount} 条的提交只在基线上：① 的前提是「基线这一支会落地」。基线没落地就删分支，那些提交只剩基线这一支 ref 持有。`,
        )
    }
    lines.push('')

    const second = payload.suspectMerged
    lines.push(`② 疑似已合（equiv=yes 但 ahead>0）—— ${second.length} 条`)
    if (second.length === 0) {
        lines.push('   （无）')
    }
    for (const row of second) {
        lines.push(
            `   ${pad(row.branch, 30)} ${short(row.tip)}  ahead=${num(row.ahead)} behind=${num(row.behind)}  ${row.reason}`,
        )
        if (row.changedPaths.length) {
            const shown = row.changedPaths.slice(0, 6).join(', ')
            const more = row.changedPaths.length > 6 ? `, …（共 ${row.changedPaths.length} 个）` : ''
            lines.push(`      比较过的路径：${shown}${more}`)
        }
    }
    if (second.length > 0) {
        lines.push('   ⇒ 内容相同但提交不同，需车道主人确认；本工具只报，不算数。')
    }
    lines.push('')

    const third = payload.unmerged
    lines.push(`③ 真的没合 —— ${third.length} 条（按 behind 降序）`)
    if (third.length === 0) lines.push('   （无）')
    for (const row of third) {
        lines.push(
            `   ${pad(row.branch, 30)} ${short(row.tip)}  ahead=${num(row.ahead)} behind=${num(row.behind)}`,
        )
    }
    lines.push('')

    const refs = payload.refsUnchanged
    lines.push(
        `refs 不变：${refs.count} 条 ${payload.range}，跑前跑后 sha 完全相同 ${refs.unchanged ? '✔' : '✘'}（本工具只读的断言）`,
    )
    if (payload.check) {
        const over = third.length > payload.limit
        lines.push(
            `check：③ ${third.length} 条 vs 上限 ${payload.limit} ⇒ ${over ? `超出 ${third.length - payload.limit} 条，exit 3` : '未超线，exit 0'}`,
        )
    }
    lines.push('')
    lines.push('核对（把 <lane> 换成表中的分支名，<base> 换成上面那一条）：')
    lines.push('  ahead      git rev-list --count <base>..<lane>')
    lines.push('  behind     git rev-list --count <lane>..<base>')
    lines.push('  contained  git merge-base --is-ancestor <lane> <base>    （exit 0 即 yes）')
    lines.push(
        '  published  git merge-base --is-ancestor <lane> origin/develop   （① 里「还没上已发布线」的判据）',
    )
    lines.push('  equiv      git diff <lane>..<base> -- <该分支改过的路径>   （无输出即 yes）')
    lines.push('')
    lines.push(
        '注：本表的 ahead 是纯 `rev-list --count`（不是 --cherry-pick 的补丁计数），所以这里 ahead=0 与 contained 是同一件事；',
    )
    lines.push(
        '    两个理由仍分列，因为别的工具报的 0 常是补丁计数 —— 那种 0 不蕴含「停在我们拥有过的点上」。',
    )
    process.stdout.write(`${lines.join('\n')}\n`)
}

function main() {
    const options = parseArgs(process.argv.slice(2))

    const topLevel = git(options.repo, ['rev-parse', '--show-toplevel'])
    if (topLevel.code !== 0 || !topLevel.out) {
        fail(`not a git repository: ${options.repo}`)
    }
    const repo = topLevel.out

    // The base: the ref asked for, else develop. Which one it is travels in the payload, because a
    // table measured against a different base is a different table.
    let baseRef = options.base
    let baseSha = resolveCommit(repo, baseRef)
    let fellBack = false
    if (!baseSha) {
        if (baseRef === FALLBACK_BASE) fail(`base '${FALLBACK_BASE}' does not exist in ${repo}`)
        baseRef = FALLBACK_BASE
        baseSha = resolveCommit(repo, baseRef)
        fellBack = true
        if (!baseSha) fail(`neither '${options.base}' nor '${FALLBACK_BASE}' exists in ${repo}`)
    }

    const before = snapshot(repo, options.range)
    if (before.size === 0) fail(`no ref matches ${options.range} in ${repo}`)

    // The published line, if this checkout has one. It decides no bucket: it only marks the rows whose
    // content is still on the base alone.
    const publishedSha = resolveCommit(repo, PUBLISHED)
    const publishedRef = publishedSha ? PUBLISHED : null
    const baseVsPublished = publishedRef
        ? {
              ref: publishedRef,
              sha: publishedSha,
              ahead: count(repo, `${publishedRef}..${baseRef}`),
              behind: count(repo, `${baseRef}..${publishedRef}`),
          }
        : null

    const rows = []
    for (const [ref, tip] of before) {
        rows.push(auditOne(repo, baseRef, baseSha, ref, tip, publishedRef))
    }

    const after = snapshot(repo, options.range)

    // The read-only assertion, taken rather than claimed.
    const moved = []
    for (const [ref, tip] of before) {
        if (after.get(ref) !== tip) moved.push({ ref, before: tip, after: after.get(ref) ?? null })
    }
    for (const [ref, tip] of after) {
        if (!before.has(ref)) moved.push({ ref, before: null, after: tip })
    }
    const refsUnchanged = { unchanged: moved.length === 0, count: before.size, moved }

    const byName = (a, b) => a.branch.localeCompare(b.branch)
    const deletable = rows.filter((row) => row.bucket === 1).sort(byName)
    const suspectMerged = rows.filter((row) => row.bucket === 2).sort(byName)
    const unmerged = rows
        .filter((row) => row.bucket === 3)
        .sort(
            (a, b) =>
                (b.behind ?? -1) - (a.behind ?? -1) ||
                (b.ahead ?? -1) - (a.ahead ?? -1) ||
                byName(a, b)
        )

    const payload = {
        tool: 'lane-audit',
        version: 1,
        readOnly: true,
        deletedRefs: [],
        localRefsOnly: true,
        fetched: false,
        base: { requested: options.base, ref: baseRef, sha: baseSha, fellBack },
        baseVsPublished,
        range: options.range,
        limit: options.limit,
        check: options.check,
        counts: {
            branches: rows.length,
            deletable: deletable.length,
            suspectMerged: suspectMerged.length,
            unmerged: unmerged.length,
        },
        refsUnchanged,
        deletable,
        suspectMerged,
        unmerged,
    }

    if (options.json) reportJson(payload)
    else reportText(payload)

    if (!refsUnchanged.unchanged) {
        process.stderr.write(
            `lane-audit: ${moved.length} ref(s) moved while this run was reading them; the table is not a reading of one state\n`,
        )
        process.exit(4)
    }
    if (options.check && unmerged.length > options.limit) process.exit(3)
    process.exit(0)
}

main()