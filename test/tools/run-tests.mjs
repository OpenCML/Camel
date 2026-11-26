// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import fs from 'fs'
import path from 'path'
import os from 'os'
import { performance } from 'perf_hooks'
import { spawn, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { killProcessTree } from './kill-tree.mjs'
import { parseTomlFile } from './toml-lite.mjs'
import { compareGirDotIsomorphic } from './gir-dot.mjs'
import { applyNormalizers } from './normalizers.mjs'
import {
    CUDA_HIDDEN,
    CUDA_VISIBLE_DEVICES,
    declaredVisibility,
    hidesCudaDevices,
    pythonEntryPoints
} from './cuda-visibility.mjs'
import { CUDA_DEVICE_ORDER, PCI_BUS_ID, chooseFreeDevice } from './free-device.mjs'
import { RUN_DIRS_KEPT, SCRATCH_DIR, formatRetention, pruneRunnerOutput, runnerOutputPath } from './runner-output.mjs'
import { judgeTree, markChildrenNested, readTreeState, readTreeStateAsync, startTreeWatch, treeGuardRole } from './tree-guard.mjs'
import { argProblems } from './cli-arg-domains.mjs'
import { tierAccountingLine, tierAccountingProblems } from './tier-accounting.mjs'
import { formatIsoTime, provenanceLine, staleAllowedMark } from './provenance.mjs'
import { normalizeReading, READING_NORMAL_VERSION } from './reading-normal.mjs'
import { describeRefusals, formatRefusalSpec, matchesRefusal, parseRefusalSpec, refusals } from './reading-refusal.mjs'
import { judgeTranscript, openTranscript, transcriptRefusal } from './transcript.mjs'

const __filename = fileURLToPath(import.meta.url)
const TOOLS_ROOT = path.dirname(__filename)
const TEST_ROOT = path.dirname(TOOLS_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const VARS_PATH = path.join(TEST_ROOT, 'vars.toml')
// The wrapper a `cpu_budget_ms` test runs under: it reaps exactly that test's process tree and
// reports the CPU time the kernel charged it on file descriptor 3 (its header has the why; the
// rule it implements is quoted where the reading is judged, in runOneTest).
const CHILD_CPU_TOOL = path.join(TOOLS_ROOT, 'child-cpu.mjs')
// Which run owns the `[TREE]` verdict, read once at load so that marking the children below cannot
// be mistaken for this run's own role (test/tools/tree-guard.mjs#treeGuardRole): the outermost run
// judges, and a run started inside it defers.
const TREE_ROLE = treeGuardRole(process.env)
// Whether this host's kernel can report the CPU time of a process tree at all: the reading
// test/tools/child-cpu.mjs makes is `cutime` + `cstime` of /proc/self/stat, which no other platform
// has. Where it exists the wrapper runs for every test and the margin rule is judged on CPU time;
// where it does not (a Windows or macOS developer machine) the wrapper cannot report a figure
// either, so it is not started for the tests that do not ask for one and the margin rule is skipped
// with that reason -- never read off the wall clock, which there measures the host's load as much
// as the test's work -- and the run says so, once, and again with its count at the end. The shared
// host the gate runs on is Linux, so the skip never decides a gate. What no platform loses is the
// timeout itself: runProcess kills a child at its `timeout_ms` whatever this rule can read.
const CAN_READ_CHILD_CPU = fs.existsSync('/proc/self/stat')
// Where a run's record goes. `test/results` unless CAMEL_TEST_RESULTS_ROOT names another tree: a
// check that has to assert what the retention kept drives two real runs into a scratch tree
// (test/cases/tools/retention/retention.mjs), and the tree it prunes is the one it writes.
const RESULTS_ROOT = process.env.CAMEL_TEST_RESULTS_ROOT
    ? path.resolve(process.env.CAMEL_TEST_RESULTS_ROOT)
    : path.join(TEST_ROOT, 'results')
const LOG_ROOT = path.join(RESULTS_ROOT, 'log')
const REPO_ROOT = path.dirname(TEST_ROOT)
const IS_WINDOWS = process.platform === 'win32'

// The host's token pool. One worker process, one token: the test case takes it through the one line of
// shell that sits in front of every case on the build host (`internal/tools/host-case.sh`), and the
// kernel returns it when that case's process tree exits -- so a case that hangs or is killed cannot
// carry a token away. A case that arrives when the pool is empty *queues*; it does not fail. When no
// pool is configured (`CAMEL_TOKEN_DIR` unset) nothing here changes and a lane runs as it always did.
const HOST_CASE = path.join(REPO_ROOT, 'internal', 'tools', 'host-case.sh')
const TOKEN_POOL = !IS_WINDOWS && !!process.env.CAMEL_TOKEN_DIR && fs.existsSync(HOST_CASE)
// What the run will report as `tokenWaitSeconds`: how many cases took a token, how long they queued in
// all, and how many of them queued at all. A pool that is never contended reports a wait of 0.0 s,
// which is a reading; a report that is absent would be a pool nobody can tell from an unused one.
const tokenTaking = { cases: 0, waitMs: 0, queued: 0 }
let tokenSerial = 0
// The build a run addresses. A Release build lands in out/latest; a Debug build has its own tree
// at out/debug (scripts/collect-out.js), so `--build debug` cannot silently read a Release binary
// and the two configurations can be tested without rebuilding one over the other.
const BUILDS = {
    release: { dir: 'latest', label: 'Release' },
    debug: { dir: 'debug', label: 'Debug' },
}

/**
 * The fields a plan file may contain. A key that is not here is an error, not a comment: a test
 * that wrote `expect_stdout_exact` under a misspelled name used to pass while asserting nothing.
 * `desc` is human-readable documentation, and `perf_budget_platform` is the provenance of a
 * benchmark budget; both are accepted, and neither turns into an assertion.
 */
const PLAN_FIELDS = new Set([
    'id', 'desc', 'order', 'tier', 'debug_tier', 'parallel', 'tags', 'defaults', 'tests', 'sweeps',
])
const TEST_FIELDS = new Set([
    'name', 'desc', 'kind', 'case', 'args', 'envs', 'tool', 'timeout_ms', 'cpu_budget_ms', 'verify_timeout_ms',
    'skip_unless', 'skip_unless_timeout_ms', 'require_artifacts', 'parallel', 'tier', 'sweep', 'cuda_devices',
    'cuda_free_device',
    'known_fail', 'known_fail_owner', 'known_fail_reason', 'known_fail_note', 'known_fail_msg',
    'known_fail_ends_when',
    'failure_note',
    'silenced_warnings',
    'backends', 'backends_agree', 'compare_to', 'compare_fields',
    'golden', 'snapshot_stream', 'snapshot_compare', 'normalizers',
    'expect_exit', 'expect_signal', 'expect_stdout_contains', 'expect_stdout_exact', 'expect_stdout',
    'expect_stdout_matches',
    'expect_stdout_starts_with', 'expect_stdout_not_contains', 'expect_stderr_contains',
    'expect_output_contains', 'expect_output_not_contains',
    'expect_diagnostic_name', 'expect_diagnostic_code',
    'expect_diagnostic_line', 'expect_diagnostic_column', 'expect_diagnostic_file', 'expect_no_diagnostics',
    'expect_refusal',
    'perf_budget_mean_ms', 'perf_budget_tolerance_pct', 'perf_budget_platform',
    'verify_script', 'verify_script_path', 'verify_js',
])
const SWEEP_FIELDS = new Set([
    'name', 'plans', 'exclude', 'envs', 'args', 'skip_env_prefix', 'skip_args_prefix', 'timeout_scale',
    'fast_sample', 'risk_tags', 'risk_sample',
])

/** A plan file that contradicts the schema; it fails the plan, not the run. */
class PlanSchemaError extends Error {
    constructor(message) {
        super(message)
        this.name = 'PlanSchemaError'
    }
}

function planLabel(planPath) {
    const relative = path.relative(REPO_ROOT, planPath)
    if (relative && !relative.startsWith('..') && !path.isAbsolute(relative)) {
        return relative.split(path.sep).join('/')
    }
    return planPath
}

/** Levenshtein distance, for "did you mean": a one-letter typo must name the field it meant. */
function editDistance(lhs, rhs) {
    let previous = Array.from({ length: rhs.length + 1 }, (_, index) => index)
    for (let i = 1; i <= lhs.length; i++) {
        const current = [i]
        for (let j = 1; j <= rhs.length; j++) {
            const substitute = previous[j - 1] + (lhs[i - 1] === rhs[j - 1] ? 0 : 1)
            current[j] = Math.min(previous[j] + 1, current[j - 1] + 1, substitute)
        }
        previous = current
    }
    return previous[rhs.length]
}

function closestField(key, allowed) {
    let best = null
    let bestDistance = 3
    for (const field of allowed) {
        const distance = editDistance(key, field)
        if (distance < bestDistance) {
            best = field
            bestDistance = distance
        }
    }
    return best
}

function checkFields(planPath, entry, allowed, where) {
    for (const key of Object.keys(entry)) {
        if (allowed.has(key)) continue
        const suggestion = closestField(key, allowed)
        const hint = suggestion ? `; did you mean '${suggestion}'?` : ''
        throw new PlanSchemaError(`${planLabel(planPath)}: ${where}: unknown key '${key}'${hint}`)
    }
}

function checkFieldType(planPath, entry, field, where, type) {
    const value = entry[field]
    if (value === undefined) return
    const ok = type === 'string'
        ? typeof value === 'string'
        : type === 'number'
            ? typeof value === 'number' && Number.isFinite(value)
            : type === 'boolean'
                ? typeof value === 'boolean'
                : type === 'strings'
                    ? Array.isArray(value) && value.every((one) => typeof one === 'string')
                    : false
    if (ok) return
    const shown = type === 'number' || type === 'boolean' ? String(value) : JSON.stringify(value)
    throw new PlanSchemaError(`${planLabel(planPath)}: ${where}: ${field} must be a ${type}, got ${shown}`)
}

// The fields the runner walks one item at a time. A string written where a list belongs is not a
// shorter list: `for (const needle of "will not run")` visits eleven single characters, and one-letter
// needles are satisfied by output that says something else entirely. Measured on
// `feat/trans/hand_on.plan.toml#after_nop_never_run`, whose `expect_stderr_contains = "will not run"`
// passed for `"… will  not run"` (two spaces: the sentence is gone) and for
// `"unknown pipeline option: --gir"` (another sentence that happens to hold all eleven letters). A
// green like that is worse than no reading, because a green is counted and nobody re-reads it, so the
// shape is refused where the plan is read: the run then reports a plan it cannot read, which is a
// state a reader can tell from a red and from a green.
const LIST_FIELDS = [
    'args', 'envs', 'normalizers', 'compare_fields', 'backends_agree',
    'expect_output_contains', 'expect_output_not_contains',
    'expect_stdout_contains', 'expect_stdout_not_contains', 'expect_stderr_contains',
]
const SWEEP_LIST_FIELDS = ['plans', 'exclude', 'envs', 'args', 'risk_tags']

function checkListFields(planPath, entry, where, fields) {
    for (const field of fields) {
        const value = entry[field]
        if (value === undefined) continue
        if (Array.isArray(value) && value.every((one) => typeof one === 'string')) continue
        const letters = typeof value === 'string' ? [...value] : null
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: ${field} must be a list of strings, got ${JSON.stringify(value)}` +
                (letters === null
                    ? ''
                    : `: a string is read one character at a time, so this asserts ${letters.length} single ` +
                      `characters (${letters.map((one) => JSON.stringify(one)).join(' ')}) instead of the ` +
                      `sentence it wrote`),
        )
    }
}

// `expect_diagnostic_file` names the file a diagnostic was reported *in*, and it is one path, not a
// list. A diagnostic has exactly one file, and a list holding several would let any of them satisfy
// the pin -- which is the hole the field exists to close: a pin on a name, a line and a column alone
// says "this sentence appears somewhere in this program", and a sentence of another file satisfies
// it whenever the two files happen to agree on the line number (`operator_overloads.conflict_import`
// pinned line 4 while the conflict was reported in `plus_c.cml` and the entry's own file carried a
// different diagnostic at 4:1). So the shape is a string and nothing else, refused here, where the
// plan, the entry and the key are named, in the same posture as `checkListFields`: an array, a
// number, a boolean or an empty string is a plan the runner cannot read, not a plan that asserts
// something smaller.
function checkDiagnosticFileField(planPath, entry, where) {
    const value = entry.expect_diagnostic_file
    if (typeof value === 'string' && value.trim().length > 0) return
    const shown = Array.isArray(value)
        ? `${JSON.stringify(value)}: a list would let any of several files satisfy the pin, and the ` +
          `field exists because one file is the claim`
        : typeof value === 'string'
            ? `${JSON.stringify(value)}: an empty string names no file`
            : JSON.stringify(value)
    throw new PlanSchemaError(
        `${planLabel(planPath)}: ${where}: expect_diagnostic_file must be a string naming one file, got ${shown}`,
    )
}

// The three things the rule asked a `known_fail` entry to carry: the
// mark, who is responsible for making the case green, and why it cannot pass today. The mark alone
// says "we accept this red"; it says nothing about who ends it or where the case is stuck, and until
// this check existed the other two lived in the prose of a note that a review had to remember to
// read. A ruling kept by memory is kept until the next writer, so the owner and the reason are
// fields, and a plan that leaves either one out fails when the plan is loaded -- with the plan, the
// case and the missing one named. There is deliberately no default for either: a default turns the
// gate into decoration.
//
// A field that only the gate reads is not read at all in practice, so `known_fail_owner` is also
// printed on the `[XFAIL]` line of every run (`printResult`). That is the difference between a
// promise in a file and a promise in front of the person reading the gate.
const OWNER_PLACEHOLDERS = new Set([
    '-', '--', '?', 'n/a', 'na', 'nil', 'none', 'null', 'nobody', 'anyone', 'somebody',
    'tbd', 'todo', 'unknown', 'unassigned', '待定', '待修', '暂无', '未知', '无人',
])
// A reason that is one of these says the case is red without saying what stands in the way; the
// next reader cannot act on it and cannot tell it from a stale one.
const REASON_PLACEHOLDERS = new Set([
    '-', '--', '?', 'n/a', 'na', 'nil', 'none', 'null',
    'tbd', 'todo', 'fixme', 'known issue', 'not supported yet', 'see above',
    '待定', '待修', '待补', '待查', '暂无', '暂不支持', '未实现',
])
// What "stuck" looks like on paper, in the three shapes this tree already writes it in: the
// refusal or the law quoted from the compiler (`` `skip=rule uri=tensor:sum reason=unbound` ``),
// the file and optionally the symbol in it (`test/tools/run-tests.mjs#loadPlanTests`), or the page
// that decides the question. A reason that names none of them is a mood, not a wall.
const WALL_SPELLINGS = [
    /`[^`]+`/,
    /[A-Za-z0-9_./-]+\.(?:cml|cpp|cc|h|hpp|py|mjs|js|toml|md)(?:#[A-Za-z0-9_:]+)?/,
    /[A-Za-z0-9_./-]+#[A-Za-z0-9_:]+/,
]

function knownFailIsMarked(mark) {
    return mark === true || (Array.isArray(mark) && mark.length > 0)
}

/**
 * The two shapes `known_fail_ends_when` may hold, read into one comparable thing. `null` means the
 * value is not one of them -- which the loader refuses and the checker reports, so nothing past the
 * load has to guess what a third shape would mean.
 *
 * `prints` is the line this entry prints when the wall moves, and it is found in the entry's own
 * output. `passes` names another case: when that case passes in the same run, this mark has ended.
 * Both are readings of a run, which is the whole point of the rule -- the field used to hold a
 * sentence, and a sentence is not something a run can disagree with.
 */
function knownFailEndCondition(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null
    const keys = Object.keys(value)
    if (keys.length !== 1 || (keys[0] !== 'prints' && keys[0] !== 'passes')) return null
    const text = value[keys[0]]
    if (typeof text !== 'string' || text.trim().length === 0) return null
    return { kind: keys[0], text: text.trim() }
}

/** How a condition reads on a line: the `[KNOWN]` roster and the strict-xfail message share it. */
function describeEndCondition(condition) {
    if (!condition) return null
    return condition.kind === 'prints'
        ? `prints "${condition.text}"`
        : `passes "${condition.text}"`
}

/**
 * What a `prints` condition is read against: the bytes the case's own process wrote, normalized the
 * way the case's own assertions read them, and nothing the runner wrote *about* the case.
 *
 * It was read from `failures + stdout + stderr`, which made every condition that
 * also appears in an `expect_output_contains` true by construction: a failure line quotes the line
 * it did not find (`output does not contain 'stated mean value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]'`),
 * so the condition was satisfied by the sentence saying it was not, and three legitimate marks read
 * as `known failure has ended` in the merge rehearsal (`modules.onnx/ort_loop_values_dynamic` and the
 * two `stated_mean_axis_*` gradients). A condition that can be matched by the sentence explaining
 * that it is unmet is not a condition, it is decoration -- the same family as an assertion that stays
 * green when nothing can match it.
 *
 * The other shape, `passes`, is a reading of another case's verdict and is taken from `completed`
 * (`settlePassesConditions`), a field of its own. No condition states a thing about the text of a
 * verdict, so nothing here needs one; `verdictText` below is where the runner's own failure lines
 * live, and it is never the haystack for a condition.
 */
function observedOutput(result) {
    // The normalized text, and not the raw streams: the condition usually mirrors an
    // `expect_output_contains`, which is read against `normalized.output`, so both have to compare
    // with one run through the same normalizers (CRLF, ANSI) or a normalizer would move one and not
    // the other.
    return `${result.normalized.stdout}\n${result.normalized.stderr}`
}

/**
 * Whether a `prints` condition has arrived: the line the mark says this entry prints when the wall
 * moves is in `output` -- the case's own output, from `observedOutput`, which is the only thing a
 * condition may be read against.
 */
function endConditionHasArrived(test, output) {
    const condition = test.__knownFailEndsWhen
    if (!condition || condition.kind !== 'prints') return false
    return output.includes(condition.text)
}

/**
 * The `passes` shape, taken once every case has settled: it is a reading of another case's verdict,
 * and the case a mark waits on may be the last one to finish, so it cannot be taken where the mark
 * fires. A mark whose ending has arrived moves out of the excused reds and into the failing ones --
 * strict xfail -- and the block printed here is what re-reads the `[XFAIL]` line it printed above.
 */
function settlePassesConditions(entries, completed, summary) {
    const ended = []
    for (const entry of entries) {
        const condition = entry.test.__knownFailEndsWhen
        if (!entry.test.__knownFail || !condition || condition.kind !== 'passes') continue
        if (!entry.outcome || entry.outcome.status !== 'xfail') continue
        const other = completed.get(condition.text)
        if (!other || other.status !== 'pass') continue
        const line =
            `known failure has ended: this entry waits on \`${condition.text}\`, which passed` +
            ' -- delete the mark'
        entry.outcome.status = 'fail'
        if (entry.outcome.result) entry.outcome.result.status = 'fail'
        entry.outcome.failures = [...(entry.outcome.failures || []), line]
        summary.xfail--
        summary.fail++
        summary.knownRows = summary.knownRows.filter((row) => row.name !== listedName(entry.plan, entry.test))
        ended.push(`${COLORS.cyan}[KNOWN]${COLORS.reset} ${line}`)
    }
    for (const line of ended) console.log(line)
    return ended
}

/**
 * A marked case must say who owns it and where it is stuck. The mark, the owner and the reason may
 * all be inherited from `[defaults]`: a plan whose default marks one back end and names the owner
 * does not have to repeat itself on each entry that changes only which back end fails.
 */
function checkKnownFailMark(planPath, entry, inherited, where) {
    const mark = entry.known_fail !== undefined ? entry.known_fail : inherited && inherited.known_fail
    if (!knownFailIsMarked(mark)) return
    const owner = entry.known_fail_owner !== undefined ? entry.known_fail_owner : inherited && inherited.known_fail_owner
    const reason = entry.known_fail_reason !== undefined ? entry.known_fail_reason : inherited && inherited.known_fail_reason
    if (typeof owner !== 'string' || owner.trim().length === 0) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail needs known_fail_owner: the mark says this ` +
                `case is red, and only an owner says who ends that -- name the lane or the person`,
        )
    }
    if (OWNER_PLACEHOLDERS.has(owner.trim().toLowerCase())) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail_owner is ${JSON.stringify(owner)}, which names ` +
                `nobody: name the lane or the person who makes this case green`,
        )
    }
    if (typeof reason !== 'string' || reason.trim().length === 0) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail needs known_fail_reason: one sentence saying ` +
                `where the case is stuck -- the refusal in backticks, the file#Symbol that refuses, or ` +
                `the page that decides it`,
        )
    }
    const stated = reason.trim()
    if (REASON_PLACEHOLDERS.has(stated.toLowerCase()) || /^(tbd|todo|fixme|待修|待定|待补|暂不支持)\b/i.test(stated)) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail_reason is ${JSON.stringify(reason)}, which says ` +
                `nothing about the wall: name what refuses, not that the case is red`,
        )
    }
    if (!WALL_SPELLINGS.some((pattern) => pattern.test(stated))) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail_reason names no wall: quote the refusal in ` +
                `backticks, name the file#Symbol that refuses, or name the page that decides it`,
        )
    }
    // The sentence the case is expected to print today, quoted from a run. Without it there is
    // nothing to compare a failure against, so `executePlans` takes every failure as the expected
    // one: a different defect can wear the mark's name and no reader can tell (a page
    // measured two entries printing `exported`, exiting 0 and writing nothing while their marks
    // reported a known failure). It is read off the entry itself -- `executePlans` compares against
    // `test.known_fail_msg` and never against a `[defaults]` value -- so a plan cannot supply it
    // once for the whole file.
    //
    // The mark may instead state the reading that ends it, and the rule chose
    // that shape for a case whose target is a number and whose wall is being built: a sentence
    // quoted from a refused run follows the implementation, so a pin on it has to be re-typed every
    // time the wall moves -- which records the wall's progress as the case's debt and reads, in the
    // plan, as "this case fails for this reason" long after that reason moved.
    //
    // What that field may hold is now exactly two things, because a promise nobody evaluates is a
    // mark nobody re-reads -- the rule 18:xx (see internal/questions/
    // 2026-11-24-deleting-known-fail-ends-when-would-unmake-11-marks.cn.md):
    //
    //   known_fail_ends_when = { prints = "the line this entry prints when the wall moves" }
    //   known_fail_ends_when = { passes = "another case, whose passing means this one is over" }
    //
    // `prints` is read off the entry's own output on every run; `passes` off another case's verdict
    // in the same run. Either way the run *evaluates* it: a mark whose ending has arrived fails the
    // run and says to delete the mark (strict xfail). Prose -- a sentence with no key -- is refused
    // here and by test/tools/check-plan-fields.mjs, because it cannot be evaluated.
    const expected = entry.known_fail_msg
    const ends = knownFailEndCondition(entry.known_fail_ends_when)
    if (entry.known_fail_ends_when !== undefined && ends === null) {
        throw new PlanSchemaError(
            `${planLabel(planPath)}: ${where}: known_fail_ends_when must be exactly one of ` +
                `{ prints = "..." } or { passes = "..." }: the condition has to be something a run ` +
                `can evaluate -- the line this entry prints when the wall moves, or a case whose ` +
                `passing means this one is over -- because a sentence nothing reads is a mark ` +
                `nobody re-reads`,
        )
    }
    if (typeof expected !== 'string' || expected.trim().length === 0) {
        if (ends === null) {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: known_fail needs known_fail_msg or known_fail_ends_when: ` +
                    `quote either the sentence this case prints today, or the reading that ends the mark -- a ` +
                    `mark that names neither takes any failure as the expected one, so another defect wears this ` +
                    `mark's name and nothing ever re-reads it`,
            )
        }
    }
}

/**
 * Refuse a plan the runner does not fully understand before any of its tests run. Every key of
 * the plan, of `[defaults]`, of a `[[tests]]` entry and of a `[[sweeps]]` entry is checked, so a
 * misspelled assertion cannot silently assert nothing. There is deliberately no way to turn
 * this off from a plan file (or from the environment): a plan that the runner only partly
 * understands is not evidence.
 */
function validatePlanSchema(doc, planPath) {
    checkFields(planPath, doc, PLAN_FIELDS, 'plan')
    // The plan's own `tags` are read by a sweep (`sourceDoc.tags`), so they are a list for the same
    // reason the assertion lists below are.
    checkListFields(planPath, doc, 'plan', ['tags'])
    if (doc.defaults !== undefined) {
        if (doc.defaults === null || typeof doc.defaults !== 'object' || Array.isArray(doc.defaults)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: [defaults] must be a table`)
        }
        checkFields(planPath, doc.defaults, TEST_FIELDS, '[defaults]')
    }
    for (const [index, test] of (doc.tests || []).entries()) {
        if (test === null || typeof test !== 'object' || Array.isArray(test)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: test #${index + 1} is not a table`)
        }
        const where = typeof test.name === 'string' && test.name.length > 0
            ? `test '${test.name}'`
            : `test #${index + 1}`
        checkFields(planPath, test, TEST_FIELDS, where)
    }
    for (const [index, sweep] of (doc.sweeps || []).entries()) {
        if (sweep === null || typeof sweep !== 'object' || Array.isArray(sweep)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: [[sweeps]] entry #${index + 1} is not a table`)
        }
        const where = typeof sweep.name === 'string' && sweep.name.length > 0
            ? `[[sweeps]] entry '${sweep.name}'`
            : `[[sweeps]] entry #${index + 1}`
        checkFields(planPath, sweep, SWEEP_FIELDS, where)
        checkListFields(planPath, sweep, where, SWEEP_LIST_FIELDS)
    }
    // The fields this change implemented get a type check too: a plan that sets one of them to a
    // number must fail here, where the file and the test are named, not later inside an assertion.
    const entries = [
        ['[defaults]', doc.defaults],
        ...(doc.tests || []).map((test, index) => [
            typeof test.name === 'string' && test.name.length > 0 ? `test '${test.name}'` : `test #${index + 1}`,
            test,
        ]),
    ]
    for (const [where, entry] of entries) {
        if (!entry) continue
        // The list-shaped fields first: a wrong shape here changes what every other assertion in the
        // entry means, so it is named before the assertions are read at all.
        checkListFields(planPath, entry, where, LIST_FIELDS)
        // The one diagnostic pin that is not a scalar by accident of history but by the shape of the
        // claim: the file a diagnostic was reported in. A value that names no file is refused here,
        // and so is the entry that pairs it with `expect_no_diagnostics` -- the absence of every
        // diagnostic and the presence of one in a named file are two answers to one question, and the
        // flag would swallow the pin rather than defeat it.
        if (entry.expect_diagnostic_file !== undefined) {
            checkDiagnosticFileField(planPath, entry, where)
        }
        if (entry.expect_no_diagnostics === true && entry.expect_diagnostic_file !== undefined) {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: expect_no_diagnostics with expect_diagnostic_file: ` +
                    `an entry cannot both require a diagnostic in a named file and require no diagnostic at all`,
            )
        }
        // A marked case has to carry its owner and its wall, and this is the only place that can
        // refuse it: the runner reads a plan once, here, and a promise checked later than this is
        // a promise kept by whoever remembered to look.
        checkKnownFailMark(planPath, entry, where === '[defaults]' ? null : doc.defaults, where)
        checkFieldType(planPath, entry, 'expect_stdout_exact', where, 'string')
        checkFieldType(planPath, entry, 'expect_stdout', where, 'string')
        // A pattern, or a list of them, every one of which has to find a line. The shape is typed
        // here; whether the pattern compiles and whether it contradicts another stdout assertion is
        // the load-time plan check (`assertionProblems`), which names `plan:line`.
        if (entry.expect_stdout_matches !== undefined) {
            const value = entry.expect_stdout_matches
            const list = Array.isArray(value) ? value : [value]
            if (typeof value !== 'string' && !Array.isArray(value)) {
                throw new PlanSchemaError(
                    `${planLabel(planPath)}: ${where}: expect_stdout_matches must be a string or a list of strings, got ${JSON.stringify(value)}`
                )
            }
            const bad = list.find((item) => typeof item !== 'string')
            if (bad !== undefined) {
                throw new PlanSchemaError(
                    `${planLabel(planPath)}: ${where}: expect_stdout_matches holds ${JSON.stringify(bad)}, which is not a string`
                )
            }
        }
        checkFieldType(planPath, entry, 'silenced_warnings', where, 'strings')
        // A refusal is a value, and a plan names it field by field: the cause the transform's table
        // spells and the place the refusal names, never the sentence the line prints
        // (`test/tools/reading-refusal.mjs`). The fields are checked here, with the plan and the
        // test named, so a misspelled field cannot compare nothing and pass.
        if (entry.expect_refusal !== undefined) {
            checkFieldType(planPath, entry, 'expect_refusal', where, 'strings')
            for (const spec of entry.expect_refusal) {
                try {
                    parseRefusalSpec(spec)
                } catch (error) {
                    throw new PlanSchemaError(`${planLabel(planPath)}: ${where}: ${error.message}`)
                }
            }
        }
        checkFieldType(planPath, entry, 'perf_budget_platform', where, 'string')
        checkFieldType(planPath, entry, 'cuda_devices', where, 'boolean')
        checkFieldType(planPath, entry, 'cuda_free_device', where, 'boolean')
        checkFieldType(planPath, entry, 'cpu_budget_ms', where, 'number')
        // A CPU budget is the test's cost bound and the timeout is only its liveness bound, so the
        // two are declared together: a test that states a budget and leaves the timeout at the
        // runner's 10 s default would be killed long before its own work could cross it, and the
        // plan would be asserting a bound nothing reaches.
        if (typeof entry.cpu_budget_ms === 'number' && typeof entry.timeout_ms !== 'number') {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: cpu_budget_ms needs timeout_ms: the budget is the ` +
                    `cost bound and the timeout is the liveness bound (a hang, not a slow run)`
            )
        }
        // `expect_signal` names the signal that must have killed the test's process. The shape is
        // checked here -- a name, or a number this platform knows, and nothing else -- so a plan that
        // writes `true` ("it dies somehow") fails with its file, its test and its key named instead
        // of passing while asserting nothing. It and `expect_exit` are one question with one answer,
        // so a table that writes both is a contradiction and not a preference.
        if (entry.expect_signal !== undefined) {
            expectedSignalNumber(entry.expect_signal, `${planLabel(planPath)}: ${where}: `)
            if (entry.expect_exit !== undefined) {
                throw new PlanSchemaError(
                    `${planLabel(planPath)}: ${where}: expect_signal with expect_exit: a process ` +
                        `ends one way, and a killed one has no exit code to compare`
                )
            }
        }
        // The exit code is expected in one of three writings, and the third exists because a
        // `verify_script` may already own the question: its payload carries `exit_code` and
        // `signal`, and an entry that states both a number here and an assertion there says one
        // thing twice with two chances to disagree -- which is how twelve cases asserting "exit 1 or
        // exit 0, never a signal" read as red the day the number here started being enforced.
        // `"script"` says the script decides. No other word is a second form: a range, an "either",
        // a "whatever" is a script's to say, not a field's to spell.
        if (typeof entry.expect_exit === 'string' && entry.expect_exit !== 'script') {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: expect_exit: the only word it takes is "script" ` +
                    `(then the verify script judges the exit code and the signal); an expected ` +
                    `number is written as one`
            )
        }
        if (entry.expect_exit === 'script' && !(typeof entry.verify_script === 'string' && entry.verify_script.length > 0)) {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: expect_exit = "script" without verify_script: ` +
                    `that asks a script to judge a code it never sees`
            )
        }
    }
}

function camelHome(build) {
    return path.join(REPO_ROOT, 'out', BUILDS[build].dir)
}

// The install a run measures: `--camel <path>` names it, else `out/<build>`. One install for the
// whole run -- `${camel}`, what `preflightBuild` checks, and the root every test child reads as
// `CAMEL_HOME` are all this -- and the environment never chooses it: `CAMEL_HOME` is the install
// location's convention for the binary and the SDK, not a switch over what a run measures.
function camelBinary(options) {
    return options.camel || toolExecutable('camel', options.build)
}

function camelInstall(options) {
    return path.dirname(path.dirname(camelBinary(options)))
}

// Whether this build's `std::jit` compiles a function to native code.
//
// The JIT has one code generator, for x86-64 (`cmake/Options.cmake`: ENABLE_FASTVM_JIT is on for
// that processor alone), so every other build -- arm64 on any platform, and a build that switched
// the JIT off by hand -- runs `std::jit` on the bytecode interpreter of `std::fvm` and says so on
// every run of a pipeline that names the pass (`RuntimeDiag::JitWithoutCodeGenerator`, one warning
// line on stderr and one diagnostic in the report). That is a fact about the build, not about the
// program a test runs, and 240 entries compare a `.jit` entry against its `.nvm` twin on stderr and
// diagnostics while others assert that a run says nothing. The notice is therefore silenced for
// every entry by default (`silenced_warnings`, see runOneTest) and the entries that test the notice
// keep it.
//
// The reading is the one `test/tools/have-native-jit.mjs` takes, so a test and a runner cannot
// disagree about which build they are in. `CAMEL_TEST_NO_JIT=1|0` answers for the harness, which is
// how the no-JIT behaviour is exercised on a build that does have a code generator.
let noCodeGeneratorCache = null

function buildHasNoCodeGenerator(options) {
    if (noCodeGeneratorCache !== null) return noCodeGeneratorCache
    const forced = process.env.CAMEL_TEST_NO_JIT
    if (forced !== undefined && forced !== '') {
        noCodeGeneratorCache = forced !== '0'
        return noCodeGeneratorCache
    }
    const probe = spawnSync(camelBinary(options), ['-v', path.join(REPO_ROOT, 'hello.cml'), 'std::jit'], {
        cwd: REPO_ROOT,
        env: { ...process.env, CAMEL_HOME: camelInstall(options) },
        encoding: 'utf8',
        timeout: 60000,
    })
    const text = `${probe.stdout || ''}${probe.stderr || ''}`
    noCodeGeneratorCache = text.includes('no native code generator')
    return noCodeGeneratorCache
}

function toolExecutable(tool, build) {
    const ext = IS_WINDOWS ? '.exe' : ''
    if (tool === 'node') return process.execPath
    return path.join(camelHome(build), 'bin', `${tool}${ext}`)
}
const COLORS = {
    green: '\x1b[32m',
    red: '\x1b[31m',
    cyan: '\x1b[36m',
    yellow: '\x1b[33m',
    reset: '\x1b[0m',
}

// Test scratch. Every run gets its own root, tmp/test/run-<pid>, and every test gets a
// directory under it (`<run>/<plan>/<test>`), exposed as `${test_tmp}` and as
// CAMEL_TEST_TMPDIR in the child environment. A test that writes a file writes there, so a
// file left by another test, or by an earlier run, cannot be read as this run's output (the
// failure that motivated this: a stale model at the path a refused export names made "a
// refused export must not write a model" fail). A program that still names its own output path
// takes it from CAMEL_TEST_TMPDIR, so the file lands under the per-test directory; only a path
// that a refusal test names without ever writing it stays literal, and the pass's `out`
// parameter isolates the one refusal case that does write.
//
// The per-test directory is removed after the test. The run removes its own root when the run
// succeeded and keeps it (and prints the path) when a test failed, so the files are there to
// read. It never removes another run's root: two runners can run at the same time.
const TEST_SCRATCH_ROOT = runnerOutputPath(SCRATCH_DIR, REPO_ROOT)
const TEST_SCRATCH_RUN = path.join(TEST_SCRATCH_ROOT, `run-${process.pid}`)

function timestamp() {
    const now = new Date()
    const pad = (n) => String(n).padStart(2, '0')
    return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
}

function ensureDir(dir) {
    fs.mkdirSync(dir, { recursive: true })
}

// CAMEL_TEST_RUN_DIRS_KEEP=<n> lowers the retention bound for one run. The default and its reason
// live beside the policy (test/tools/runner-output.mjs); this is the one number a check sets small
// to watch the bound hold across two real runs (docs/test-plans.md, "Runner output").
function resultsKeep() {
    const raw = process.env.CAMEL_TEST_RUN_DIRS_KEEP
    if (raw === undefined || raw === '') return RUN_DIRS_KEPT
    const parsed = Number(raw)
    if (!Number.isInteger(parsed) || parsed < 0) {
        throw new Error(`CAMEL_TEST_RUN_DIRS_KEEP needs a non-negative integer, got '${raw}'`)
    }
    return parsed
}

/** One git command's trimmed stdout, or null when git is unavailable or refuses. */
function gitOutput(args) {
    const proc = spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true, timeout: 30000 })
    if (proc.error || proc.status !== 0) return null
    return proc.stdout.trim()
}

/**
 * What this run was, written before its first test: the revision, the build and tier, and every
 * plan it selected. This is the manifest that replaces the copy of each plan and of each case the
 * runner used to write into every test's status directory -- a copy nothing ever read back, made
 * once per test, so a 5,667-test gate rewrote the same plan file 5,667 times (see writeLogFiles).
 * With it, a run directory says which plan, at which revision, produced it without carrying a copy
 * of the checkout's own files.
 */
function writeRunManifest(logDir, options, plans) {
    const revision = gitOutput(['rev-parse', 'HEAD'])
    const status = gitOutput(['status', '--porcelain', '--untracked-files=no'])
    fs.writeFileSync(
        path.join(logDir, 'run.json'),
        JSON.stringify(
            {
                started_at: new Date().toISOString(),
                build: options.build,
                tier: options.tier,
                jobs: options.jobs,
                update_golden: options.updateGolden,
                results_root: RESULTS_ROOT,
                // null, not false: git unavailable means the provenance is unknown, and a manifest
                // that claimed the tree was clean would be worse than one that shrugs.
                revision,
                dirty: status === null ? null : status.length > 0,
                targets: options.targets.map((target) => planLabel(target)),
                plans: plans.map((plan) => planLabel(plan.__planPath)),
            },
            null,
            2
        )
    )
}

// CAMEL_TEST_TIMEOUT_SCALE=<n> multiplies every test and verification timeout. For running
// the plans under a diagnostic mode that slows every program down (collection stress,
// safepoint poisoning, a sanitizer build), where the timeouts would otherwise decide.
const TIMEOUT_SCALE = (() => {
    const scale = Number(process.env.CAMEL_TEST_TIMEOUT_SCALE)
    return Number.isFinite(scale) && scale > 0 ? scale : 1
})()

// A Debug build runs the same program through unoptimised C++: a loop of a million rounds takes
// tens of seconds where the Release build takes under one. The debug layer multiplies a test's
// own timeout by this, so a plan's budget still means "this is slow", not "this is Debug".
const DEBUG_BUILD_TIMEOUT_SCALE = 4

// How much room a test must leave under its `timeout_ms` to count as a real bound: the run fails
// when a passing test used more than a quarter of it **of its own CPU time**. The quantity is the
// work's, not the host's, and deliberately so: judged on the wall clock, this rule failed
// the plan runner's own self-test at random on the shared host -- 1 of 12 runs at a 60 s bound,
// and twice in two agents' gates -- while every run inside it was green,
// because a passing test's wall clock on a busy host is the neighbours' scheduling. Four machines'
// worth of headroom is what keeps a bound a bound. A test that needs more time states it in a
// comment next to the number (docs/test-plans.md). A test that declares `cpu_budget_ms` is judged
// on that budget instead and does not read the wall clock as a bound at all: its timeout is a
// liveness bound (see the rule quoted where the reading is judged), so this rule has nothing to
// say about it.
const TIMEOUT_MARGIN = 4

// What a timed-out case's second attempt is given: twice the bound the first attempt had. The
// factor is on the *bound* and not on the case, so the double is a statement about the number that
// decided the first verdict -- a floor would be a second bound the plan never wrote, and an
// absolute minimum would make the same plan read differently on a fast host and a slow one.
const RERUN_TIMEOUT_FACTOR = 2

// Where a second attempt's record goes, under the run's own directory: the first attempt's logs
// stay the first attempt's (`rerun/first/`), and a case that ends green on the second attempt is
// not a case whose timed-out output was overwritten. Both are under `rerun/`, so a reader of a run
// directory can tell the two attempts apart from the ordinary `pass/` and `fail/` trees.
const RERUN_FIRST_DIR = 'rerun/first'
const RERUN_SECOND_DIR = 'rerun/second'

// What the run says where that margin cannot be read at all, and the reason a test's record carries
// in that case. It is a sentence about the host, not about the test, so it never fails one: a host
// with no CPU figure is asked for no margin rather than for a wall-clock one, which would make the
// bound a property of whoever else is running. The test's timeout is untouched by this: runProcess
// kills the child at it either way, so a test that really runs too long still fails.
export const NO_CPU_TIME_SKIP = 'skipped: this host reports no CPU time'

// What a case's record says about the CPU time it cost. A record carries a figure or a sentence
// saying why there is none; it never carries 0, which a reader would take as "this case is free"
// while the truth is "nobody measured it". The figure comes from the wrapper the runner already
// starts for the test (`test/tools/child-cpu.mjs`, fd 3): the kernel's `cutime` + `cstime` for
// exactly this test's process tree, so the wrapper's own time is not in it. No second process is
// started to read it, on any platform: where the kernel reports no such figure the record says so
// and the runner does nothing else about it.
export const CPU_READING_WRAPPED =
    'process-tree CPU time (user + system) the kernel charged this test, read by test/tools/child-cpu.mjs'
export const CPU_READING_ABSENT =
    'absent: this platform reports no per-process CPU time (there is no /proc/self/stat), and the ' +
    'runner starts no process to invent one'

/**
 * One case's CPU cost, as its record carries it. Three shapes, kept apart so that a later reader
 * can tell "this case was cheap" from "this platform cannot say" from "this case did not run":
 *
 *   * a figure -- the case's own CPU time, with where it came from;
 *   * no figure although the host could report one -- the wrapper said nothing, and the reason it
 *     gave is quoted (`cpuReason`); on this shape the timeout-margin rule fails the case, so the
 *     record and the verdict agree;
 *   * no figure and no way to get one -- `CPU_READING_ABSENT`, which names the platform's gap
 *     rather than the case's cost.
 */
export function cpuReadingRecord({ cpuMs, cpuReason, canReadCpu }) {
    if (Number.isFinite(cpuMs)) return { cpuMs, cpuReading: CPU_READING_WRAPPED }
    if (!canReadCpu) return { cpuMs: null, cpuReading: CPU_READING_ABSENT }
    return { cpuMs: null, cpuReading: `absent: ${cpuReason || 'the wrapper reported nothing'}` }
}

// How many tests this run left unjudged that way, for the line it prints at the end.
let marginSkippedTests = 0

// How many `cpu_budget_ms` bounds this run never judged, for the same reason and by the same rule:
// a host that reports no CPU figure is not a test that crossed its budget. Counted and printed
// apart from `pass`, so "not read" is never read as "held".
let budgetSkippedTests = 0

/**
 * The timeout-margin rule's verdict for one test that passed everything else. `canReadCpu` is
 * whether this host reports a process tree's CPU time at all (CAN_READ_CHILD_CPU).
 *
 *   * a figure to read: the test fails when its own CPU time used more than a quarter of its
 *     timeout, and passes otherwise;
 *   * no figure although the host can report one (the wrapper said nothing): the test fails --
 *     a margin that cannot be read is not a margin, and it is not a pass either;
 *   * a host that can report no figure: the margin is skipped, with NO_CPU_TIME_SKIP as its
 *     reason, and this rule decides nothing.
 *
 * It never decides whether a test ran too long; that is the runner's own kill at `timeout_ms`,
 * whose verdict is a timeout on any platform.
 */
export function timeoutMarginVerdict({ cpuMs, cpuReason, wallMs, timeoutMs, canReadCpu }) {
    if (cpuMs !== null) {
        if (cpuMs * TIMEOUT_MARGIN > timeoutMs) {
            return {
                failure:
                    `used ${cpuMs.toFixed(0)} ms of CPU against a ${timeoutMs.toFixed(0)} ms timeout, ` +
                    `below the ${TIMEOUT_MARGIN}x margin: the bound is the work's own time, not a bound ` +
                    `(raise it with a stated reason, or make the test do less). The wall clock read ` +
                    `${wallMs.toFixed(0)} ms, which on a shared host is the neighbours'`,
                skipped: null,
            }
        }
        return { failure: null, skipped: null }
    }
    if (!canReadCpu) return { failure: null, skipped: NO_CPU_TIME_SKIP }
    return {
        failure:
            `no CPU reading to judge the margin of its ${timeoutMs.toFixed(0)} ms timeout by: ` +
            `${cpuReason || 'the wrapper reported nothing'}. A bound whose margin nobody ` +
            `can read is not a bound, and it is not a pass either`,
        skipped: null,
    }
}

/**
 * The three numbers that answer "how far from the line did this test run": the CPU time the kernel
 * charged its own process tree, the line its cost verdict was read against, and how many times that
 * reading fits under that line. They are recorded for **every** test, not only for the ones that
 * declare a bound: the timeout-margin rule above is judged on this same figure, and a passing test
 * used to be recorded as `cpu_ms: null` -- "ran, did not go red, and nobody can say by how much",
 * which is the third shape of a green nobody looked at (beside the green of a test that never ran
 * and the green of an assertion that asserted nothing). The reading itself costs nothing more than
 * it already did: the wrapper runs for every test on a host whose kernel reports CPU time, and the
 * figure was computed and then discarded.
 *
 * The line is **not decided here**. It is the number the verdicts below were made from, and this
 * function records it; a record that moved it to make a margin read bigger would erase the very
 * measurement it exists for.
 *
 *   * `cpu_budget_ms`, when the test declares one: the test is judged on that figure, and
 *     `timeout_ms` is only its liveness bound;
 *   * otherwise `timeout_ms / TIMEOUT_MARGIN`: the figure `timeoutMarginVerdict` compares the
 *     reading against, since that rule fails a test whose own CPU time used more than a quarter of
 *     its timeout.
 *
 * `marginX` is `line / reading`: 1.0 is the line itself, 39 is a reading with thirty-nine times the
 * room it needs, and 0.99 is one percent from red. It is null when there is no reading (a host
 * without `/proc/self/stat`, or a wrapper that reported nothing) and when the reading is 0 ms --
 * below the kernel's 10 ms tick, so the ratio is not a number. The line and the reading are
 * recorded in both cases, so the room is still there to be read even when it cannot be divided.
 */
function costRecord({ cpuMs, timeoutMs, cpuBudgetMs }) {
    const basis = cpuBudgetMs === null ? 'timeout_margin' : 'cpu_budget_ms'
    const lineMs = cpuBudgetMs === null ? timeoutMs / TIMEOUT_MARGIN : cpuBudgetMs
    const marginX = Number.isFinite(cpuMs) && cpuMs > 0
        ? Math.round((lineMs / cpuMs) * 1000) / 1000
        : null
    return { cpuLimitMs: Math.round(lineMs * 1000) / 1000, cpuMarginX: marginX, cpuMarginBasis: basis }
}

/**
 * A counting semaphore with FIFO order. A normal test holds one slot; a test marked
 * `parallel = false` asks for every slot, so it starts only when no other test is running and
 * no other test starts while it runs. FIFO keeps a queued serial test ahead of tests queued
 * after it, and no holder ever waits for a slot another holder needs, so the queue cannot
 * deadlock. A test waiting for another test's result (`priority`) goes to the front: it must
 * follow its reference, not the whole run.
 */
class Slots {
    constructor(count) {
        this.free = count
        this.queue = []
    }

    /**
     * `priority` puts a test in front of the queue. A test that waits for another test's result
     * (a comparison against a reference test, a back-end agreement check) must not queue behind
     * every other test of the run: it would run last, and the in-order printer would hold back
     * the whole run's output until then. It queues as soon as its reference is done, which is
     * after the plan's earlier tests have already taken their slots.
     */
    acquire(count, priority = false) {
        if (count <= this.free && this.queue.length === 0) {
            this.free -= count
            return Promise.resolve()
        }
        return new Promise((resolve) => {
            if (priority) this.queue.unshift({ count, resolve })
            else this.queue.push({ count, resolve })
        })
    }

    release(count) {
        this.free += count
        while (this.queue.length > 0 && this.queue[0].count <= this.free) {
            const next = this.queue.shift()
            this.free -= next.count
            next.resolve()
        }
    }
}

/**
 * A test is run alone (`parallel = false`, on the test or on its plan) when it shares something
 * outside its own scratch directory: a fixed path another test writes, a compiler cache, the
 * wall clock. A benchmark is run alone as well unless it says otherwise: its numbers mean
 * nothing next to a machine busy with other tests. A test may say `parallel = true` to opt in.
 */
function isSerial(plan, test) {
    if (test.parallel === true) return false
    if (test.parallel === false || plan.parallel === false) return true
    return test.kind === 'benchmark'
}

/**
 * Run one child process and collect its output, as `spawnSync` did, but without blocking the
 * runner: the other tests run while this one waits. The timeout kills the whole process tree
 * (a test may have started a compiler or an interpreter of its own).
 *
 * `cpuReport` opens a fourth pipe, on which test/tools/child-cpu.mjs writes the CPU time the
 * kernel charged the command's process tree. The runner cannot read that itself: the one reading
 * Node has of a child's CPU is the `cutime` + `cstime` delta of /proc/self/stat, which sums every
 * child this process reaped, and the runner reaps every test's child from one process.
 */
function runProcess(command, args, { cwd, env, timeoutMs, marks, cpuReport = false }) {
    return new Promise((resolve) => {
        const started = performance.now()
        // A test must not write into the tree it is testing. A Python harness that imports a module
        // beside it (`test/cases/perf/reduce_layout.py` imports `step_budget.py`) leaves a
        // `__pycache__` behind, and one left in a checkout makes the case ledger's answer depend on
        // which test ran first -- the ledger reads the filesystem, so it sees a cache written this
        // run. The interpreter is told not to write bytecode; a test that needs its own value sets
        // `PYTHONDONTWRITEBYTECODE` in its `envs`, and that wins because those are already in `env`.
        const childEnv = { ...(env || process.env) }
        if (!('PYTHONDONTWRITEBYTECODE' in childEnv)) childEnv.PYTHONDONTWRITEBYTECODE = '1'
        let stdout = ''
        let stderr = ''
        let cpuReportText = ''
        let timedOut = false
        let settled = false
        let timer = null
        let child = null
        const finish = (status, signal, error) => {
            if (settled) return
            settled = true
            // The child is gone at this point (on error, never existed). `spawnedAt` and
            // `exitedAt` are what lets the scheduler separate "a child is running" from "the
            // slot is held while the runner does its own work".
            if (marks) marks.exitedAt = performance.now()
            if (timer) clearTimeout(timer)
            resolve({
                status: typeof status === 'number' ? status : null,
                signal: signal ?? null,
                error: error || null,
                timedOut,
                wallMs: performance.now() - started,
                stdout,
                stderr,
                cpuReport: cpuReportText,
            })
        }
        try {
            child = spawn(command, args, {
                cwd,
                env: childEnv,
                stdio: cpuReport ? ['ignore', 'pipe', 'pipe', 'pipe'] : ['ignore', 'pipe', 'pipe'],
                windowsHide: true,
                // On POSIX a process group lets the timeout take the whole tree down; on Windows
                // `taskkill /T` does that.
                detached: !IS_WINDOWS,
            })
            // `spawn` returns only after the operating system created the child (fork/exec or
            // CreateProcess), so the interval before this mark is runner work, not test work.
            if (marks) {
                marks.spawnedAt = performance.now()
                marks.spawnMs = marks.spawnedAt - started
            }
        } catch (error) {
            finish(null, null, error)
            return
        }
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.stderr.on('data', (chunk) => {
            stderr += chunk
        })
        if (cpuReport) {
            const report = child.stdio[3]
            report.setEncoding('utf8')
            report.on('data', (chunk) => {
                cpuReportText += chunk
            })
        }
        child.on('error', (error) => finish(null, null, error))
        child.on('close', (status, signal) => finish(status, signal, null))
        if (timeoutMs > 0) {
            timer = setTimeout(() => {
                timedOut = true
                killProcessTree(child)
            }, timeoutMs)
        }
    })
}

function walkPlans(entryPath, files = []) {
    const stats = fs.statSync(entryPath)
    if (stats.isFile()) {
        if (entryPath.endsWith('.plan.toml')) files.push(entryPath)
        return files
    }
    for (const name of fs.readdirSync(entryPath)) {
        walkPlans(path.join(entryPath, name), files)
    }
    return files
}

const DEFAULT_JOBS = (() => {
    const count = typeof os.availableParallelism === 'function' ? os.availableParallelism() : os.cpus().length
    return Math.max(1, count || 1)
})()

// The layers are `fast`, `slow`, `milestone` and `all`: one name per destination, so "the slow
// layer" is only ever the release gate's slow layer (the plans named in SLOW_PLANS, run in full)
// and never the unsampled copies of a sweep. `milestone` is what only runs at a milestone: the
// unsampled copies of a sweep and the plans the policy holds back (benchmarks, soaks, fuzzing,
// PyTorch comparisons, progen). `all` is everything but `debug`.
//
// `debug` is not a layer of an ordinary run: it selects the plans that exercise views, data
// blocks and the executor on a Debug build, where the view-escape assertion exists at all.
const TIERS = new Set(['fast', 'slow', 'milestone', 'all', 'debug'])

function positiveInteger(value, what) {
    const parsed = Number(value)
    if (!Number.isInteger(parsed) || parsed < 1) {
        throw new Error(`${what} needs a positive integer, got '${value}'`)
    }
    return parsed
}

/**
 * Every option this parser accepts, in one list, because the refusal for an unknown one names it.
 *
 * The parser below is the truth and this is its index: a word added there belongs here in the same
 * commit. The list exists because "I do not know that word" is only half an answer -- the reader
 * still has to open this file to learn what the tree *does* know, and the gap between those two
 * readings is exactly "the caller is newer than the checkout it was pointed at", which is the one
 * thing the run cannot guess. `targets` is listed because it is the argv position that is not an
 * option: a plan file, a plan directory, or a short name (`smoke`, `feat`, `para`), and it is the
 * only word that may omit the leading dashes (see the refusal below).
 */
const KNOWN_ARGV = [
    'targets (a plan file, a plan directory, or a short name such as smoke)',
    '--tier fast|slow|milestone|all|debug',
    '--build release|debug|profile',
    '--camel <install>',
    '--jobs <n>',
    '--update-golden',
    '--compile-only',
    '--list-known',
    '--list-cases',
    '--print-time-sensitive',
    '--selection-cases[=<file>]',
    '--selection-layer <plans>',
    '--selection-empty',
]

/**
 * A refusal about argv rather than about a run, marked so `main` prints it as the one line it is.
 *
 * An argv error is the caller's sentence, not a crash: it arrives before anything is measured, it
 * has one cause, and a stack trace buries the only fact worth reading.
 */
function argvRefusal(message) {
    return Object.assign(new Error(message), { argvRefusal: true })
}

function parseArgs(argv) {
    const envBuild = process.env.CAMEL_TEST_BUILD
    if (envBuild !== undefined && !Object.hasOwn(BUILDS, envBuild)) {
        throw new Error(`CAMEL_TEST_BUILD must be one of ${Object.keys(BUILDS).join(', ')}, got '${envBuild}'`)
    }
    const options = {
        updateGolden: false,
        // `fast` is the default layer: the full test set is opt-in (`--tier all`, `npm run test:all`).
        tier: 'fast',
        // Which build the tests run against. `release` is out/latest; `debug` is out/debug.
        build: envBuild || 'release',
        buildExplicit: envBuild !== undefined,
        // The install this run measures, named by `--camel <path>`. With none the build above
        // decides (`out/<build>`); with one, it is this binary, its root is every child's
        // `CAMEL_HOME`, and `--build` only still selects the layer the run addresses.
        camel: null,
        // `--jobs` defaults to the processor count: one test per core, no more. `--jobs 1` gives
        // exactly the historical serial execution, which is how the runner's own self-test
        // compares parallel output with serial output.
        jobs: DEFAULT_JOBS,
        targets: [],
        // Plan files that `--changed` selected through a `layer: true` rule: they stay named
        // targets but follow `--tier`, so a plan whose slow cases must not run from an ordinary
        // change runs only its fast layer. `--run-changed` produces this; it is not a user knob.
        layeredPlans: new Set(),
        // `--changed` measured an empty selection: no target travels with the run because there is
        // no plan to run, and an absent target list must not become the whole plan tree here -- that
        // widening is what let `changed: 0 target(s)` run 336 plan files and print a green summary.
        // Only `test/runner.mjs` passes this flag, and it is refused beside a target.
        selectionEmpty: false,
        // `--list-known` answers one question in one line per test, `{name, verdict, reading}`: which
        // named case came out which way. The question "is the known set the same set" was answered
        // only by the `[XFAIL]` lines, so a before/after comparison had to be picked out of a full
        // transcript by hand -- and two runs whose known *counts* agreed were read as "nothing
        // moved" while one case had changed the way it fails. It is additive: the run prints
        // everything it prints without the flag, and the rows come last. The wrapper that carries
        // them back (`internal/tools/camel-remote.ps1 -ListKnown`) brackets them with two markers,
        // which is what removes the need for a mode that suppresses the narration.
        // `options.listKnownRows` is where `printResult` collects the verdicts while it prints them.
        listKnown: false,
        // `--list-cases` answers "which cases would this selection run" without starting one: the
        // same plan loading, `[[sweeps]]` expansion and tier filter a run uses, printed as one JSON
        // row per case, and no install is consulted because nothing is executed. It is the reading
        // `--changed` hands to `scripts/selection-cases.mjs` and the one a selection record is
        // compared against; a second implementation of "which cases are in this selection" would
        // agree on the day it was written and disagree the first time a plan learned a sweep.
        listCases: false,
    printTimeSensitive: false,
        // `--selection-cases <file>` runs exactly the cases a recorded selection names
        // (`{"cases": ["<plan id>/<case name>", ...]}`), instead of every case of the selected
        // plans. A case list computed from the change is only meaningful if the run executes that
        // list: handing the plans over and letting the tier expand them again is how a case got
        // selected because its whole plan was.
        selectionCases: null,
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--update-golden') {
            options.updateGolden = true
        } else if (arg === '--selection-empty') {
            options.selectionEmpty = true
        } else if (arg === '--print-time-sensitive') {
        options.printTimeSensitive = true
    } else if (arg === '--list-cases') {
            options.listCases = true
        } else if (arg === '--selection-cases' && i + 1 < argv.length) {
            options.selectionCases = path.resolve(process.cwd(), argv[++i])
        } else if (arg.startsWith('--selection-cases=')) {
            options.selectionCases = path.resolve(process.cwd(), arg.slice('--selection-cases='.length))
        } else if (arg === '--list-known') {
            options.listKnown = true
        } else if (arg === '--selection-layer' && i + 1 < argv.length) {
            for (const entry of argv[++i].split(',')) {
                if (entry.trim()) options.layeredPlans.add(path.resolve(process.cwd(), entry.trim()))
            }
        } else if (arg === '--tier' && i + 1 < argv.length) {
            const tier = argv[++i]
            if (!TIERS.has(tier)) throw new Error(`--tier must be one of ${[...TIERS].join(', ')}, got '${tier}'`)
            options.tier = tier
        } else if (arg.startsWith('--tier=')) {
            const tier = arg.slice('--tier='.length)
            if (!TIERS.has(tier)) throw new Error(`--tier must be one of ${[...TIERS].join(', ')}, got '${tier}'`)
            options.tier = tier
        } else if (arg === '--build' && i + 1 < argv.length) {
            const build = argv[++i]
            if (!Object.hasOwn(BUILDS, build)) throw new Error(`--build must be one of ${Object.keys(BUILDS).join(', ')}, got '${build}'`)
            options.build = build
            options.buildExplicit = true
        } else if (arg.startsWith('--build=')) {
            const build = arg.slice('--build='.length)
            if (!Object.hasOwn(BUILDS, build)) throw new Error(`--build must be one of ${Object.keys(BUILDS).join(', ')}, got '${build}'`)
            options.build = build
            options.buildExplicit = true
        } else if (arg === '--camel') {
            if (i + 1 >= argv.length) throw new Error('--camel needs the path of the install to measure')
            options.camel = path.resolve(process.cwd(), argv[++i])
        } else if (arg.startsWith('--camel=')) {
            const install = arg.slice('--camel='.length)
            if (install === '') throw new Error('--camel needs the path of the install to measure')
            options.camel = path.resolve(process.cwd(), install)
        } else if (arg === '--compile-only') {
            // Answers one question: which programs in the entries the fast layer does not run fail
            // to compile. It does not answer whether those tests pass -- their own criteria are
            // comparisons and budgets that judge nothing without a run -- so it reports two columns,
            // the compilation failures and the entries whose question is not this one, and it is not
            // part of the gate.
            options.compileOnly = true        } else if (arg === '--jobs' && i + 1 < argv.length) {
            options.jobs = positiveInteger(argv[++i], '--jobs')
        } else if (arg.startsWith('--jobs=')) {
            options.jobs = positiveInteger(arg.slice('--jobs='.length), '--jobs')
        } else if (arg.startsWith('-')) {
            // A word that starts with `-` is never a target. Reaching this point means the caller
            // named an option this checkout does not have -- normally because the caller is newer
            // than the tree it was pointed at -- and pushing it as a target made `walkPlans` stat
            // `.../--list-cases` and throw ENOENT, which every consumer reads as "this selection
            // could not be read". Measured: a lane cut at 0beeecb9fb, before this tree
            // had `--list-cases`, answered the host's `[RUNNER, ...targets, '--list-cases',
            // '--tier', fast]` with a stack trace and exit 1, and `scripts/selection-cases.mjs`
            // reported `the plan loader refused this selection (exit 1)` -- a version mismatch
            // wearing the words of an empty selection.
            throw argvRefusal(
                `unknown option '${arg}': this runner does not know it (the caller is newer than this tree)`,
            )
        } else {
            options.targets.push(path.resolve(process.cwd(), arg))
        }
    }
    // The debug layer never runs on a Release binary: its whole point is the assertions that are
    // compiled out of one, and the escape case aborts. `--tier debug` therefore selects the
    // Debug build; saying `--build release` with it is a contradiction, not a preference.
    if (options.tier === 'debug') {
        if (options.buildExplicit && options.build !== 'debug') {
            throw new Error(`--tier debug runs on the Debug build; --build ${options.build} was given`)
        }
        options.build = 'debug'
    }
    // `--selection-empty` says this run's selection is a measured zero, so a target beside it is a
    // contradiction: the flag means nothing was selected and the target means something was.
    if (options.selectionEmpty && options.targets.length > 0) {
        throw new Error(
            `--selection-empty means no plan was selected, so it cannot travel with a target (${options.targets[0]})`
        )
    }
    return options
}

/**
 * The install this run measures, and whether it is there: `--camel <path>` names it, else
 * `out/<build>`, and `CAMEL_TEST_BUILD` or the Release default names the build -- one rule, read
 * once. It is returned rather than printed and exited on so that a caller can ask the question
 * before it walks into the refusal: the runner's own self-test asks it once, because reporting the
 * same missing binary once per check turned one absent install into a page of reds that said
 * nothing about the code (test/tools/selftest/run.mjs).
 *
 * `present` is the whole answer; `buildCommand` is the helper that would build this install, so a
 * refusal and the self-test's one line cannot disagree about how to fix it.
 */
export function measuredInstall(options = {}) {
    const envBuild = process.env.CAMEL_TEST_BUILD
    const build = options.build
        || (envBuild !== undefined && Object.hasOwn(BUILDS, envBuild) ? envBuild : 'release')
    const exe = camelBinary({ ...options, build })
    return {
        build,
        label: BUILDS[build].label,
        exe,
        present: fs.existsSync(exe),
        buildCommand:
            `pwsh -NoProfile -File ${path.join(path.dirname(REPO_ROOT), 'camel-build.ps1')} -Path ` +
            `${REPO_ROOT} -Config ${BUILDS[build].label}`,
    }
}

/**
 * Refuse to run when the install this run measures is not built. `--camel <path>` names it; with
 * none it is `out/<build>`, written by `collect-out.js <build>`. The debug layer additionally
 * refuses anything but a Debug binary: a binary left there by another configuration is caught by
 * its own --version line, which names the build.
 */
function preflightBuild(options) {
    const install = measuredInstall(options)
    if (!install.present) {
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} no ${install.label} binary at ${install.exe}`
        )
        if (options.camel) {
            console.error('    --camel names the install this run measures; it must be a built install\'s bin/camel.')
        } else {
            console.error(`    Build it with the shared helper: ${install.buildCommand}`)
            console.error(`    The ${install.label} tier never falls back to out/latest.`)
        }
        process.exit(2)
    }
    if (options.build !== 'debug') return
    const home = camelInstall(options)
    const pathKey = IS_WINDOWS ? Object.keys(process.env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    const probe = spawnSync(install.exe, ['--version'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 30000,
        env: {
            ...process.env,
            CAMEL_HOME: home,
            [pathKey]: `${path.join(home, 'bin')}${path.delimiter}${process.env[pathKey] || ''}`,
        },
    })
    const text = `${probe.stdout || ''}${probe.stderr || ''}`
    if (probe.error || !/\(Debug\b/.test(text)) {
        console.error(`${COLORS.red}[FAIL]${COLORS.reset} ${install.exe} is not a Debug build: ${text.trim() || probe.error || 'no version'}`)
        console.error('    Rebuild it with: npm run debug (through the shared helper, -Config Debug).')
        process.exit(2)
    }
}

/**
 * The Python interpreter for test preconditions and `tool = "python"` tests: the active virtual
 * environment's, else the benchmark environment's (benchmarks/.venv), else PATH's.
 */
function pythonExecutable() {
    const venvPython = (venv) =>
        IS_WINDOWS ? path.join(venv, 'Scripts', 'python.exe') : path.join(venv, 'bin', 'python')
    for (const venv of [process.env.VIRTUAL_ENV, path.join(REPO_ROOT, 'benchmarks', '.venv')]) {
        if (venv && fs.existsSync(venvPython(venv))) return venvPython(venv)
    }
    return IS_WINDOWS ? 'python' : 'python3'
}

function loadVars(options) {
    // Built-in variables; test/vars.toml may override them. `${camel}` is the compiler of the run,
    // the same path the runner prints as `[INFO] camel executable` and records as `camel_exe` in
    // summary.json: a plan that runs a program which starts a compiler names the install with
    // `--camel ${camel}`, a command-line argument and never an environment switch.
    const builtins = {
        python: pythonExecutable(),
        camel: camelBinary(options),
    }
    if (!fs.existsSync(VARS_PATH)) return builtins
    return { ...builtins, ...parseTomlFile(VARS_PATH) }
}

function parseDiagnostics(text) {
    const diagnostics = []
    const clean = applyNormalizers(text || '', ['strip_ansi', 'normalize_newlines', 'trim_trailing_space'])
    // A diagnostic that belongs to the run's configuration rather than to a line of the
    // program (a device named by --devices, for one) has no source position; the short format
    // writes `?` there. It is still a diagnostic: name and code can be asserted, and line and
    // column stay absent.
    //
    // The message of one diagnostic may run onto further lines, and the short format closes the
    // last of them with `(name=..., code=...)`: `SemanticDiag::NoMatchingFunction` writes the
    // overload list of a call to a function of the program that way. So it is the header that
    // starts a diagnostic and the block runs to the next header, where the suffix is read. A
    // message read this way is read whole; the suffix of a wrapped message can no longer be
    // taken for another diagnostic's, and one whose message stays on the header line reads
    // exactly as before.
    const header = /^(?:\([^)]+\))?(.*?):(\d+|\?):(\d+|\?): \[[^\]]+\]: /gm
    const starts = []
    let match
    while ((match = header.exec(clean)) !== null) {
        starts.push({
            path: match[1],
            line: match[2],
            column: match[3],
            begin: match.index,
            after: header.lastIndex,
        })
    }
    const suffix = /\(name=([^,]+), code=(0x[0-9A-Fa-f]+)\)/
    for (let i = 0; i < starts.length; ++i) {
        const end = i + 1 < starts.length ? starts[i + 1].begin : clean.length
        const closed = suffix.exec(clean.slice(starts[i].after, end))
        if (closed === null) continue // a message with no name to assert (--error-format text)
        diagnostics.push({
            path: starts[i].path,
            line: starts[i].line === '?' ? undefined : Number.parseInt(starts[i].line, 10),
            column: starts[i].column === '?' ? undefined : Number.parseInt(starts[i].column, 10),
            name: closed[1],
            code: closed[2],
        })
    }
    return diagnostics
}

function expandArgToken(token, scope) {
    const exact = String(token).match(/^\$\{([A-Za-z0-9_.-]+)\}$/)
    if (exact) {
        const value = scope[exact[1]]
        if (Array.isArray(value)) return value.flatMap((item) => expandArgToken(item, scope))
        if (value === undefined) throw new Error(`unknown variable ${exact[1]}`)
        return [String(value)]
    }
    return [String(token).replace(/\$\{([A-Za-z0-9_.-]+)\}/g, (_, name) => {
        if (!(name in scope)) throw new Error(`unknown variable ${name}`)
        const value = scope[name]
        if (Array.isArray(value)) throw new Error(`array variable ${name} must be used as a full token`)
        return String(value)
    })]
}

// Exported because the layer report (`scripts/tier-report.mjs`) has to know what a copy actually
// launches: two entries whose argument lists read the same can still resolve to different
// commands (`${backend}` is a pass list from `test/vars.toml`), and a report that compared the
// written words would call 3293 distinct runs 2233. One expansion, one answer.
export function expandArgs(args, scope) {
    const expanded = []
    for (const token of args || []) {
        expanded.push(...expandArgToken(token, scope))
    }
    return expanded
}

/**
 * Substitute `${name}` in an assertion string (the `expect_*` fields), so an expectation about a
 * path a program prints can name this test's scratch directory. Unknown names are errors, as in
 * `args`.
 */
export function expandTextTokens(text, scope) {
    return String(text).replace(/\$\{([A-Za-z0-9_.-]+)\}/g, (_, name) => {
        if (!(name in scope)) throw new Error(`unknown variable ${name}`)
        const value = scope[name]
        if (Array.isArray(value)) throw new Error(`array variable ${name} cannot be used in text`)
        return String(value)
    })
}

function formatDurationMs(valueMs, digits = 2) {
    if (Math.abs(valueMs) >= 1000) {
        return `${(valueMs / 1000).toFixed(digits)} s`
    }
    return `${valueMs.toFixed(digits)} ms`
}

function benchmarkSampleCount(benchmark) {
    if (Array.isArray(benchmark.samples_ms)) {
        return benchmark.samples_ms.length
    }
    if (Number.isInteger(benchmark.iterations) && benchmark.iterations >= 0) {
        return benchmark.iterations
    }
    return null
}

/**
 * The CPU reading of a test that declared `cpu_budget_ms`: the last JSON line
 * test/tools/child-cpu.mjs wrote on file descriptor 3. A missing or unreadable reading is a reason
 * the caller turns into a failure -- a budget the runner cannot read has examined nothing.
 */
function readCpuReport(text) {
    const line = String(text || '')
        .split('\n')
        .map((entry) => entry.trim())
        .filter(Boolean)
        .pop()
    if (!line) {
        return { cpuMs: null, reason: 'the runner got no CPU report from test/tools/child-cpu.mjs' }
    }
    let report
    try {
        report = JSON.parse(line)
    } catch (error) {
        return { cpuMs: null, reason: `the CPU report is not JSON (${error.message})` }
    }
    if (!Number.isFinite(report.cpu_ms)) {
        return { cpuMs: null, reason: report.reading || 'the child reported no CPU figure' }
    }
    return { cpuMs: report.cpu_ms, reason: null }
}

/**
 * How a run ended, in words that are not `null`.
 *
 * `exitCode` is `null` when there is no exit code to report: the child was killed by the timeout,
 * a signal took it down, or it never started. Printing `got null` for all three reads as one thing
 * ("something odd"), and it was read as a timeout for two rounds while the process was in fact
 * dying of SIGSEGV in 0.03 s -- a crash looked at as a hang, in both directions of the diagnosis.
 * So the three shapes are named and kept apart:
 *
 *   got exit 1
 *   got signal SIGSEGV (rc=139, 0.03 s)      -- the shell's convention, 128 + the signal number
 *   got timeout after 30000 ms               -- only a real timeout says this
 *   could not start: <message>               -- the spawn failed; the process never existed
 */
function describeExit(result) {
    const where = result.errorMessage ? ` could not start: ${result.errorMessage}` : null
    if (where) return where.trim()
    if (result.timedOut) {
        const bound = typeof result.timeoutMs === 'number' ? ` after ${result.timeoutMs} ms` : ''
        return `timeout${bound}`
    }
    if (result.signal) {
        const number = os.constants && os.constants.signals ? os.constants.signals[result.signal] : undefined
        const rc = typeof number === 'number' ? `rc=${128 + number}, ` : ''
        return `signal ${result.signal} (${rc}${formatDurationMs(result.wallMs)})`
    }
    return `exit ${result.exitCode}`
}

/** The number a signal name stands for, as this platform numbers it; undefined for no signal. */
function signalNumberOf(name) {
    const signals = (os.constants && os.constants.signals) || {}
    if (typeof name !== 'string' || name.length === 0) return undefined
    return Object.prototype.hasOwnProperty.call(signals, name) ? signals[name] : undefined
}

/**
 * The signal a plan expects, as the number this platform numbers it by: `expect_signal = "ABRT"`,
 * `"SIGABRT"` and `6` are one expectation written three ways on POSIX. A number is this platform's
 * number and no other's -- Windows numbers SIGABRT 22, so `6` is refused there -- which is why the
 * name is the spelling a plan uses when it means one particular signal.
 *
 * There is no default and no boolean form. `expect_exit = 0` is the ordinary end of a process, so an
 * entry that says nothing is asserting it; a process that dies of a signal is not ordinary, and an
 * entry that expects one has to name it -- otherwise the entry says nothing a reader could check,
 * and a death by a *different* signal would pass it.
 */
function expectedSignalNumber(value, prefix = '') {
    const signals = (os.constants && os.constants.signals) || {}
    if (typeof value === 'number') {
        if (!Object.values(signals).includes(value)) {
            throw new PlanSchemaError(
                `${prefix}expect_signal: ${value} is not a signal number on this platform`
            )
        }
        return value
    }
    if (typeof value !== 'string' || value.trim().length === 0) {
        throw new PlanSchemaError(
            `${prefix}expect_signal must name a signal ("ABRT", "SIGABRT") or give the number this ` +
                `platform numbers it by, got ${JSON.stringify(value)}: a plan that expects a crash has ` +
                `to say which signal killed it`
        )
    }
    const written = value.trim().toUpperCase()
    const name = written.startsWith('SIG') ? written : `SIG${written}`
    const number = signalNumberOf(name)
    if (number === undefined) {
        throw new PlanSchemaError(
            `${prefix}expect_signal: '${value}' is not a signal this platform knows`
        )
    }
    return number
}

function formatMetric(result) {
    // A test judged on its own CPU time prints the CPU figure it was judged on, with the budget it
    // was read against and the wall clock beside it as information: on this host the wall figure is
    // the neighbours' scheduling, and a reader who wants it should not have to open meta.json.
    if (typeof result.cpuBudgetMs === 'number') {
        const reading = Number.isFinite(result.cpuMs)
            ? formatDurationMs(result.cpuMs)
            : 'no reading'
        return `${reading} cpu of ${formatDurationMs(result.cpuBudgetMs)} budget, ` +
            `${formatDurationMs(result.wallMs)} wall`
    }
    // A failed `--timeit` iteration reports the diagnosis, not a mean: the failure is the
    // test's result, so the status line carries no number.
    if (result.benchmark && Number.isFinite(result.benchmark.mean_ms)) {
        const samples = benchmarkSampleCount(result.benchmark)
        const sampleSuffix = samples === null ? '' : ` (n=${samples})`
        if (samples !== null && samples < 2) {
            return `${formatDurationMs(result.benchmark.mean_ms)}${sampleSuffix}`
        }
        return `${formatDurationMs(result.benchmark.mean_ms)} +/- ${formatDurationMs(result.benchmark.ci95_ms)}${sampleSuffix}`
    }
    if (typeof result.wallMs === 'number') {
        return formatDurationMs(result.wallMs)
    }
    return ''
}

/**
 * Whether this entry's line waits for a second attempt: it failed because its own bound took it.
 *
 * `timedOut` is `runProcess`'s own reading -- the child was killed at its `timeout_ms` -- and it is
 * the only shape a starved case produces, which is why it is the whole condition. A failure with an
 * exit code has already answered its question; a verification script's timeout is that script's
 * bound (it fails the case through `verifyFailureLines`), not the case's, and is left alone.
 *
 * A marked known failure is not re-run: its red is already excused, and a second attempt would
 * answer a different question ("has the mark healed?") whose rule is `known_fail_ends_when`.
 */
function rerunCandidate(entry, options) {
    if (options.compileOnly) return false
    const outcome = entry.outcome
    if (!outcome || outcome.status !== 'fail' || !outcome.result) return false
    if (entry.test.__knownFail) return false
    return outcome.result.timedOut === true
}

/**
 * The host's load average in the shape `internal/tools/camel-remote.ps1` prints it, or null where
 * this host has no reading.
 *
 * A second attempt is a timing, and a timing means nothing without the load it was taken under: on
 * this project's build host the 1-minute load is 22-31 most of the day, and the same case has been
 * measured at 12.3 s CPU and at 80.6 s for the same work. So the line that reports the second
 * attempt states the load beside it, and where there is no reading it says so instead of printing
 * zeros -- `os.loadavg()` answers [0, 0, 0] on Windows, which is a reading of nothing dressed as a
 * reading of no load.
 */
function hostLoadReading() {
    try {
        const fields = fs.readFileSync('/proc/loadavg', 'utf8').trim().split(/\s+/).slice(0, 3).map(Number)
        if (fields.length === 3 && fields.every(Number.isFinite)) return fields
    } catch {
        // No /proc/loadavg on this host: the caller prints `load unreadable here`.
    }
    return null
}

function formatLoad(load) {
    if (load === null) return 'load unreadable here'
    return `load ${load.map((value) => value.toFixed(2)).join(' ')} (1m/5m/15m)`
}

/**
 * What a re-run case's one line says beyond its verdict: that the first attempt timed out, and what
 * the second attempt did with twice the bound. Both facts stay on the line, because the point of
 * the mechanism is that a green reached this way is not the same reading as a green reached first
 * time -- and a failure reached this way is not the same as one that never got a second look.
 */
function rerunNote(status, result, load) {
    const opening = 'timed out on the first pass'
    if (status === 'pass') {
        return `${opening}, passed in ${formatDurationMs(result.wallMs)} on re-run (${formatLoad(load)})`
    }
    // The second attempt can end otherwise than by its bound, and each shape is said: a crash on
    // the second attempt is a failure like any other, and a line that called it a timeout would be
    // the one reading this runner must never produce.
    if (result.timedOut) return `${opening} and again on re-run (${formatLoad(load)})`
    if (result.signal) {
        return `${opening}, killed by ${result.signal} on re-run (${formatLoad(load)})`
    }
    return `${opening}, failed again on re-run (${formatLoad(load)})`
}

function printPlanHeader(plan) {
    const relativePath = path.relative(PLANS_ROOT, plan.__planPath).replace(/\\/g, '/')
    const showPath = relativePath.split('.')[0].replace(/\//g, '.')
    const planLabel = plan.desc || plan.id || '.'
    console.log('')
    console.log(`${COLORS.cyan}[PLAN]${COLORS.reset} ${showPath}: ${planLabel}${staleAllowedMark()}`)
}

/**
 * One verdict, printed, and handed back as the exact text that was printed. The return value is
 * what the transcript records (`test/tools/transcript.mjs`), so the console line and the recorded
 * line are one string and cannot drift; it comes back with the colours stripped, because a
 * transcript that carries escape sequences is a file only a terminal can count.
 */
function printStatusLine(status, name, metric = '') {
    const label = status === 'pass'
        ? `${COLORS.green}[PASS]${COLORS.reset}`
        : status === 'skip'
            ? `${COLORS.yellow}[SKIP]${COLORS.reset}`
            : status === 'xfail'
                ? `${COLORS.yellow}[XFAIL]${COLORS.reset}`
                : `${COLORS.red}[FAIL]${COLORS.reset}`
    const left = `${label} ${name}`
    if (!metric) {
        console.log(left)
        return plainStatusLine(left)
    }
    const width = 78
    const spaces = ' '.repeat(Math.max(1, width - left.length - metric.length))
    const line = `${left}${spaces}${metric}`
    console.log(line)
    return plainStatusLine(line)
}

/** The console text without its colour, which is all a transcript holds. */
function plainStatusLine(line) {
    return line.replace(/\x1b\[[0-9;]*m/g, '')
}

/**
 * The plan file a `[[sweeps]]` copy's case was written in, as one detail line, or null for a test
 * the plan declares itself.
 *
 * A copy belongs to the plan that runs it -- `meta.json`'s `plan_path`, the log directory and the
 * cost reading are the sweeper's, and they are right to be: the sweeper is what started the run.
 * Its name carries the source plan too (`<sweep>:<source plan id>:<test>`), and that is where the
 * reader was left: the verdict line and the `[PLAN]` header above it name the sweeper, which holds
 * no such test. So a person who takes a red out of this run and opens the file it names finds
 * nothing to fix, and the file to open is the one that wrote the case. The plan file is the one
 * thing a name cannot carry -- the id is not the path, and a plan with no `id` gives a basename
 * that several plans share -- so it is printed here, under the verdict, in the shape the runner
 * already uses when a plan's *own* file is what is wrong (`[INFO] runner error: <plan file>: ...`).
 * Both readings then answer "which file does this red belong to", and they answer with different
 * files.
 *
 * Not printed for a passing copy: a green case sends nobody anywhere, and the fast layer's
 * sampled copies would each grow a second line for a puzzle no one is solving.
 */
function sweepSourceLine(test) {
    const source = test.__sweepSource
    if (!source) return null
    return `source plan: ${planLabel(source.plan_path)}`
}

function printDetailLines(lines) {
    for (const line of lines) {
        const shortKv = line.match(/^([^:]{1,24}):\s+(.+)$/)
        if (shortKv && shortKv[2].length <= 60) {
            console.log(`    ${COLORS.cyan}[INFO]${COLORS.reset} ${shortKv[1].padEnd(28)} ${shortKv[2]}`)
        } else {
            console.log(`    ${COLORS.cyan}[INFO]${COLORS.reset} ${line}`)
        }
    }
}

/**
 * A regular expression that matched nothing says so with the two things a reader needs to fix it or
 * to know it is a real defect: the pattern as it was written, and the head of the output it was
 * matched against. The head, not the tail: a shape assertion is about how the output opens, and the
 * tail is what `printFailedOutput` already shows. Twelve lines, the same bound that function uses.
 */
function printMatchDiagnostics(diagnostics) {
    const maxLines = 12
    const maxWidth = 240
    for (const entry of diagnostics || []) {
        const lines = String(entry.stdout).replace(/\n$/, '').split('\n')
        const shown = lines.slice(0, maxLines)
        console.log(`    ${COLORS.cyan}[match]${COLORS.reset} pattern /${entry.pattern}/ -- a legal regular expression that matched no line`)
        if (shown.length === 0 || (shown.length === 1 && shown[0] === '')) {
            console.log(`    ${COLORS.cyan}[stdout head]${COLORS.reset} (empty)`)
            continue
        }
        console.log(
            `    ${COLORS.cyan}[stdout head]${COLORS.reset} ${shown.length} of ${lines.length} line(s)` +
                (lines.length > shown.length ? ', the rest is in the output file above' : ''),
        )
        for (const line of shown) {
            const text = line.length > maxWidth ? `${line.slice(0, maxWidth)}...` : line
            console.log(`      ${text}`)
        }
    }
}

/**
 * The end of what a failed test wrote, for a log that is all there is to read (continuous
 * integration, where the files of test/results are not at hand): with CI or
 * CAMEL_TEST_SHOW_OUTPUT set, the last lines of standard output and standard error follow the
 * failure, line ends and control characters made visible.
 *
 * When the tail is shorter than what the test wrote, the line that cuts it says how much it cut
 * **and where the uncut text is** (`pointer`, the same line the caller printed above the tail). A
 * printer that cuts evidence in silence is worse than one that prints nothing: the second sends the
 * reader to the log, and the first lets them believe they have already read it. Measured on
 * 2026-11-04: `runner.backend_matrix` was red because the two lines naming the defect were 75 lines
 * above a tail that looked complete, and the elision line said `... 75 line(s) before` -- a count
 * with no destination.
 */
const SHOW_FAILED_OUTPUT = Boolean(process.env.CAMEL_TEST_SHOW_OUTPUT || process.env.CI)

function printFailedOutput(result, pointer) {
    if (!SHOW_FAILED_OUTPUT) return
    const maxLines = 12
    const maxWidth = 240
    // Two `(empty)` lines say the same thing twice, and a reader skimming them sees the shape of a
    // printer that lost its input rather than a run that said nothing. One sentence names the fact
    // the two lines only implied, and it keeps the pointer, which is where the uncut text is.
    if (!result.stdout && !result.stderr) {
        const where = pointer ? ` -- ${pointer}` : ''
        console.log(
            `    ${COLORS.cyan}[output]${COLORS.reset} both empty: the run produced no explanation${where}`,
        )
        return
    }
    for (const [name, text] of [['stdout', result.stdout], ['stderr', result.stderr]]) {
        if (!text) {
            console.log(`    ${COLORS.cyan}[${name}]${COLORS.reset} (empty)`)
            continue
        }
        const lines = text.replace(/\n$/, '').split('\n')
        const shown = lines.slice(-maxLines)
        if (lines.length > shown.length) {
            const where = pointer ? ` -- ${pointer}` : ''
            console.log(
                `    ${COLORS.cyan}[${name}]${COLORS.reset} omitted ${lines.length - shown.length} line(s) above${where}`,
            )
        }
        for (const line of shown) {
            const visible = line
                .replace(/\r/g, '\\r')
                .replace(/\x1b/g, '\\e')
                .replace(/[\x00-\x08\x0b-\x1f]/g, (c) => `\\x${c.charCodeAt(0).toString(16).padStart(2, '0')}`)
            const clipped = visible.length > maxWidth ? `${visible.slice(0, maxWidth)}...` : visible
            console.log(`    ${COLORS.cyan}[${name}]${COLORS.reset} ${clipped}`)
        }
    }
}

/**
 * Where a failed test's whole output is. The log keeps twelve lines of it (`printFailedOutput`,
 * and only under CI), which is enough to see the shape of a failure and not enough to read the
 * cause: twice on 2026-11-02 a conclusion was drawn from a truncated fragment whose first line --
 * the one that said what happened -- was above the cut. The runner already writes every test's
 * stdout, stderr, merged output and meta.json into its run directory; this line is the pointer to
 * it, relative to the checkout so it reads the same on a build machine as here.
 */
function fullOutputLine(statusDir) {
    const relative = path.relative(REPO_ROOT, statusDir).split(path.sep).join('/')
    return `full output: ${relative}/ (stdout.txt, stderr.txt, output.txt, meta.json)`
}

/**
 * The commit, the binary and the tree a run's numbers belong to, on one line before them.
 *
 * Four rewrites in one night came from a correct number attributed to the wrong artifact: a stale
 * `refs/remotes` ("those commits are on no branch"), `build.ninja` read as `build-Release.ninja`, a
 * lane's readings filed as `develop`'s, and a `-NoBuild` run's hollow exit code read as a failure.
 * The numbers were right and their owner was not, so the run states its owner:
 *
 *   [provenance] tip <sha> | binary built <ISO> | tree <abs> | tier fast | jobs 8
 *
 * `tip` is `git rev-parse HEAD` **of this checkout** -- never a branch ref, which names another tree's
 * commit. A dirty tree prints `(dirty: N files)` and no sha: the sha names a commit, and what ran was
 * that commit plus edits nobody recorded. `binary built` is the executable's mtime, not the build
 * log's: "the build ran" is not "the binary is new" (a failed link, a stale out/latest and another
 * tree's artifacts all leave the log looking right).
 *
 * Cost is two `git` calls and one `stat` per run, paid once, at the summary where the numbers are.
 */
function readProvenance(options) {
    const sha = gitOutput(['rev-parse', 'HEAD'])
    // Untracked files are counted as dirty on purpose: a new case file or a new plan that the commit
    // does not carry is part of what ran, and `run.json`'s narrower count (tracked only) answers a
    // different question.
    const porcelain = gitOutput(['status', '--porcelain'])
    const dirty = porcelain === null ? 0 : porcelain.split('\n').filter((line) => line.trim() !== '').length
    let binaryBuilt = 'unknown'
    try {
        binaryBuilt = formatIsoTime(fs.statSync(camelBinary(options)).mtime)
    } catch {
        // A run that never started a camel program may have no binary (a plan of `tool = "node"`
        // cases, a tree that was never built): the field says `unknown` rather than inventing one.
    }
    return provenanceLine({
        sha: sha || null,
        dirty,
        binaryBuilt,
        tree: REPO_ROOT,
        tier: options.tier,
        jobs: options.jobs,
    })
}

function printProvenance(options) {
    console.log(`${COLORS.cyan}[provenance]${COLORS.reset} ${readProvenance(options)}${staleAllowedMark()}`)
}

/**
 * What the run left in the checkout, on the line after `[SUMMARY]` and for the same reason the line
 * above it exists: a reading belongs to a tree, and a tree that this run just made dirty is the one
 * thing that makes the next `[provenance]` untrue. The heading is the whole check -- a run that
 * leaves a path behind fails, and the exit code says so -- and the detail under it names the path
 * and the test.
 *
 * Three lines and no fourth: `clean`, `FAIL`, and what was already there before the run started.
 * `not judged` is said out loud for the one run that cannot answer (`--update-golden` rewrites
 * tracked files on purpose), because a check that quietly turns itself off is the defect it exists
 * to catch. Everything the verdict rests on is in test/tools/tree-guard.mjs.
 */
function printTreeVerdict(verdict) {
    if (verdict.deferred) {
        // A nested run that printed nothing here would look like a run whose check had been removed;
        // it prints where the verdict went instead, because the reader of a nested run's output is
        // usually looking for the line and not finding it is how a deferral turns into a suspicion.
        console.log(
            `${COLORS.cyan}[TREE]${COLORS.reset} ${verdict.reason} (this run's window is inside ` +
                `that run's, so it is not a second reading of the same tree)`
        )
        return
    }
    if (!verdict.judged) {
        // `not judged` is said out loud for the one run that cannot answer (`--update-golden`
        // rewrites tracked files on purpose), because a check that quietly turns itself off is the
        // defect it exists to catch. A reading that failed -- `git` not there, a status that
        // returned non-zero -- is that defect, and it is red.
        const line = `[TREE] not judged: ${verdict.reason}`
        if (verdict.failed) console.error(`${COLORS.red}[FAIL]${COLORS.reset} ${line}`)
        else console.log(`${COLORS.yellow}[TREE]${COLORS.reset} ${line}`)
        return
    }
    const plural = (count, word) => `${count} ${word}${count === 1 ? '' : 's'}`
    const who = (item) => {
        if (item.at === null) return 'the watcher did not see it appear (attribution unavailable)'
        const at = `first seen ${(item.at / 1000).toFixed(1)} s into the run`
        if (item.running.length === 0) return `${at}, while no test was running`
        // Five names and a count: at `-Jobs 8` a handful is a reading, and the first version of this
        // line printed all of them -- one path written while 7,270 entries were "running" made a
        // 65 KB line that the terminal cut in half, so the diagnosis was unreadable exactly when it
        // was needed.
        const names = item.running.slice(0, 5).map(
            (run) => `${run.plan ? `${run.plan}/` : ''}${run.name}${run.case ? ` (${run.case})` : ''}`
        )
        const more = item.running.length > names.length ? `, and ${item.running.length - names.length} more` : ''
        return `${at}, while ${item.running.length === 1 ? 'this test was' : `${item.running.length} tests were`} running: ${names.join(', ')}${more}`
    }
    const listed = (item) => `${item.status.padEnd(2)} ${item.path}`
    // A path that appeared and went again has no status to print: `git status` does not list it, and
    // the only thing known about it is what it was when the watcher saw it.
    const goneAgain = (item) => `${item.path} -- ${who(item)}`
    if (verdict.failed) {
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} [TREE] ${plural(verdict.appeared.length, 'path')} appeared under the ` +
                'checkout during this run: the run wrote outside test/results and tmp/test'
        )
        for (const item of verdict.appeared) {
            console.error(`    ${COLORS.red}[TREE]${COLORS.reset} ${listed(item)} -- ${who(item)}`)
        }
    } else {
        // "left nothing" and not "nothing appeared": a path that appeared and went again is said on
        // its own line below, and a green line that contradicted it would be the wrong reading.
        console.log(
            `${COLORS.cyan}[TREE]${COLORS.reset} clean: the run left nothing under the checkout ` +
                `(git status --porcelain -u normal: ${verdict.beforeCount} before, ${verdict.afterCount} after, ` +
                `${verdict.watched} director${verdict.watched === 1 ? 'y' : 'ies'} watched)`
        )
    }
    if (verdict.preexisting.length > 0) {
        // Printed whatever the verdict: the paths a run before this one left are not this run's, but
        // they are somebody's, and dropping them from the report is how they become permanent.
        const shown = verdict.preexisting.slice(0, 12).join(', ')
        const more = verdict.preexisting.length > 12 ? `, and ${verdict.preexisting.length - 12} more` : ''
        console.log(
            `${COLORS.cyan}[TREE]${COLORS.reset} ${plural(verdict.preexisting.length, 'path')} were already ` +
                `uncommitted when this run started, not this run's: ${shown}${more}`
        )
    }
    if (verdict.transient.length > 0) {
        // Gone by the end of the run, so it is not a failure -- but a test that writes into the
        // checkout and removes it again is one cleanup call away from leaving it.
        const shown = verdict.transient.slice(0, 12).map(goneAgain).join(', ')
        const more = verdict.transient.length > 12 ? `, and ${verdict.transient.length - 12} more` : ''
        console.log(
            `${COLORS.yellow}[TREE]${COLORS.reset} ${plural(verdict.transient.length, 'path')} appeared in the ` +
                `checkout and were gone again by the end of the run: ${shown}${more}`
        )
    }
    if (verdict.failure || verdict.watched === 0) {
        // The watcher is the attribution and never the verdict, so a broken watcher costs the names
        // above and nothing else. Said out loud so that a report with no name in it says why --
        // `0 directories watched` is not a tree with nothing in it, it is a watcher that did not
        // start, and a line that printed it without comment would read as a clean run.
        const why = verdict.failure || 'the walk watched no directory at all'
        console.log(`${COLORS.yellow}[TREE]${COLORS.reset} the path watcher is incomplete (${why})`)
    }
}

/**
 * The name `--list-known` prints for one test: its plan id in front of the case name. A case name is
 * unique inside its plan and nowhere else -- `backends` appends the back end, a `[[sweeps]]` entry
 * copies another plan's names wholesale -- so the plan travels in the name. It is the same
 * `<plan id>/<name>` pair the runner already keys dependency results by (`byPlanName`), and the part
 * after the last `/` is exactly the name the `[XFAIL]` line prints.
 */
function listedName(plan, test) {
    const planId = test.__planId || plan.id || path.basename(plan.__planPath, '.plan.toml')
    return `${planId}/${test.name}`
}

/**
 * The four verdicts, added: the number `[SUMMARY] total:` prints, and the number the transcript
 * has to hold. One function so the line a reader reads and the number the check compares cannot be
 * two different sums.
 */
function summaryTotal(summary) {
    return summary.pass + summary.fail + summary.skip + summary.xfail
}

/**
 * Wait until everything already written to `stream` has left the process.
 *
 * `process.stdout` is a pipe on a host run, and there the write is asynchronous: `process.exit`
 * ends the process with whatever is still queued, and the tail of the run's own output goes with
 * it. Measured over three runs of one base: every one of them printed `total: 9159` and
 * every one of them printed its 9159 case lines over the minutes it ran, while the `--list-known`
 * block -- the only thing printed in one tight loop, immediately before the exit -- arrived with
 * 9159 rows, 5265 rows and 1239 rows, the fewest from the slowest run. One empty write whose
 * callback fires when the queue is empty is the whole fix; it costs a tick and no output.
 */
function flushStream(stream) {
    return new Promise((resolve) => {
        try {
            stream.write('', () => resolve())
        } catch {
            resolve()
        }
    })
}

async function flushOutput() {
    await Promise.all([flushStream(process.stdout), flushStream(process.stderr)])
}

function printRunSummary(summary, transcript) {
    const total = summaryTotal(summary)
    const skipped = summary.skip > 0 ? `, skip: ${summary.skip}` : ''
    const known = summary.xfail > 0 ? `, known failures: ${summary.xfail}` : ''
    // The two ways a failing run ends without an exit code, counted apart. They are printed only
    // when one happened, so a green run's summary keeps its shape -- and a red one says which of
    // the two it was without the reader having to look at the individual failures.
    const timedOut = summary.timedOut > 0 ? `, timed out: ${summary.timedOut}` : ''
    const crashed = summary.crashed > 0 ? `, killed by a signal: ${summary.crashed}` : ''
    // The states a second attempt was given, as its own two fields and never inside `pass`:
    // `pass` answers "how many cases are green", and a case that passed on its second attempt IS
    // green -- the gate compares a case by its name, and that name is green -- so it is counted
    // there once, not twice. `re-run` answers "how many cases were given a second attempt" and
    // `re-run passed` how many of those came back green. Merging the pair into `pass` would hide
    // first-attempt timeouts behind a clean number; merging them into `timed out` would give that
    // field two meanings (today it counts the failures whose exit code the bound took), which is
    // exactly the two-facts-one-number shape this runner keeps out of its records.
    const rerun = summary.rerun > 0
        ? `, re-run: ${summary.rerun}, re-run passed: ${summary.rerunPassed}`
        : ''
    console.log('')
    const line =
        `${COLORS.cyan}[SUMMARY]${COLORS.reset} total: ${total}, pass: ${summary.pass}, fail: ${summary.fail}` +
        `${skipped}${known}${timedOut}${crashed}${rerun}${staleAllowedMark()}`
    console.log(line)
    // The same line, without its colour, closes the transcript: the file then states the total its
    // own lines were counted under, so a reader of the file alone -- `transcript.mjs --check` -- can
    // check it without being told the number by whoever fetched it, and a file cut before this line
    // is refused for saying no total rather than read as a run of whatever it still holds.
    if (transcript) transcript.summaryLine(plainStatusLine(line))
}

/**
 * What ends each red the gate accepted, one line per known failure, printed after the tree verdict.
 *
 * A known failure is a red the run lets through, and `known failures: 41` says how many there are
 * and nothing about any of them: which case, and what would end it. `known_fail_ends_when` is the
 * field that answers the second question, and until this printed it the only reader was the person
 * who opened the plan file -- the same shape this project already called worse than no mark at all
 * (the owner had it, and printing it is why the `[XFAIL]` line carries `known_fail_owner`).
 *
 * `(no end condition)` in red is the reading this exists to expose: a red nobody has said how to
 * end. The closing count is the metric -- how many of the run's known failures are of that kind --
 * so a run whose marks are all pinned reads `0 of 41`, and a run that adds a mark without an end
 * sentence moves that number instead of hiding inside a total.
 *
 * Position: after `printTreeVerdict`, because `[provenance]` then `[SUMMARY]` then what the run left
 * in the checkout are three lines whose order is pinned (test/cases/tools/provenance.mjs), and a
 * block inserted between them would be a fourth reading of the same run claiming a place in it.
 */
function printKnownRoster(summary) {
    const rows = summary.knownRows || []
    if (rows.length === 0) return
    const missing = rows.filter((row) => !row.endsWhen).length
    for (const row of rows) {
        const ends = row.endsWhen ? describeEndCondition(row.endsWhen) : `${COLORS.red}(no end condition)${COLORS.reset}`
        console.log(`${COLORS.cyan}[KNOWN]${COLORS.reset} ${row.name}: ${ends}`)
    }
    console.log(
        `${COLORS.cyan}[KNOWN]${COLORS.reset} ${missing} of ${rows.length} known failure(s) ` +
            'carry no known_fail_ends_when'
    )
}

/**
 * The gate's three numbers, one line. A run that names the build it just paid for (the remote
 * helper exports CAMEL_GATE_BUILD_SECONDS) prints build + test = total; a run that built nothing
 * (a local plan run, or the -NoBuild path, which exports 0) prints build 0.0 s. The format is
 * pinned by test/tools/selftest/run.mjs, so a regression in the split is red, not a missing line.
 */
function gateSplit(testWallMs) {
    const raw = Number(process.env.CAMEL_GATE_BUILD_SECONDS || 0)
    const buildSeconds = Number.isFinite(raw) && raw > 0 ? raw : 0
    const testSeconds = testWallMs / 1000
    return { buildSeconds, testSeconds, totalSeconds: buildSeconds + testSeconds }
}

function formatGateSplit(split) {
    return `${COLORS.cyan}[SPLIT]${COLORS.reset} build ${split.buildSeconds.toFixed(1)} s, test ${split.testSeconds.toFixed(1)} s, total ${split.totalSeconds.toFixed(1)} s`
}

/**
 * Hide the machine's CUDA devices from a Python child that did not ask for them. Called before a
 * test's own `envs` are applied, so a value the test writes itself still wins; the rule and the
 * evidence behind it are in test/tools/cuda-visibility.mjs.
 */
function hideCudaDevicesFrom(env, test) {
    if (hidesCudaDevices(test)) env[CUDA_VISIBLE_DEVICES] = CUDA_HIDDEN
    return env
}

/**
 * Give a child the one card this test was chosen to run on. Called before a test's own `envs`, so a
 * test that names the variable itself keeps its value, and after `hideCudaDevicesFrom`, so a test
 * that asked for a card is not left with an empty list. `CUDA_DEVICE_ORDER` travels with it because
 * the ordinal is `nvidia-smi`'s: CUDA orders devices fastest-first by default, which is not the
 * order the number came from on a host whose cards differ.
 */
function applyCudaDevice(env, test) {
    if (!test.__cudaDevice) return env
    env[CUDA_DEVICE_ORDER] = PCI_BUS_ID
    env[CUDA_VISIBLE_DEVICES] = String(test.__cudaDevice.ordinal)
    return env
}

/**
 * Pick the card a `cuda_free_device = true` test runs on, once, before anything of its test starts.
 * Returns the reason to skip, or null when the test may run -- with a card, or with no card to give
 * it. A test that writes `CUDA_VISIBLE_DEVICES` itself has made the decision and is left alone,
 * by the same rule that keeps the runner from overruling a `cuda_devices` test's environment.
 *
 * The three answers of the host are kept apart on purpose (`test/tools/free-device.mjs`): cards
 * that are all occupied skip the test with their numbers named, a card that is idle is handed to
 * the child, and a host that lists no card at all leaves the test to run as it always did. Only
 * the middle-of-those-negative case is a skip, so an out-of-memory error inside a run that was
 * given a card stays that test's own failure.
 */
async function selectCudaDeviceFor(test) {
    if (test.cuda_free_device !== true || test.__cudaDevice) return null
    if (declaredVisibility(test) !== null) return null
    const chosen = await chooseFreeDevice()
    if (chosen.kind === 'device') {
        test.__cudaDevice = {
            ordinal: chosen.ordinal,
            uuid: chosen.uuid,
            usedMib: chosen.usedMiB,
            totalMib: chosen.totalMiB
        }
        return null
    }
    if (chosen.kind === 'busy') return chosen.reason
    return null
}

/**
 * A test's precondition: `skip_unless` is a command (same variable expansion as `args`) run from
 * the repository root before the test. A non-zero exit skips the test, reporting the command's
 * output as the reason; the test is then neither passed nor failed. Use it only for conditions
 * outside the code under test, such as network access for a dataset download.
 */
async function checkPrecondition(test, sharedVars, scratch, scratchRelative) {
    if (!test.skip_unless || test.skip_unless.length === 0) {
        return null
    }
    const casePath = entryCasePath(test)
    const scope = { ...sharedVars, case: casePath, test_tmp: scratchRelative }
    const [cmd, ...args] = expandArgs(test.skip_unless, scope)
    // The precondition is a Python child of the runner too, and it gets the same visibility as
    // the test it guards: a precondition that fails on a busy card would skip the test, and a
    // skip is a silent loss of coverage, not a red (docs/test-plans.md, "CUDA visibility").
    const env = { ...process.env, CAMEL_TEST_TMPDIR: scratch }
    if (pythonEntryPoints(test).precondition) hideCudaDevicesFrom(env, test)
    // The precondition is the test's own question about the host, so it is asked with the card the
    // test will be given: a probe that answered "no device" against the card another tenant filled
    // would skip the very test this card was chosen for.
    applyCudaDevice(env, test)
    const proc = await runProcess(cmd, args, {
        cwd: REPO_ROOT,
        // The precondition writes where the test would: a dataset downloaded for it lands in
        // its own scratch directory, so two tests (or two runs) cannot corrupt one cache.
        env,
        timeoutMs: test.skip_unless_timeout_ms || 120000,
    })
    if (proc.status === 0) {
        return null
    }
    const detail = `${proc.stdout || ''}${proc.stderr || ''}`.trim() || proc.error?.message || `exit ${proc.status}`
    return `precondition not met (${test.skip_unless.join(' ')}): ${detail}`
}

/**
 * The artifacts one test reads out of this build: `require_artifacts` is a list of paths, expanded
 * like `args` and resolved from the repository root, where a leading `<install>` is the install this
 * run measures (`out/latest`, `out/debug`, or the root `--camel` named) and a last segment may carry
 * `*`. Returns the paths that are not there, empty when every one of them is.
 *
 * A missing one fails this test and never skips it:
 *
 *   产物的缺失是一个读数(读不到),不是一个结论(不用测)。
 *   A missing product is a reading -- "nothing could be read" -- and not a conclusion -- "nothing to
 *   test".
 *
 * The difference is the whole subject: a skip and a pass are the same number in a run's summary, so
 * an entry whose product stopped being produced keeps its place in the green count while the check
 * body inside it never starts. That is not hypothetical -- three launchers spelled a build directory
 * the layout had moved, and two probes asked for an extension module before the test that imports
 * it, and neither shape could be told apart from a test that held.
 *
 * `skip_unless` stays for the other question: a host's device, its compiler, an optional package --
 * conditions outside the code under test. This field is for the code's own products.
 */
function missingArtifacts(test, sharedVars, scratchRelative, options) {
    if (!Array.isArray(test.require_artifacts) || test.require_artifacts.length === 0) return []
    const casePath = entryCasePath(test)
    const scope = { ...sharedVars, case: casePath, test_tmp: scratchRelative }
    const install = camelInstall(options)
    const missing = []
    for (const entry of expandArgs(test.require_artifacts, scope)) {
        const named = String(entry)
        const fromInstall = named.startsWith('<install>')
        const target = fromInstall
            ? path.join(install, named.slice('<install>'.length).replace(/^[\\/]/, ''))
            : path.resolve(REPO_ROOT, named)
        if (!artifactIsThere(target)) missing.push(named.includes('<install>') ? `${named} (${target})` : target)
    }
    return missing
}

/** Whether `target` is a file, or matches when its last segment carries a `*`. */
function artifactIsThere(target) {
    if (!target.includes('*')) return fs.existsSync(target)
    const directory = path.dirname(target)
    const pattern = path.basename(target)
    const source = pattern.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*')
    let entries
    try {
        entries = fs.readdirSync(directory)
    } catch {
        return false
    }
    const shape = new RegExp(`^${source}$`)
    return entries.some((name) => shape.test(name))
}

/** The failure a test whose artifact is missing reports: the paths first, then what it means. */
// The file an entry is reported against: its case, or the plan file when the entry names none (an
// entry that runs a tool with inline arguments has no case, and `path.resolve(dir, undefined)` throws).
function entryCasePath(test) {
    if (typeof test.case === 'string' && test.case.length > 0) {
        return path.resolve(path.dirname(test.__planPath), test.case)
    }
    return test.__planPath
}

function missingArtifactResult(casePath, missing) {
    return runnerErrorResult(
        casePath,
        [
            'the artifact this test reads was not built:',
            ...missing.map((entry) => `  ${entry}`),
            'a missing product is a reading (nothing could be read), not a conclusion (nothing to ' +
                'test): the entry fails here instead of skipping, because a skip and a pass are the ' +
                'same number in the summary',
        ].join('\n'),
    )
}

function sanitizeSegment(value) {
    return String(value).replace(/[<>:"/\\|?*]+/g, '_')
}

/**
 * The fresh scratch directory of one test; it is removed when the test is done. The plan's id is
 * part of the path so that two tests of different plans with the same name (a sweep copies a
 * test under a prefix, but a future plan may not) cannot share a directory, which in a parallel
 * run would mean one test reading what another wrote.
 */
function createTestScratchDir(test) {
    const base = sanitizeSegment(test.name) || 'test'
    const planBase = sanitizeSegment(test.__planId || 'plan') || 'plan'
    let dir = path.join(TEST_SCRATCH_RUN, planBase, base)
    for (let suffix = 2; fs.existsSync(dir); suffix++) {
        dir = path.join(TEST_SCRATCH_RUN, planBase, `${base}-${suffix}`)
    }
    ensureDir(dir)
    return dir
}

function snapshotCompare(actual, expected, mode) {
    if (mode === 'gir_dot_isomorphic') {
        return compareGirDotIsomorphic(actual, expected)
    }
    return actual === expected ? { ok: true } : { ok: false, message: 'snapshot differs from golden' }
}

function benchmarkFromJson(rawStdout) {
    const payload = JSON.parse(rawStdout)
    if (payload.kind !== 'timeit') {
        throw new Error(`unexpected benchmark kind: ${payload.kind}`)
    }
    return payload
}

function inlineVerifyModuleSource(source) {
    const encodedSource = JSON.stringify(source)
    return `
import fs from 'fs'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message = 'assertion failed') => {
    if (!condition) throw new Error(message)
}
const fail = (message = 'verification failed') => {
    throw new Error(message)
}
const approx = (actual, expected, tolerance = 1e-9) =>
    Math.abs(Number(actual) - Number(expected)) <= tolerance
// ---- the declared error bound, in ulps ----
//
// A transcendental function is not promised to answer the same bits on every device: design
// principles 7.1 promises a written error bound instead, in ulps, declared one function at a time
// (\`stdlib/math.cml\`: "an error of at most 2 ulp ... not the same bits on every device"). An
// assertion on such a number therefore has to be an assertion about that bound -- a bit comparison
// asserts something the language never promised, and registering it as a known failure would hide
// the real errors that come later.
//
// The unit is the last place of the **declared** value, not of the measured one: a 2-ulp promise is
// about the result's own grid, and reading it off the measurement would let a wrong answer move its
// own tolerance. \`bits\` is the precision the answer is declared at -- 53 for a \`float\` (f64), 24
// for an f32 -- because the promise is "of the type's own precision".
const ulpStep = (value, bits = 53) => {
    const magnitude = Math.abs(Number(value))
    if (!Number.isFinite(magnitude) || magnitude === 0) {
        // Not reached for a zero declared value: \`ulp\` compares that exactly, because 7.1 promises
        // zeros (and their signs) exactly. The grid at zero is the subnormal one, whose step is the
        // smallest representable value at this precision.
        return bits >= 53 ? Number.MIN_VALUE : Math.pow(2, -149 + (53 - bits))
    }
    const exponent = Math.floor(Math.log2(magnitude))
    return Math.pow(2, exponent - (bits - 1))
}
const ulpDistance = (actual, expected, bits = 53) => {
    const a = Number(actual)
    const e = Number(expected)
    if (Number.isNaN(a) || Number.isNaN(e)) return Number.NaN
    if (a === e) return 0
    if (!Number.isFinite(a) || !Number.isFinite(e)) return Number.POSITIVE_INFINITY
    return Math.abs(a - e) / ulpStep(e, bits)
}
// \`ulp(measured, declared, bound)\`: true when the two are within \`bound\` units in the last place of
// the declared value. Special values are exact, because 7.1 promises them: a NaN answers a NaN (its
// payload and signalling bit are not part of the value), and +-inf and the two zeros match as they
// are, the sign of a zero included.
const ulp = (actual, expected, bound = 2, bits = 53) => {
    const a = Number(actual)
    const e = Number(expected)
    if (Number.isNaN(e)) return Number.isNaN(a)
    if (!Number.isFinite(e) || e === 0) return Object.is(a, e)
    return ulpDistance(a, e, bits) <= bound
}
const match = (pattern, text = input.normalized.output) => {
    const regex = pattern instanceof RegExp ? pattern : new RegExp(pattern)
    const found = regex.exec(text)
    if (!found) throw new Error(\`pattern not found: \${regex}\`)
    return found
}
const helpers = { input, assert, fail, approx, match, ulp, ulpDistance }
const inlineSource = ${encodedSource}

try {
    const trimmed = inlineSource.trim()
    if (
        trimmed.startsWith('(')
        || trimmed.startsWith('function')
        || trimmed.startsWith('async ')
    ) {
        const verify = (0, eval)(\`(\${inlineSource})\`)
        if (typeof verify !== 'function') {
            throw new Error('verify_script expression did not evaluate to a function')
        }
        await verify(helpers)
    } else {
        await (async () => {
${source}
        })()
    }
} catch (error) {
    console.error(error && error.stack ? error.stack : String(error))
    process.exit(1)
}
`
}

function verificationConfigOf(test) {
    const hasInline = typeof test.verify_script === 'string' && test.verify_script.trim().length > 0
    const hasPath = typeof test.verify_script_path === 'string' && test.verify_script_path.trim().length > 0
    const hasLegacyInline = typeof test.verify_js === 'string' && test.verify_js.trim().length > 0

    if (hasLegacyInline && hasInline) {
        return {
            error: 'verify_js and verify_script cannot both be set',
        }
    }
    if (hasPath && (hasInline || hasLegacyInline)) {
        return {
            error: 'verify_script_path cannot be combined with verify_script or verify_js',
        }
    }
    if (hasInline) {
        return {
            mode: 'inline',
            source: test.verify_script,
        }
    }
    if (hasPath) {
        return {
            mode: 'path',
            source: test.verify_script_path,
        }
    }
    if (hasLegacyInline) {
        return {
            mode: 'inline',
            source: test.verify_js,
        }
    }
    return null
}

async function runVerification(test, result) {
    const config = verificationConfigOf(test)
    if (!config) return null
    // The whole verification is slot-held time: the temporary directory, the payload, the
    // interpreter's startup and its removal. `childMs` is the part a process was actually
    // running in; the rest is the runner's own work.
    const verifyStarted = performance.now()
    const marks = {}
    if (config.error) {
        return {
            ok: false,
            sourceKind: 'config',
            scriptPath: '',
            command: '',
            exitCode: null,
            signal: null,
            timedOut: false,
            errorMessage: config.error,
            stdout: '',
            stderr: '',
            wallMs: performance.now() - verifyStarted,
            childMs: 0,
        }
    }

    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-test-verify-'))
    const setupMs = performance.now() - verifyStarted
    let scriptPath = ''
    let sourceKind = ''
    if (config.mode === 'path') {
        scriptPath = path.resolve(path.dirname(test.__planPath), config.source)
        sourceKind = 'script'
        if (!fs.existsSync(scriptPath)) {
            fs.rmSync(tempDir, { recursive: true, force: true })
            return {
                ok: false,
                sourceKind,
                scriptPath,
                command: '',
                exitCode: null,
                signal: null,
                timedOut: false,
                errorMessage: `verify script does not exist: ${scriptPath}`,
                stdout: '',
                stderr: '',
                wallMs: performance.now() - verifyStarted,
                childMs: 0,
            }
        }
    } else {
        scriptPath = path.join(tempDir, 'verify-inline.mjs')
        sourceKind = 'inline'
        fs.writeFileSync(scriptPath, inlineVerifyModuleSource(config.source))
    }

    const payloadPath = path.join(tempDir, 'input.json')
    // The documented payload fields are listed in docs/test-plans.md; every one of them must be
    // present, and a missing key is silently dropped by `JSON.stringify` (`case_path` was
    // assembled from a result that did not carry it yet, so a script normalizing the absolute
    // source path out of its output could not, and its digest varied with the checkout).
    const payload = {
        repo_root: REPO_ROOT,
        test_root: TEST_ROOT,
        plan_path: test.__planPath,
        test_name: test.name,
        case_path: result.casePath,
        command: result.command,
        exit_code: result.exitCode,
        signal: result.signal,
        timed_out: result.timedOut,
        error_message: result.errorMessage,
        wall_ms: result.wallMs,
        stdout: result.stdout,
        stderr: result.stderr,
        output: result.output,
        normalized: result.normalized,
        diagnostics: result.diagnostics,
        benchmark: result.benchmark,
        // The CPU cost reading, for every test: the figure the test's own process tree was charged,
        // the declared budget if it has one, the line its cost verdict was read against, and the
        // room left under that line. `cpu_ms` used to be the budget's figure only (`null` for every
        // other test), so a script could not see the margin of a test judged by the timeout rule
        // even though the runner had measured it; the three fields beside it are the same ones
        // meta.json records (see `costRecord`).
        cpu_ms: result.cpuMs ?? null,
        cpu_budget_ms: result.cpuBudgetMs ?? null,
        // The sentence that goes with `cpu_ms`: where the figure was read, or why this platform
        // cannot supply one. A script that reports a case's cost reads this before it divides.
        cpu_reading: result.cpuReading ?? null,
        cpu_limit_ms: result.cpuLimitMs ?? null,
        cpu_margin_x: result.cpuMarginX ?? null,
        cpu_margin_basis: result.cpuMarginBasis ?? null,
        test_tmp: result.testTmp,
        test_tmp_relative: result.testTmpRelative,
    }
    fs.writeFileSync(payloadPath, JSON.stringify(payload, null, 2))

    const proc = await runProcess(process.execPath, [scriptPath, payloadPath], {
        cwd: REPO_ROOT,
        env: process.env,
        timeoutMs: (test.verify_timeout_ms || 5000) * TIMEOUT_SCALE,
        marks,
    })
    fs.rmSync(tempDir, { recursive: true, force: true })
    const verifyFinished = performance.now()

    const timedOut = proc.timedOut
    return {
        ok: !timedOut && proc.status === 0,
        sourceKind,
        scriptPath,
        command: `${process.execPath} ${scriptPath} ${payloadPath}`,
        exitCode: typeof proc.status === 'number' ? proc.status : null,
        signal: proc.signal ?? null,
        timedOut,
        errorMessage: proc.error ? proc.error.message : null,
        stdout: proc.stdout || '',
        stderr: proc.stderr || '',
        wallMs: verifyFinished - verifyStarted,
        setupMs,
        spawnMs: marks.spawnMs ?? 0,
        childMs: marks.spawnedAt === undefined ? 0 : (marks.exitedAt ?? verifyFinished) - marks.spawnedAt,
        cleanupMs: verifyFinished - (marks.exitedAt ?? verifyFinished),
    }
}

function verifyFailureLines(verification) {
    const lines = []
    if (!verification) return lines
    if (verification.errorMessage) lines.push(`verification error: ${verification.errorMessage}`)
    if (verification.timedOut) lines.push('verification timed out')
    if (verification.exitCode !== 0) {
        lines.push(`verification exited ${verification.exitCode}`)
    }
    const detail = `${verification.stdout}${verification.stderr}`.trim()
    if (detail) {
        const clipped = detail.length > 2000 ? `${detail.slice(0, 2000)}...` : detail
        lines.push(`verification output: ${clipped}`)
    }
    if (lines.length === 0) {
        lines.push('verification failed')
    }
    return lines
}

/**
 * Where two strings first differ, with the text around the difference. An exact comparison that
 * only said "differs" would send the reader to the log for a trailing space; this names the
 * offset and shows both sides.
 */
function describeTextDifference(expected, actual) {
    const shared = Math.min(expected.length, actual.length)
    let index = 0
    while (index < shared && expected[index] === actual[index]) index++
    const window = (text) => {
        const start = Math.max(0, index - 30)
        const end = Math.min(text.length, index + 70)
        return `${start > 0 ? '...' : ''}${text.slice(start, end)}${end < text.length ? '...' : ''}`
    }
    // A short output is shown whole: the reader should not have to open the log to see the
    // trailing space or the extra line that made the comparison fail.
    const whole = expected.length + actual.length <= 400
    return `first difference at offset ${index} (expected ${expected.length} chars, got ${actual.length} chars): `
        + `expected ${JSON.stringify(whole ? expected : window(expected))}, `
        + `got ${JSON.stringify(whole ? actual : window(actual))}`
}

function compareField(field, actual, baseline) {
    if (field === 'exit_code') return actual.exitCode === baseline.exitCode
    if (field === 'stdout_normalized') return actual.normalized.stdout === baseline.normalized.stdout
    if (field === 'stderr_normalized') return actual.normalized.stderr === baseline.normalized.stderr
    if (field === 'output_normalized') return actual.normalized.output === baseline.normalized.output
    if (field === 'diagnostics') {
        // Name, code and source position of every diagnostic, in the order printed.
        const key = (result) => JSON.stringify(
            result.diagnostics.map((d) => [d.name, d.code, d.line, d.column])
        )
        return key(actual) === key(baseline)
    }
    throw new Error(`unsupported differential field: ${field}`)
}

/**
 * The result a test compares against. Test names are not unique across plans (`shapes.nvm` exists
 * in both `compile.enum` and `para.handoff`), so the plan's own test wins; `compare_to` that names
 * a test of another plan falls back to the name. A serial run's map was overwritten as tests
 * finished, which only gave the right answer because the reference of a plan always ran just
 * before its dependents; a parallel run must not depend on that.
 */
function lookupCompleted(context, name) {
    const scoped = context.planId ? context.completed.get(`${context.planId}/${name}`) : undefined
    return scoped ?? context.completed.get(name)
}

/**
 * The patterns of `expect_stdout_matches`: a string, or a list of them, every one of which has to
 * find a line. The field takes a list for the same reason `expect_stdout_contains` does.
 */
function stdoutMatchPatterns(test) {
    const raw = test.expect_stdout_matches
    if (raw === undefined || raw === null) return []
    if (Array.isArray(raw)) return raw.map((entry) => String(entry))
    return [String(raw)]
}

/**
 * Line by line, because that is what the assertion says: "some line of the output looks like this".
 * Not the whole output at once (then `^` and `$` would anchor the file, not a line, and a plan
 * cannot say "a line that starts with ..."), and not every line (`no test wants that, and one extra
 * printed line would turn it red -- that is a different assertion`).
 */
function matchesAnyLine(regex, text) {
    const lines = String(text).split('\n')
    if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop()
    return lines.some((line) => regex.test(line))
}

/**
 * What a plan gets wrong about its stdout assertions *before* any test runs: a regular expression
 * that does not compile, an empty pattern (it matches everything and would be green forever), and
 * two assertions on the same stdout (`expect_stdout_matches` with `expect_stdout_contains` or
 * `expect_stdout_exact`) -- the last combination has no meaning a reader could guess, so it is
 * refused rather than ordered. These are plan errors, not failing tests: the runner refuses to
 * start, and the message names `plan:line`.
 */
function assertionProblems(test) {
    const problems = []
    const patterns = stdoutMatchPatterns(test)
    if (patterns.length === 0) return problems
    for (const field of ['expect_stdout_contains', 'expect_stdout_exact', 'expect_stdout']) {
        const value = test[field]
        if (value === undefined || value === null) continue
        if (Array.isArray(value) && value.length === 0) continue
        problems.push({
            word: `expect_stdout_matches with ${field}`,
            needle: field,
            reason: 'both assert the same stdout, and which of them wins is not written anywhere: keep one',
        })
        break
    }
    for (const pattern of patterns) {
        if (pattern === '') {
            problems.push({
                word: 'expect_stdout_matches: ""',
                needle: 'expect_stdout_matches',
                reason: 'an empty pattern matches every line, so the assertion would be green forever: write the line you mean',
            })
            continue
        }
        try {
            new RegExp(pattern)
        } catch (error) {
            problems.push({
                word: `expect_stdout_matches: /${pattern}/`,
                needle: pattern,
                reason: `is not a regular expression (${error.message}); it is one pattern a single line must match, not a glob`,
            })
        }
    }
    return problems
}

/**
 * The file `expect_diagnostic_file` names, as the absolute path the comparison below needs.
 *
 * A relative value is read from the directory of the entry's own program -- the place its `import`s
 * resolve from, which is where a module of the case lives -- so a plan writes
 * `expect_diagnostic_file = "plus_c.cml"` next to `case = ".../mods/conflict_import.cml"` and means
 * the `plus_c.cml` that case actually loads. An absolute value is taken as it is, and `${...}` tokens
 * are expanded the way the other assertion strings are (`${test_tmp}` names this test's scratch
 * directory). An entry with no `case` (a `tool` entry) resolves it from the repository root.
 */
function namedDiagnosticFile(test, scope) {
    const text = expandTextTokens(test.expect_diagnostic_file, scope)
    const anchor = typeof test.case === 'string' && test.case.length > 0
        ? path.dirname(entryCasePath(test))
        : REPO_ROOT
    return path.resolve(anchor, text)
}

/**
 * Whether a diagnostic the run reported came from the file the entry requires.
 *
 * The compiler prints the path it was handed; the runner hands it an absolute case path, so the
 * reported path is normally absolute already, and a relative one is resolved from the child's
 * working directory (the repository root). Separators are folded by `path.resolve` and the letter
 * case is folded on Windows, where they name the same file.
 */
function sameDiagnosticFile(reported, wanted) {
    if (typeof reported !== 'string' || reported.length === 0) return false
    const fold = (one) => (IS_WINDOWS ? one.toLowerCase() : one)
    return fold(path.resolve(REPO_ROOT, reported)) === fold(wanted)
}

async function validateTest(test, result, context) {
    const failures = []
    const scope = context.scope || {}
    // A process ends one of two ways, and a plan says which it expects: an exit code, or a signal
    // that killed it. `expect_signal` is the second and it stands in place of the exit assertion
    // rather than joining it, because a killed process has no exit code at all (`exitCode` is null):
    // checking both would fail one crash twice, once for the signal that was expected and once for
    // the exit code that cannot exist. This is the one place a signal decides a verdict, so a plan
    // that expects a crash no longer has to be marked `known_fail` -- a mark that says the run is
    // wrong today, which reads as "the crash is the defect" and turns the entry green again the day
    // the process stops crashing.
    if (test.expect_signal !== undefined) {
        if (signalNumberOf(result.signal) !== expectedSignalNumber(test.expect_signal)) {
            failures.push(`expected signal ${String(test.expect_signal)}, got ${describeExit(result)}`)
        }
    } else if (typeof test.expect_exit === 'number' && result.exitCode !== test.expect_exit) {
        failures.push(`expected exit ${test.expect_exit}, got ${describeExit(result)}`)
    } else if (typeof test.expect_exit !== 'number' && test.expect_exit !== 'script' && !result.timedOut && result.exitCode !== 0) {
        // The default the schema documents -- `expectedSignalNumber` above says it in one sentence,
        // "an entry that says nothing is asserting it" -- was not the default this check applied: it
        // ran only when a plan wrote `expect_exit`, so an entry that forgot the field judged nothing
        // about a crash. Measured before this line existed, on a plan whose case called
        // `process.exit(1)`: `[PASS]`, `total: 1, pass: 1`, run exit 0. A green a crash cannot turn
        // red is worse than a red the machine caused, so an absent field now means zero rather than
        // "no question asked".
        failures.push(
            `exited ${describeExit(result)}, and this entry says nothing about the exit code: an entry ` +
            'expects the ordinary end of a process unless it writes `expect_exit`',
        )
    }
    for (const rawNeedle of test.expect_stdout_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.stdout.includes(needle)) {
            failures.push(`stdout does not contain '${needle}'`)
        }
    }
    // A regular expression is the assertion for a line whose shape is what matters and whose values
    // are not pinned (`^shape \d+ x \d+$`). It reads the output the way `expect_stdout_contains`
    // does -- is there a line like this? -- and it is checked line by line, so `^` and `$` anchor a
    // line and an unanchored pattern is a substring of a line (docs/test-plans.md has both
    // examples). The pattern was compiled at load (a bad one is a plan error, see
    // `assertionProblems`); the failure names the pattern, says the regex is legal, and prints the
    // head of the output, because "did not match" without either is not a reading.
    for (const rawPattern of stdoutMatchPatterns(test)) {
        const pattern = expandTextTokens(rawPattern, scope)
        let regex
        try {
            regex = new RegExp(pattern)
        } catch (error) {
            failures.push(`expect_stdout_matches: /${pattern}/ is not a regular expression (${error.message})`)
            continue
        }
        if (!matchesAnyLine(regex, result.normalized.stdout)) {
            failures.push(`expect_stdout_matches: /${pattern}/ (a legal regular expression) matches no line of the normalized stdout`)
            result.matchDiagnostics = result.matchDiagnostics || []
            result.matchDiagnostics.push({ pattern, stdout: result.normalized.stdout })
        }
    }
    // `expect_stdout` is the legacy name of the same byte-exact comparison: a plan written
    // before `expect_stdout_exact` existed meant the whole output, and the values it holds are
    // whole outputs. Both compare the normalized stdout, as the other expectations do.
    for (const field of ['expect_stdout_exact', 'expect_stdout']) {
        if (typeof test[field] !== 'string') continue
        const expected = expandTextTokens(test[field], scope)
        if (result.normalized.stdout !== expected) {
            failures.push(`${field} differs: ${describeTextDifference(expected, result.normalized.stdout)}`)
        }
    }
    if (typeof test.expect_stdout_starts_with === 'string') {
        const expectedStart = expandTextTokens(test.expect_stdout_starts_with, scope)
        if (!result.normalized.stdout.startsWith(expectedStart)) {
            failures.push(`stdout does not start with ${JSON.stringify(expectedStart)}`)
        }
    }
    // The other direction: a plan that asserts a run says nothing about something (the profiler
    // off is silent, a diagnostic is not printed for a program that has no fault) needs a
    // negative assertion, and a missing one is not the same as a passing one.
    for (const rawNeedle of test.expect_stdout_not_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (result.normalized.stdout.includes(needle)) {
            failures.push(`stdout contains '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_stderr_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.stderr.includes(needle)) {
            failures.push(`stderr does not contain '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_output_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.output.includes(needle)) {
            failures.push(`output does not contain '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_output_not_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (result.normalized.output.includes(needle)) {
            failures.push(`output contains '${needle}'`)
        }
    }
    // The refusal, as the value the site recorded rather than the sentence it printed: each entry
    // names fields of one `READING_REFUSAL` record, and a record has to answer all of them. Nothing
    // here reads the `REINTERP` line -- a rename of that wording is not a change of the transform,
    // and a plan that pinned the wording was red for the rename.
    if (Array.isArray(test.expect_refusal) && test.expect_refusal.length > 0) {
        let records = []
        try {
            records = refusals(result.normalized.stderr)
        } catch (error) {
            failures.push(error.message)
        }
        for (const rawSpec of test.expect_refusal) {
            const spec = parseRefusalSpec(rawSpec)
            if (!records.some((record) => matchesRefusal(record, spec))) {
                failures.push(
                    `no refusal record says '${formatRefusalSpec(spec)}': ` +
                        `the run recorded ${describeRefusals(records)}`
                )
            }
        }
    }
    // "The run records no diagnostic at all" is a claim of its own, and until this field existed a
    // plan had no way to make it: `expect_diagnostic_name = ""` only cancels the name `[defaults]`
    // gave the entry (the block below is gated on truthiness) and asserts nothing. The flag is the
    // stronger claim, so it stands in place of the per-diagnostic expectations rather than joining
    // them -- an entry cannot both require a diagnostic and require none -- and an entry that does
    // not name the field keeps exactly the meaning it had.
    if (test.expect_no_diagnostics === true) {
        if (result.diagnostics.length > 0) {
            const seen = result.diagnostics
                .map((diagnostic) => diagnostic.name || diagnostic.code || diagnostic.message)
                .join('; ')
            failures.push(`expected no diagnostics, got ${result.diagnostics.length}: ${seen}`)
        }
    } else if (
        test.expect_diagnostic_name
        || test.expect_diagnostic_code
        || typeof test.expect_diagnostic_line === 'number'
        || typeof test.expect_diagnostic_column === 'number'
        || typeof test.expect_diagnostic_file === 'string'
    ) {
        // The file the entry requires, when it names one. An entry that does not keeps exactly the
        // meaning it had -- the pin is a name, a code, a line and a column, and any diagnostic the
        // run reported may satisfy it -- because a field that quietly narrowed every existing
        // assertion would turn the plan tree red for writing what it wrote.
        const wantedFile = typeof test.expect_diagnostic_file === 'string'
            ? namedDiagnosticFile(test, scope)
            : null
        const pinsOtherFields = (diagnostic) => {
            if (test.expect_diagnostic_name && diagnostic.name !== test.expect_diagnostic_name) return false
            if (test.expect_diagnostic_code && diagnostic.code !== test.expect_diagnostic_code) return false
            if (typeof test.expect_diagnostic_line === 'number' && diagnostic.line !== test.expect_diagnostic_line) return false
            if (typeof test.expect_diagnostic_column === 'number' && diagnostic.column !== test.expect_diagnostic_column) return false
            return true
        }
        const matched = result.diagnostics.find(
            (diagnostic) => pinsOtherFields(diagnostic)
                && (wantedFile === null || sameDiagnosticFile(diagnostic.path, wantedFile))
        )
        if (!matched) {
            // What was asked, in the order a reader weighs it: what the diagnostic says, then the
            // file it must come from. An entry that named no file produces the message it always
            // did, character for character.
            const asked = [
                test.expect_diagnostic_name || null,
                test.expect_diagnostic_code || null,
                typeof test.expect_diagnostic_line === 'number' ? `line ${test.expect_diagnostic_line}` : null,
                typeof test.expect_diagnostic_column === 'number' ? `column ${test.expect_diagnostic_column}` : null,
            ].filter(Boolean).join(', ')
            const askedIn = wantedFile === null ? '' : ` in ${planLabel(wantedFile)}`
            // The reading that makes a wrong file worth a sentence of its own: the same diagnostic, in
            // the file the run did print it in. Without it the reader sees "no such diagnostic" and
            // has to open the log to learn that it is there, one file over -- which is the reading
            // that cost an evening on `operator_overloads.conflict_import`.
            const reportedElsewhere = wantedFile === null
                ? []
                : result.diagnostics.filter(pinsOtherFields).map((diagnostic) => diagnostic.path)
            const where = reportedElsewhere.length === 0
                ? ''
                : `: that diagnostic is reported in ${[
                    ...new Set(reportedElsewhere.map((one) => planLabel(one))),
                ].join(', ')}, not in the file the entry requires`
            failures.push(`expected diagnostic ${asked}${askedIn}${where}`)
        }
    }
    if (test.kind === 'snapshot') {
        const stream = test.snapshot_stream || 'stdout'
        const actualText = stream === 'stderr' ? result.normalized.stderr : result.normalized.stdout
        const goldenPath = path.resolve(path.dirname(test.__planPath), test.golden)
        if (context.options.updateGolden) {
            ensureDir(path.dirname(goldenPath))
            fs.writeFileSync(goldenPath, actualText)
        }
        if (!fs.existsSync(goldenPath)) {
            failures.push(`golden file does not exist: ${goldenPath}`)
        } else {
            // A golden file is text with LF line ends, also in a checkout that has CRLF (git's
            // core.autocrlf); what the program printed is compared as it is.
            const expected = fs.readFileSync(goldenPath, 'utf8').replace(/\r\n/g, '\n')
            // The recorded side takes the same install root as the read side above (runOneTest):
            // one rule for both halves, so `normalize_paths` cannot normalize one and not the other.
            const expectedText = applyNormalizers(expected, test.normalizers, {
                installRoot: camelInstall(context.options),
            })
            const cmp = snapshotCompare(actualText, expectedText, test.snapshot_compare || 'text_exact')
            if (!cmp.ok) failures.push(cmp.message)
        }
    }
    if (test.kind === 'differential') {
        const base = lookupCompleted(context, test.compare_to)
        if (!base) {
            failures.push(`compare_to target not found: ${test.compare_to}`)
        } else {
            for (const field of test.compare_fields || []) {
                if (!compareField(field, result, base)) {
                    failures.push(`differential field mismatch: ${field}`)
                }
            }
        }
    }
    if (test.__backendReference) {
        // A test with `backends`: every back end after the first must agree with the first on
        // the listed fields.
        const base = lookupCompleted(context, test.__backendReference)
        if (!base) {
            failures.push(`reference back end did not run: ${test.__backendReference}`)
        } else {
            for (const field of test.backends_agree || []) {
                if (!compareField(field, result, base)) {
                    failures.push(`differs from ${test.__backendReference}: ${field}`)
                }
            }
        }
    }
    if (test.kind === 'benchmark') {
        if (!result.benchmark) {
            failures.push('missing benchmark payload')
        } else if (result.benchmark.failed) {
            // `--timeit` reports the iteration that failed, its diagnostics and no mean.
            // That is the test's result; it is reported here instead of the runner throwing on
            // the missing metric.
            const reasons = (result.benchmark.diagnostics || []).map(
                (diagnostic) =>
                    `${diagnostic.data?.name || diagnostic.code || 'diagnostic'}: ${diagnostic.message}`
            )
            const reason =
                reasons.length > 0
                    ? reasons.join('; ')
                    : result.benchmark.failure || 'no reason given'
            failures.push(
                `the timed pass failed in iteration ${result.benchmark.iteration ?? '?'}: ${reason}`
            )
        } else if (!Number.isFinite(result.benchmark.mean_ms)) {
            failures.push('benchmark payload has no mean_ms')
        } else if (typeof test.perf_budget_mean_ms === 'number') {
            const tolerance = Number(test.perf_budget_tolerance_pct || 0)
            const ceiling = test.perf_budget_mean_ms * (1 + tolerance / 100)
            if (result.benchmark.mean_ms > ceiling) {
                failures.push(
                    `benchmark mean ${result.benchmark.mean_ms.toFixed(2)} ms exceeds budget ${test.perf_budget_mean_ms.toFixed(2)} ms (+${tolerance}%)`
                )
            }
        }
    }
    // The test's own process is gone and the slot went back with it. What is left to check is the
    // verification script: a process of its own, so it takes a permit from the verification width
    // instead of a test slot, and the runner never has more than `jobs` of them alive next to the
    // `jobs` tests.
    // Only a test that has a verification script takes a permit: the other 3,747 tests must not
    // queue behind one.
    const verifying = verificationConfigOf(test) !== null
    if (context.clock) context.clock.validationAt = performance.now()
    if (verifying && context.verifications) await context.verifications.acquire(1)
    // Waiting for a permit is the runner's own queue, not the verification: it is measured apart
    // from the script's own time, because a queue that grows here is slot capacity nobody uses.
    if (context.clock) context.clock.verifyPermitAt = performance.now()
    try {
        result.verification = await runVerification(test, result)
    } finally {
        if (verifying && context.verifications) context.verifications.release(1)
    }
    if (context.clock) context.clock.verifyDoneAt = performance.now()
    if (result.verification && !result.verification.ok) {
        failures.push(...verifyFailureLines(result.verification))
    }
    return failures
}

async function runOneTest(test, sharedVars, logContext, scratch, scratchRelative, marks) {
    const casePath = entryCasePath(test)
    if (test.__planError) {
        return runnerErrorResult(casePath, test.__planError)
    }
    const toolName = test.tool || 'camel'
    const toolExe = toolName === 'python'
        ? pythonExecutable()
        : toolName === 'camel'
            ? camelBinary(logContext.options)
            : toolExecutable(toolName, logContext.options.build)
    if (!toolExe) {
        throw new Error(`unknown test tool: ${toolName}`)
    }
    // The scratch directory is created (and removed) by executePlans, before the precondition
    // runs: the precondition writes into it too. `${test_tmp}` in args, env values and
    // expectations is its repository-relative path; CAMEL_TEST_TMPDIR is the absolute one.
    const scope = {
        ...sharedVars,
        case: casePath,
        test_tmp: scratchRelative,
    }
    if (test.__backend) {
        const passes = sharedVars[`run_${test.__backend}`]
        if (passes === undefined) {
            throw new Error(`unknown back end '${test.__backend}' for ${test.name}: no run_${test.__backend} in test/vars.toml`)
        }
        scope.backend = passes
    }
    const args = expandArgs(test.args || [], scope)
    // The build's own notice about `std::jit` (see buildHasNoCodeGenerator) is silenced here unless
    // the entry names it in `silenced_warnings`: an entry that compares back ends or asserts that a
    // run says nothing is asking about the program, and the entry that tests the notice itself is
    // the one that writes `silenced_warnings = []`. `-Wno-...` is scanned out of the whole argv by
    // the command line (`takeWarningArguments`), so its position does not matter.
    const silencedWarnings = test.silenced_warnings ?? ['jit-without-code-generator']
    // Only an entry that runs the compiler takes the flag: a `tool` entry hands its arguments to
    // another program (a script of this repository, the language server, the debugger), which knows
    // nothing of `-Wno-...` and would read it as its own.
    if (
        toolName === 'camel' &&
        silencedWarnings.includes('jit-without-code-generator') &&
        buildHasNoCodeGenerator(logContext.options)
    ) {
        args.unshift('-Wno-jit-without-code-generator')
    }
    // `--compile-only`: the same entry, the same case, the same environment and the same arguments
    // that are not passes -- but the pipeline stops once the program is built. Every word that
    // names a pass is dropped and one compile pass is appended (`std::gir` builds the program and
    // returns an empty graph), so nothing the entry would have executed is executed. This is what
    // makes a case outside the fast tier visible without paying for its run.
    // Only an entry that hands the compiler a case and a pipeline is judged: a tool entry, one that
    // drives the CLI (`--timeit`, `--output-format`), or one whose first word is a flag is asking
    // something else, and a compile of it would answer a question nobody asked.
    const judgedByCompile =
        logContext.options.compileOnly &&
        !test.tool &&
        args.length > 0 &&
        !String(args[0]).startsWith('--') &&
        !args.some((word) => String(word).startsWith('--timeit'))
    if (logContext.options.compileOnly && !judgedByCompile) {
        test.__notJudged = true
        // The second column, said out loud: this entry asks something other than whether a program
        // compiles, so the sweep does not count it either way.
        console.log(`[NOT-JUDGED] ${test.name} (${test.case || test.tool}) -- not a compilation question`)
    }
    if (judgedByCompile) {
        const firstPass = args.findIndex((word) => /^[A-Za-z_][\w:-]*::/.test(word))
        if (firstPass >= 0) args.length = firstPass
        const timeit = args.findIndex((word) => word === '--timeit' || word === '--output-format')
        if (timeit >= 0) args.length = Math.min(args.length, timeit)
        args.push('std::gir')
    }    const env = { ...process.env }
    // Assertions on diagnostics (name, code, line, column) read the one-line text format.
    // A test of the format with source excerpts passes --error-format text, which wins.
    env.CAMEL_ERROR_FORMAT = 'short'
    env.CAMEL_TEST_TMPDIR = scratch
    // Before the test's own `envs`: a value the test writes for itself wins over the default.
    if (pythonEntryPoints(test).tool) hideCudaDevicesFrom(env, test)
    // The card this test was chosen to run on, when it did not name one: before its own `envs`, so
    // a test that writes `CUDA_VISIBLE_DEVICES` itself still wins.
    applyCudaDevice(env, test)
    for (const entry of test.envs || []) {
        const text = expandTextTokens(entry, scope)
        const eq = text.indexOf('=')
        if (eq <= 0) {
            throw new Error(`invalid env entry for ${test.name}: ${text}`)
        }
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    // Every test child reads the standard library and the runtime library of the install this run
    // measures: `CAMEL_HOME` is set to that install's root here, whichever way the install was
    // named. The same root is what `normalize_paths` reads below: it is the directory whose
    // `stdlib/...` a diagnostic prints, and it need not be inside this checkout (a worktree that
    // measures another tree's install, `--camel <other>/out/latest/bin/camel`).
    const installRoot = camelInstall(logContext.options)
    env.CAMEL_HOME = installRoot
    // Windows commonly inherits `Path`; a second `PATH` key would hide the compiler paths.
    const pathKey = IS_WINDOWS ? Object.keys(env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[pathKey] = `${path.join(env.CAMEL_HOME, 'bin')}${path.delimiter}${env[pathKey] || ''}`

    // A second attempt of a case whose first attempt its own bound took gets twice that bound, and
    // nothing else about the attempt differs: same entry, same case, same environment, same
    // assertions. Doubling only -- no floor and no absolute minimum, because a second rule for the
    // same number is a second truth about what the plan asked for. The plan's `timeout_ms` is
    // multiplied, so the Debug factor and `CAMEL_TEST_TIMEOUT_SCALE` apply to both attempts in the
    // same proportion and the second bound is the first one doubled whatever they say.
    const attemptTimeoutMs =
        (test.timeout_ms || 10000) * TIMEOUT_SCALE *
        (logContext.options.build === 'debug' ? DEBUG_BUILD_TIMEOUT_SCALE : 1)
    const timeoutMs = logContext.rerun ? attemptTimeoutMs * RERUN_TIMEOUT_FACTOR : attemptTimeoutMs
    // Every test on a host whose kernel reports CPU time runs under the wrapper that reports its
    // own: the command and its environment are unchanged, the wrapper reaps this test's tree and
    // puts the kernel's figure on a fourth pipe (test/tools/child-cpu.mjs). A test that declares
    // `cpu_budget_ms` needs the figure wherever it runs -- it is judged on it -- so it keeps the
    // wrapper on a host that cannot read one, where the reading comes back as the reason and the
    // test fails for it. The cost of the wrapper for the whole fast layer, measured on the host at
    // -Jobs 8: 2.80 min for smoke + feat against 2.60 min for the same 5,890 tests unwrapped.
    const cpuBudgetMs = typeof test.cpu_budget_ms === 'number' ? test.cpu_budget_ms : null
    const wrapForCpu = CAN_READ_CHILD_CPU || cpuBudgetMs !== null
    const launch = wrapForCpu
        ? { command: process.execPath, args: [CHILD_CPU_TOOL, '--', toolExe, ...args] }
        : { command: toolExe, args }
    // The token sits outermost, so it is held for the whole of the case -- including the CPU wrapper
    // and anything the case itself starts -- and it is the case's own token, not the runner's.
    let tokenReport = null
    let spawned = launch
    if (TOKEN_POOL) {
        tokenSerial += 1
        tokenReport = path.join(os.tmpdir(), `camel-token-${process.pid}-${tokenSerial}.txt`)
        env.CAMEL_TOKEN_REPORT = tokenReport
        spawned = { command: 'bash', args: [HOST_CASE, launch.command, ...launch.args] }
    }
    const proc = await runProcess(spawned.command, spawned.args, {
        // The checkout root, and not the test's scratch directory, because `${test_tmp}` is a
        // *repository-relative* path (docs/test-plans.md, "Test Scratch"): a plan writes
        // `ONNX_OUT=${test_tmp}/m.onnx`, and a program started anywhere but here resolves that one
        // level deeper, under its own cwd. Measured with `cwd: scratch` here: 98
        // failures in `feat/modules` against 3 on the base and every one of the 95 new ones
        // `ENOENT ... /tmp/test/run-<pid>/<plan>/<test>/<file>`, because the model had been written
        // to `<scratch>/tmp/test/...` while the verification script looked for the absolute path.
        // A program that writes a *bare* relative path therefore still writes into the checkout, and
        // the thing that answers for it is the `[TREE]` line at the end of the run
        // (test/tools/tree-guard.mjs): it names the path and the test that was running when it
        // appeared. The one test that did this -- the sweep that runs a compiler over every case in
        // the tree -- starts each of those programs in its own directory instead
        // (scripts/check-plan-refs.mjs).
        cwd: REPO_ROOT,
        env,
        timeoutMs,
        marks,
        cpuReport: wrapForCpu,
    })
    // The test program is gone, so the slot has done its job. Everything left -- resuming from the
    // child's `close` event, normalizing and asserting on what it printed, the verification script,
    // removing the scratch directory -- is the runner's own work. Measured over eight runs of the
    // smoke + feat fast layer at -Jobs 8: 255-532 s of all held slot time had no child in it (a
    // median of 280 s, 28% of the run's slots), and 213-488 s of that was the verification script
    // (1,920 scripts booting node, 88-145 ms each) and the `skip_unless` probes; the runner's own
    // steps in the slot were 2.9-4.4 s together.
    // A `parallel = false` test passes no hook and keeps its slot to the end (see executePlans).
    if (typeof logContext.releaseSlot === 'function') logContext.releaseSlot()
    // How long this case queued for its token, in the pool's own millisecond reading. Absent when the
    // case never got one (it would have exited 6 and this line would not be reached) -- `null` here is
    // "no pool", never "waited zero".
    let tokenWaitMs = null
    if (tokenReport) {
        try {
            const text = fs.existsSync(tokenReport) ? fs.readFileSync(tokenReport, 'utf8') : ''
            fs.rmSync(tokenReport, { force: true })
            const lines = text.split('\n').filter((line) => line.trim() !== '')
            if (lines.length > 0) {
                tokenWaitMs = lines.reduce((sum, line) => sum + Number(line.split(' ')[1] || 0), 0)
                tokenTaking.cases += lines.length
                tokenTaking.waitMs += tokenWaitMs
                // A take that costs a few milliseconds is the pool's own overhead, not a queue: count
                // a case as queued only when it waited long enough to be a wait (100 ms), so the line
                // does not report a queue that nobody stood in.
                if (tokenWaitMs >= 100) tokenTaking.queued += 1
            }
        } catch {
            // A reading that could not be taken is not a case that failed: the token was held either
            // way, and `tokenWaitSeconds` says it did not see this case.
        }
    }
    const cpuReading = wrapForCpu
        ? readCpuReport(proc.cpuReport)
        // Nothing was wrapped, because this host has no reading to take and the test declared no
        // budget: the reason is still recorded, so that `cpu_ms: null` never has to be read as `0`
        // by whoever opens the record later.
        : { cpuMs: null, reason: `no reading was taken: ${NO_CPU_TIME_SKIP}` }
    const raw = {
        command: `${toolExe} ${args.join(' ')}`.trim(),
        tokenWaitMs,
        exitCode: proc.status,
        signal: proc.signal,
        timedOut: proc.timedOut,
        errorMessage: proc.error ? proc.error.message : null,
        wallMs: proc.wallMs,
        stdout: proc.stdout,
        stderr: proc.stderr,
    }
    raw.output = `${raw.stdout}${raw.stderr}`

    const normalized = {
        stdout: applyNormalizers(raw.stdout, test.normalizers, { installRoot }),
        stderr: applyNormalizers(raw.stderr, test.normalizers, { installRoot }),
        output: applyNormalizers(raw.output, test.normalizers, { installRoot }),
    }

    const costFields = costRecord({ cpuMs: cpuReading.cpuMs, timeoutMs, cpuBudgetMs })

    const result = {
        ...raw,
        normalized,
        wallMs: raw.wallMs,
        timeoutMs,
        benchmark: null,
        diagnostics: parseDiagnostics(raw.output),
        testTmp: scratch,
        testTmpRelative: scratchRelative,
        // Set before validation: the verify script payload carries this as `case_path`, and the
        // script that normalizes a source path out of its output needs it.
        casePath,
        // The CPU cost reading, for every test: the figure the wrapper reported (null where this
        // host reports none), the declared budget if there is one, the line the cost verdict was
        // read against and the room left under it (see `costRecord`). meta.json and the verify
        // payload carry all four, so "how far from red" is in the record and not only inferable
        // from a bound somebody has to know to divide by four.
        cpuBudgetMs,
        // `cpuMs` and the sentence that goes with it: the figure the wrapper reported, or the
        // reason this platform has none (`cpuReadingRecord`). meta.json, the verify payload and the
        // per-case timing record all carry the pair, so a null figure always travels with its why.
        ...cpuReadingRecord({
            cpuMs: cpuReading.cpuMs,
            cpuReason: cpuReading.reason,
            canReadCpu: CAN_READ_CHILD_CPU,
        }),
        cpuReason: cpuReading.reason,
        ...costFields,
        // The card the runner chose for this test, if it chose one: which card a GPU test ran on
        // is what makes its result readable later, and it is not derivable from the output.
        cudaDevice: test.__cudaDevice || null,
    }

    if (test.kind === 'benchmark' && raw.stdout) {
        result.benchmark = benchmarkFromJson(raw.stdout)
    }
    // Everything above is the runner turning the child's bytes into a result: resuming from the
    // `close` event, normalizing the two streams, parsing the diagnostics. It all happens on the
    // runner's thread, after the child is gone, while the slot is still held.
    if (logContext.clock) logContext.clock.normalizedAt = performance.now()

    // The verification reads this test's output: executePlans removes the scratch directory
    // only after this returns.
    // A compile-only run has no output to hold the entry's assertions to: what it asks is whether
    // the program builds, and the entry's own expectations -- stdout, exit code, diagnostics -- are
    // the name of a run that did not happen. So the verdict is the compiler's alone, and it names
    // the case that failed to build.
    const expectsRefusal = typeof test.expect_exit === 'number' && test.expect_exit !== 0
    if (test.__notJudged) result.notJudged = true
    // The sweep answers one question, so it has two columns and no third: a program that does not
    // compile is the question, and everything the entry would have judged besides compiling is
    // "not this question" -- never a failure, and never silently dropped either, because a red
    // nobody explains gets read as a defect.
    const failures = logContext.options.compileOnly
        ? test.__notJudged || result.exitCode === 0 || expectsRefusal
            ? []
            : [
                  `does not compile (${test.case}): ` +
                      ((result.output.match(/^error\[[^\n]*/m) || [''])[0] ||
                          describeExit(result)),
              ]
        : await validateTest(test, result, { ...logContext, scope })    // The cost judgement of a test that declares `cpu_budget_ms`, in the unit
    // the rule asks for:
    //
    //   "改量 CPU 时间，不量墙钟。共享主机上的墙钟随负载漂移 ... 进程的 CPU 时间（用户态 + 内核态）
    //    基本不随负载变"
    //
    // So the figure judged here is the CPU time (user + system) the kernel charged the test's own
    // process tree, and a crossed budget is the test's work growing -- never "the host was busy".
    // `timeout_ms` stays, and is what it is for such a test: a liveness bound. A program that stops
    // working crosses the timeout while its CPU budget stands still, and a program that works
    // harder crosses the budget while the wall clock may say anything at all. The reading is in
    // 10 ms ticks (USER_HZ), which is a thousandth of the budget these plans declare.
    if (!logContext.options.compileOnly && cpuBudgetMs !== null && result.timedOut) {
        failures.push(
            `killed at its ${formatDurationMs(timeoutMs)} liveness bound after ` +
                `${formatDurationMs(result.wallMs)}: a cpu_budget_ms test's timeout catches a program ` +
                `that stopped working, and the CPU budget is what catches one that works harder`
        )
    } else if (!logContext.options.compileOnly && cpuBudgetMs !== null) {
        if (result.cpuMs === null && !CAN_READ_CHILD_CPU) {
            // The host reports no CPU figure at all, so this is a fact about the host and not a
            // reading of the test: the same split `timeoutMarginVerdict` makes a few lines below,
            // for the same reason. Before this, a `cpu_budget_ms` entry on such a host was `fail`
            // with no way to become green -- "the runner cannot read a bound" printed as "the test
            // crossed one", which is the two readings a gate must not merge. The test keeps its
            // other verdicts (what it printed, its exit code, its diagnostics); what it loses is
            // the cost judgement, and the run says at the end how many it never judged.
            result.budgetSkipped = NO_CPU_TIME_SKIP
            budgetSkippedTests++
        } else if (result.cpuMs === null) {
            failures.push(
                `cpu_budget_ms ${cpuBudgetMs.toFixed(0)} ms has no CPU reading: ${result.cpuReason} ` +
                    `(a budget the runner cannot read is not a bound; test/tools/child-cpu.mjs reads ` +
                    `a child's CPU from /proc/self/stat and needs Linux)`
            )
        } else if (result.cpuMs > cpuBudgetMs) {
            failures.push(
                `used ${formatDurationMs(result.cpuMs)} of CPU against a cpu_budget_ms of ` +
                    `${formatDurationMs(cpuBudgetMs)}: the test's own work grew (its wall clock read ` +
                    `${formatDurationMs(result.wallMs)}, which is not the bound on this host)`
            )
        }
    }
    // A timeout is a pass condition, so it is only a real bound when the test finishes with room to
    // spare. The room is measured in the work's own currency -- the CPU time the kernel charged this
    // test's process tree -- and not in the wall clock, which on a shared host belongs to whoever
    // else is running. This is the same reading, from the same wrapper, that `cpu_budget_ms` is
    // judged against, and the reason the wrapper runs for every test.
    //
    // What the rule cannot read it does not guess, and it does not borrow the wall clock: on a host
    // with no /proc/self/stat there is no CPU figure for any test, so the margin is skipped there
    // with that reason (NO_CPU_TIME_SKIP, said once at the start and counted at the end) instead of
    // being read off a clock that measures the neighbours. Where the figure is available and a
    // test's reading did not arrive, the test fails rather than passing on a bound whose room
    // nothing examined: a margin that cannot be read is not a margin. A test whose own CPU exceeds
    // its wall clock is multi-threaded work: the reading sums its threads, so this rule reads the
    // work, not the schedule, and such a test states its cost as a `cpu_budget_ms` if its total work
    // is what it wants bounded. Measured over the whole fast layer: 21 of 5,873 tests
    // used more CPU than their wall clock (all ONNX-runtime, dlpack and fuzz cases), and every test
    // in the layer had at least four times its own CPU time of room under its timeout, so no bound
    // in the layer is read differently in a green run than it was before this rule.
    if (failures.length === 0 && cpuBudgetMs === null) {
        const margin = timeoutMarginVerdict({
            cpuMs: cpuReading.cpuMs,
            cpuReason: cpuReading.reason,
            wallMs: result.wallMs,
            timeoutMs,
            canReadCpu: CAN_READ_CHILD_CPU,
        })
        if (margin.failure) failures.push(margin.failure)
        if (margin.skipped) {
            // Recorded on the test, so a reader of this run's records sees that its bound was never
            // judged rather than that it passed a judgement.
            result.marginSkipped = margin.skipped
            marginSkippedTests++
        }
    }
    const status = failures.length === 0 ? 'pass' : 'fail'
    return { ...result, failures, status }
}

/**
 * One test's record. The plan and the case are deliberately not copied in: they used to be, once
 * per test, and nothing read either copy back. `run.json` at the run root carries the revision and
 * the list of plans this run selected, and this test's own `meta.json` carries `plan_path` and
 * `case_path`, so what produced this output is recorded by name rather than by a duplicate of a
 * file the checkout still holds. The copies were also the bulk of a run directory: the same plan
 * file was rewritten once for every test in it. See test/tools/runner-output.mjs, and
 * docs/test-plans.md, "Runner Output".
 */
/** The directory one test's log files go into: `<run>/<attempt>/<status>/<plan>/<test>/`. */
function logPathOf(plan, test, status, logDir, attempt = '') {
    return path.join(logDir, attempt, status, sanitizeSegment(plan.id), sanitizeSegment(test.name))
}

function writeLogFiles(plan, test, result, status, logDir, attempt = '') {
    const statusDir = logPathOf(plan, test, status, logDir, attempt)
    ensureDir(statusDir)
    fs.writeFileSync(path.join(statusDir, 'stdout.txt'), result.stdout)
    fs.writeFileSync(path.join(statusDir, 'stderr.txt'), result.stderr)
    fs.writeFileSync(path.join(statusDir, 'output.txt'), result.output)
    if (result.verification) {
        fs.writeFileSync(path.join(statusDir, 'verify_stdout.txt'), result.verification.stdout)
        fs.writeFileSync(path.join(statusDir, 'verify_stderr.txt'), result.verification.stderr)
    }
    fs.writeFileSync(
        path.join(statusDir, 'meta.json'),
        JSON.stringify(
            {
                plan_id: plan.id,
                test: test.name,
                case_path: result.casePath,
                status,
                failure_note: test.failure_note || null,
                failures: result.failures,
                diagnostics: result.diagnostics,
                perf_budget_platform: test.perf_budget_platform || null,
                metrics: result.benchmark
                    ? {
                        mean_ms: result.benchmark.mean_ms,
                        ci95_ms: result.benchmark.ci95_ms,
                    }
                    : { wall_ms: result.wallMs },
                command: result.command,
                exit_code: result.exitCode,
                signal: result.signal,
                timed_out: result.timedOut,
                error_message: result.errorMessage,
                verification: result.verification
                    ? {
                        source_kind: result.verification.sourceKind,
                        script_path: result.verification.scriptPath,
                        command: result.verification.command,
                        ok: result.verification.ok,
                        exit_code: result.verification.exitCode,
                        signal: result.verification.signal,
                        timed_out: result.verification.timedOut,
                        error_message: result.verification.errorMessage,
                    }
                    : null,
                wall_ms: result.wallMs,
                timeout_ms: result.timeoutMs ?? null,
                // The second attempt of a case whose first attempt timed out, as its own record:
                // which attempt this is, the load it was taken under, and the first attempt's own
                // facts. Nested rather than merged into the fields above, because `timed_out: true`
                // beside this one would otherwise have to mean either "this attempt" or "the case",
                // and a field with two meanings is not a record.
                rerun: result.rerun ?? null,
                // The CPU reading every test records: the figure the kernel charged its own process
                // tree, and the budget it declares if it declares one. The wall clock above is
                // provenance, not the bound.
                cpu_ms: result.cpuMs ?? null,
                cpu_budget_ms: result.cpuBudgetMs ?? null,
                // Where that figure came from, or the sentence saying why this platform has none.
                // Beside `cpu_ms` so that a null is never read as "this case cost nothing": the
                // record answers "nobody can measure it here" in words.
                cpu_reading: result.cpuReading ?? null,
                // The same reading every test now records, with the line it was judged against and
                // how many times it fits under that line: `cpu_limit_ms` is the test's
                // `cpu_budget_ms` when it declares one and `timeout_ms / 4` otherwise (the timeout
                // margin's line), `cpu_margin_basis` says which of the two it is, and
                // `cpu_margin_x` is `cpu_limit_ms / cpu_ms` (1.0 is the line, null where there is
                // no reading or the reading is 0 ms). Read together they answer "how far from red"
                // for a green test, which is what the record could not answer before.
                cpu_limit_ms: result.cpuLimitMs ?? null,
                cpu_margin_x: result.cpuMarginX ?? null,
                cpu_margin_basis: result.cpuMarginBasis ?? null,
                // Set where this host reports no CPU time: the test's timeout margin was not judged
                // at all, and this says why (NO_CPU_TIME_SKIP) instead of leaving a pass that looks
                // like a judged one.
                margin_skipped: result.marginSkipped ?? null,
                // Why there is no CPU figure, when there is none. `cpu_ms: null` on its own reads as
                // "used no CPU", which on a host with no /proc/self/stat is exactly backwards, so the
                // reason is written next to the null instead of being left to a console line.
                cpu_reason: result.cpuReason ?? null,
                // The card this test was given when it did not name one (`cuda_free_device`):
                // ordinal, uuid and the memory reading the choice came from.
                cuda_device: result.cudaDevice ?? null,
                // This test's plan, the plan that ran it. For a `[[sweeps]]` copy these are the
                // sweep's own, and `source_plan` / `source_test` name what was copied.
                plan_path: test.__planPath,
                sweep: test.__sweep ?? null,
                source_plan: test.__sweepSource ? test.__sweepSource.plan_path : null,
                source_plan_id: test.__sweepSource ? test.__sweepSource.plan_id : null,
                source_test: test.__sweepSource ? test.__sweepSource.test : null,
                test_tmp: result.testTmpRelative || null,
            },
            null,
            2
        )
    )
    // The log files are written while no test's slot is held, on the one thread every test shares:
    // `printResult` times the call and records the window, because the cost is paid by whichever
    // slots the write blocked, not by the test whose log it is.
}

/**
/**
 * A test with `backends = ["nvm", "fvm", ...]` is one test per back end, named
 * `<name>.<backend>`, in that order. `${backend}` in its `args` is the pass list `run_<backend>`
 * of test/vars.toml. Every assertion of the entry holds for each of them; in addition, each back
 * end after the first must equal the first on the fields listed in `backends_agree`. A back end
 * listed in `known_fail` is expected to fail (see main).
 *
 * A test with no `backends` list is one run, and its mark is `known_fail = true`. The mark used to
 * be read *after* the early return for that shape, so `known_fail` on such a test did nothing at
 * all: the case failed, the run went red, and the mark was never mentioned. The defect was found
 * and fixed the one case where it showed -- by giving that case a `fvm` back end so the mark
 * applied -- which fixed the symptom; the author's 01:50 ruling asked for the mechanism. Now the
 * shape decides how the mark is written, and a mark written in the other shape's words is refused
 * by name instead of being ignored.
 */
function expandBackends(test) {
    const marked = test.known_fail ?? false
    if (!Array.isArray(test.backends)) {
        if (marked === false || (Array.isArray(marked) && marked.length === 0)) return [test]
        if (marked !== true) {
            throw new Error(
                `known_fail on ${test.name} names back ends (${JSON.stringify(marked)}), but the test has no ` +
                    `backends list, so no back end of that name runs here: write known_fail = true`,
            )
        }
        // The single-run shape pins its fragment the same way the back-end shape below does: this
        // one returned the entry before reading the field, so an entry with `known_fail = true`,
        // no `known_fail_msg` and no `backends` was reported as `known failure changed its mind:
        // expected "undefined"` -- a correct known failure turned into a hard failure by a pin
        // nobody wrote. `docs/test-plans.md` says the field is legal to omit, and this is the line
        // that makes that true for both shapes.
        return [{
            ...test,
            __knownFail: true,
            __knownFailMsg: typeof test.known_fail_msg === 'string' ? test.known_fail_msg : null,
            // The sentence that ends the red, carried out of the plan file so the run's closing
            // roster can print it beside the name. Absent unless the plan writes one, like
            // `__knownFailMsg`: the mechanism grew by pinning cases one at a time, and a mark
            // written before the field existed stays legal and reads as `(no end condition)`.
            __knownFailEndsWhen: knownFailEndCondition(test.known_fail_ends_when),
        }]
    }
    if (marked === true) {
        throw new Error(
            `known_fail on ${test.name} is true, but the test has backends: name the back ends that fail`,
        )
    }
    if (marked !== false && !Array.isArray(marked)) {
        throw new Error(`known_fail on ${test.name} must be true or a list of back ends; it is ${JSON.stringify(marked)}`)
    }
    const knownFail = new Set(marked === false ? [] : marked)
    for (const backend of knownFail) {
        if (!test.backends.includes(backend)) {
            throw new Error(`known_fail names '${backend}', which is not in backends of ${test.name}`)
        }
    }
    return test.backends.map((backend, index) => ({
        ...test,
        name: `${test.name}.${backend}`,
        __backend: backend,
        __backendReference: index === 0 ? null : `${test.name}.${test.backends[0]}`,
        __knownFail: knownFail.has(backend),
        // Absent unless the plan pins one, so every mark written before this field existed stays
        // legal: the mechanism grows by pinning cases one at a time, never by a migration.
        __knownFailMsg: typeof test.known_fail_msg === 'string' ? test.known_fail_msg : null,
        __knownFailEndsWhen: knownFailEndCondition(test.known_fail_ends_when),
    }))
}

/**
 * The tests of one plan file, own and backend-expanded. Exported because the plan inventory
 * (`scripts/plan-inventory.mjs`) must see the same tests this runner selects: two readings of a
 * plan file drift, one of them silently.
 */
export function loadPlanTests(planPath) {
    const doc = parseTomlFile(planPath)
    validatePlanSchema(doc, planPath)
    const planId = doc.id || path.basename(planPath, '.plan.toml')
    const tests = (doc.tests || [])
        .map((test) => ({ ...(doc.defaults || {}), ...test, __planPath: planPath, __planId: planId }))
        .flatMap(expandBackends)
    return { doc, tests }
}

/**
 * A plan the schema rejected. It becomes one failing test of that plan, so the rest of the run
 * still runs and reports; the failure names the plan file, the test and the key. Silently
 * dropping the plan would hide exactly the defect this exists to expose.
 */
function invalidPlan(planPath, error) {
    const id = path.basename(planPath, '.plan.toml')
    return {
        id,
        __planPath: planPath,
        __error: error.message,
        tests: [{
            name: '<invalid plan>',
            kind: 'behavior',
            case: path.basename(planPath),
            args: [],
            __planPath: planPath,
            __planId: id,
            __planError: error.message,
        }],
    }
}

/**
 * A `[[sweeps]]` entry of a plan runs the tests of other plans again under an extra
 * environment, with all their assertions:
 *
 *   [[sweeps]]
 *   name = "poison"                     # named "<name>:<source plan id>:<original name>"
 *   plans = ["../modules", "../linear"] # plan files or directories, relative to this plan
 *   exclude = ["modules/onnx"]          # substrings of plan paths that are left out
 *   args = ["--gc-check", "poison"]
 *   skip_args_prefix = "--gc-"           # leave out tests that already name such an option
 *   timeout_scale = 20
 *
 * It is how a property that every program must keep (here: no raw pointer is held across a
 * safepoint) is checked against whatever the other plans exercise, including tests added
 * later: every test of the `camel` tool is copied, whatever it expects -- a program that runs to
 * completion, a stored picture, or a diagnostic, a refusal or a fault it expects to see. A
 * benchmark (it carries a budget, not a property) and a differential test (it compares two runs to
 * each other, not one run to its assertions) are left out, and so is a test that sets
 * `sweep = false`.
 */
const SWEPT_KINDS = new Set(['behavior', 'snapshot', 'negative'])

/**
 * The plan files one `[[sweeps]]` entry reads: its `plans` entries, minus `exclude`, minus the plan
 * that lists the sweep, one copy each -- a plan named twice (a directory target and a file inside
 * it, say) is one source, because two expansions of it would be two copies with the same name and
 * the same work.
 */
export function sweepSources(sweep, planPath) {
    const exclude = (sweep.exclude || []).map((item) => String(item))
    return [...new Set(sweep.plans
        .flatMap((entry) => walkPlans(path.resolve(path.dirname(planPath), entry)))
        .filter((source) => source !== planPath)
        .filter((source) => !exclude.some((item) => source.split(path.sep).join('/').includes(item))))]
        .sort()
}

/**
 * Why this sweep leaves a source test out, or null when it copies the test. One function for the
 * question, so that the count of what a sweep covers (`negativeSweepAccounting`) cannot answer it
 * differently from what the sweep runs.
 */
function sweepExclusion(sweep, test) {
    if (!SWEPT_KINDS.has(test.kind)) return `kind ${test.kind}`
    if (test.sweep === false) return 'sweep = false'
    if (test.__knownFail) return 'known_fail'
    if ((test.tool || 'camel') !== 'camel') return 'not the camel tool'
    // A sweep that sets its own options skips a source test that already names one of them: the
    // copy would otherwise say --gc-check poison ... --gc-check poison and the parser would see
    // the option twice.
    const own = (test.envs || []).map((entry) => String(entry))
    if (sweep.skip_env_prefix && own.some((entry) => entry.startsWith(sweep.skip_env_prefix))) {
        return 'names the sweep\'s variable'
    }
    const ownArgs = (test.args || []).map((entry) => String(entry))
    if (sweep.skip_args_prefix && ownArgs.some((entry) => entry.startsWith(sweep.skip_args_prefix))) {
        return 'names the sweep\'s option'
    }
    // Options are read by a run: the words go in front of its entry file. A test that asks the
    // compiler (`check`, `inspect`, `--version`) has no run to put them in front of, and they would
    // land on the wrong parser.
    const RUNDRIVING = new Set(["${case}", "--timeit", "--output-format"])
    if ((sweep.args || []).length > 0 && !RUNDRIVING.has(ownArgs[0])) return 'does not drive a run'
    return null
}

export function expandSweeps(doc, planPath) {
    const out = []
    for (const sweep of doc.sweeps || []) {
        if (!sweep.name || !Array.isArray(sweep.plans)) {
            throw new Error(`a [[sweeps]] entry of ${planPath} needs a name and plans`)
        }
        const scale = Number.isFinite(sweep.timeout_scale) ? sweep.timeout_scale : 1
        // `fast_sample = N` keeps N tests per source plan in the fast layer and puts the rest in
        // the milestone layer: the default developer run still checks the property against every
        // plan, and the full sweep (every eligible test) is `--tier milestone` away. The
        // sample prefers a test that runs the program to completion, since that is the one that
        // exercises the most of the collector; a plan of negative tests still contributes one.
        //
        // The sample is risk-based: a source plan whose `tags` contain one of `risk_tags` keeps
        // `risk_sample` tests (collections, closures and iterators move objects across
        // safepoints in more ways, so one sample is thin there), the rest keep `fast_sample`.
        const fastSample = Number.isInteger(sweep.fast_sample) && sweep.fast_sample > 0 ? sweep.fast_sample : 0
        const riskTags = new Set((sweep.risk_tags || []).map((tag) => String(tag)))
        const riskSample = Number.isInteger(sweep.risk_sample) && sweep.risk_sample > 0 ? sweep.risk_sample : 0
        for (const source of sweepSources(sweep, planPath)) {
            const { doc: sourceDoc, tests: sourceTests } = loadPlanTests(source)
            const sourceDir = path.dirname(source)
            const sweepId = doc.id || path.basename(planPath, '.plan.toml')
            // A copy is named after the sweep, its source plan and the source test. The source
            // plan is part of the name because two plans may both have a test called `shapes`
            // without it the copies collide, and a back end waits for, and compares
            // against, whichever copy of its reference ran last. `id` is the plan's stable
            // identifier, unique across the run; the fallback matches loadPlanTests.
            const sourceId = sourceDoc.id || path.basename(source, '.plan.toml')
            const rename = (name) => `${sweep.name}:${sourceId}:${name}`
            const risky = (sourceDoc.tags || []).some((tag) => riskTags.has(String(tag)))
            const sampleCount = fastSample > 0 && risky && riskSample > fastSample ? riskSample : fastSample
            const eligible = []
            for (const test of sourceTests) {
                if (sweepExclusion(sweep, test) === null) eligible.push(test)
            }
            const sampled = new Set()
            if (sampleCount > 0) {
                const preferred = eligible.filter(
                    (test) => (test.expect_exit === undefined || test.expect_exit === 0) &&
                        test.expect_signal === undefined &&
                        test.tier !== 'slow' && test.tier !== 'milestone'
                )
                for (const test of [...preferred, ...eligible]) {
                    if (sampled.size >= sampleCount) break
                    sampled.add(test)
                }
            }
            for (const test of eligible) {
                const own = (test.envs || []).map((entry) => String(entry))
                out.push({
                    ...test,
                    name: rename(test.name),
                    envs: [...own, ...(sweep.envs || [])],
                    args: [...(sweep.args || []), ...(test.args || [])],
                    timeout_ms: (test.timeout_ms || 10000) * scale,
                    verify_timeout_ms: (test.verify_timeout_ms || 5000) * scale,
                    __backendReference: test.__backendReference ? rename(test.__backendReference) : null,
                    __sweep: sweep.name,
                    // A copy belongs to the plan that runs it, not to the plan it was copied from.
                    // Before this, a copy's `meta.json` named two different plans: `plan_id` was the
                    // sweep's (from the plan the entry lives under) and `plan_path` was the source's
                    // (`...test` carried the source's `__planPath` through), and every reader that
                    // aggregated cost by `plan_path` charged the copy to the source plan. Nothing is
                    // lost by owning it: the source stays in the copy's name
                    // (`<sweep>:<source plan id>:<source test>`) and in `__sweepSource`.
                    __planPath: planPath,
                    __planId: sweepId,
                    __sweepSource: { plan_path: source, plan_id: sourceId, test: test.name },
                    // A path in an entry is relative to the plan file that declares it, so the
                    // copied one is resolved here, against the source plan, while that is still
                    // known. An absolute path resolves to itself, which is why every other reader
                    // can keep resolving `case` against `__planPath`.
                    ...(typeof test.case === 'string' ? { case: path.resolve(sourceDir, test.case) } : {}),
                    ...(typeof test.golden === 'string' ? { golden: path.resolve(sourceDir, test.golden) } : {}),
                    ...(typeof test.verify_script_path === 'string'
                        ? { verify_script_path: path.resolve(sourceDir, test.verify_script_path) }
                        : {}),
                    ...(sampleCount > 0 ? { tier: sampled.has(test) ? 'fast' : 'milestone' } : {}),
                })
            }
        }
    }
    return out
}

/**
 * What the tree's `[[sweeps]]` entries cover and what they leave out, counted through the same two
 * functions the expansion uses (`sweepSources`, `sweepExclusion`): `declared` `negative` entries in
 * the source plans of a sweep, `swept` copies of them, `excluded` the rest, and the reason each
 * excluded one fell out. `reason` says which reading the code implements -- `a` when a `negative`
 * entry is copied like any other test, `b` when its kind is left out -- so that the coverage a
 * comment claims and the coverage the code has cannot move apart without this line moving.
 *
 * The line is a reading and not a decision: the runner prints it from its self-test and asserts
 * nothing about the numbers, only that they are printed. It exists because the two did move apart
 * once: `expandSweeps` left `negative` out from the sweep's first commit while its comment named
 * only benchmarks and differential tests as left out, so 1258 entries of the sweep's source plans
 * were declared `negative` and never copied, and no count, summary or gate line said so -- the
 * missing copies were green because they had not run.
 */
export function negativeSweepAccounting(planPaths) {
    const counts = {
        declared: 0,
        swept: 0,
        reasons: new Map(),
        reason: SWEPT_KINDS.has('negative') ? 'a' : 'b',
    }
    for (const planPath of planPaths) {
        const doc = loadPlanTests(planPath).doc
        for (const sweep of doc.sweeps || []) {
            if (!sweep.name || !Array.isArray(sweep.plans)) continue
            for (const source of sweepSources(sweep, planPath)) {
                for (const test of loadPlanTests(source).tests) {
                    if (test.kind !== 'negative') continue
                    counts.declared += 1
                    const why = sweepExclusion(sweep, test)
                    if (why === null) counts.swept += 1
                    else counts.reasons.set(why, (counts.reasons.get(why) || 0) + 1)
                }
            }
        }
    }
    return counts
}

/** The reading as one line, then the reasons the rest of them are not swept, one per kind. */
export function negativeSweepLine(counts) {
    const line = `negative-sweeps declared=${counts.declared} swept=${counts.swept} `
        + `excluded=${counts.declared - counts.swept} reason=${counts.reason}`
    const reasons = [...counts.reasons.entries()].sort()
    if (reasons.length === 0) return line
    return `${line}\n  negative-sweeps: not swept -- ${reasons.map(([why, n]) => `${n} ${why}`).join(', ')}`
}

/**
 * The layer a plan's tests land in when the test says nothing: the plan's own `tier` when it names
 * one of the four, `fast` otherwise (`docs/test-plans.md`: "any `tier` other than `slow`,
 * `milestone` or `debug` is the fast layer" -- the old `smoke`/`feat`/`benchmark` labels included,
 * so a plan written before the split still runs by default).
 *
 * One function, because this question used to be answered in two places and the second answer was
 * different. Selection went through `testTier`, which normalises; the per-plan accounting read
 * `plan.tier ?? 'fast'` raw, so a plan labelled `feat` was filed under a layer no run covers
 * (`coveredTiers` never returns it) and its emptiness was excused as "another layer's plan" instead
 * of reported as a plan this layer started nothing in. Six plans in this tree carry `tier = "feat"`;
 * their 96 tests have always run fast, and the accounting is the only reading that said otherwise.
 * `scripts/tier-report.mjs` prints how many entries reached a layer by which of these answers, so
 * "fast" no longer hides whether it was decided or defaulted (iron rule 6).
 *
 * Exported because the plan inventory (`scripts/plan-inventory.mjs`) and the layer report ask the
 * same question: a second copy of this rule would drift from it.
 */
export function planLayer(plan) {
    const tier = plan.tier
    if (tier === 'fast' || tier === 'slow' || tier === 'milestone' || tier === 'debug') return tier
    // Reached by a plan with no `tier` and by one whose value is not a layer name. Both are the fast
    // layer by rule; neither is a fact anyone can read off the file, which is why the report counts
    // them apart.
    return 'fast'
}

/**
 * A test's layer: `tier` on the test first, then its plan (`planLayer`). `milestone` is the layer
 * that only runs at a milestone, either because the plan file says so or because a sweep left the
 * copy unsampled. `debug` is a further layer selected only by `--tier debug`: a test that aborts
 * when an assertion fires, or that is only meaningful with assertions compiled in, lives there and
 * is never part of an ordinary or `all` run.
 *
 * A test's `tier` is strict: a value that is not one of the four is an error here, not a silent
 * `fast`, because the test said which layer it wants and the runner cannot honour it.
 */
export function testTier(plan, test) {
    const tier = test.tier !== undefined ? test.tier : planLayer(plan)
    if (tier !== 'fast' && tier !== 'slow' && tier !== 'milestone' && tier !== 'debug') {
        throw new Error(`test '${test.name}' of ${plan.id || plan.__planPath}: tier must be 'fast', 'slow', 'milestone' or 'debug'`)
    }
    return tier
}

/**
 * Select the tests of a layer. A differential test or a back end that compares against an earlier
 * test needs that test's result, so the earlier test is kept even when its own layer is the other
 * one: a fast run must not fail because it left out the reference a selected test compares
 * against.
 *
 * `debug` is a plan-set layer: `debug_tier = true` on a plan selects its tests on a Debug build
 * while leaving their ordinary layer alone -- 22 plans, 319 tests, which run fast in Release and
 * again under `--tier debug`. The entries whose own layer *is* `debug` (35, all from plans whose
 * `tier = "debug"`; no test in this tree sets `tier = "debug"` itself) belong to no other layer.
 * `all` therefore leaves the debug layer out: it runs on the Release binary, where the abort case
 * would not abort.
 */
// The layers a run of `tier` claims to cover: a plan of one of them that starts nothing in this run
// is a failure, and a plan of another layer is expected to be empty here. `compileOnly` covers the
// non-fast entries wherever they live, so no plan is "expected empty" in that mode.
function coveredTiers(tier, compileOnly = false) {
    if (compileOnly) return new Set(['fast', 'slow', 'milestone', 'debug'])
    if (tier === 'all') return new Set(['fast', 'slow', 'milestone'])
    if (tier === 'debug') return new Set(['debug'])
    return new Set([tier])
}

// A plan path as a reader names it: relative to the repository root, forward slashes.
function relPath(abs) {
    return path.relative(path.resolve(TEST_ROOT, '..'), abs).split(path.sep).join('/')
}
function selectTier(plans, tier, namedPlans = new Set(), compileOnly = false, keep = null) {
    const order = []
    for (const plan of plans) {
        for (const test of plan.tests) order.push({ plan, test })
    }
    // `keep` is a recorded case selection (`--selection-cases`): a test outside it is not selected
    // by anything, whatever layer it belongs to. It is applied beside the layer rule instead of
    // after it, so the accounting below counts what this run starts: a narrowed plan whose entries
    // were all left out is not a plan that "began no test of its own layer" -- it was not selected.
    const keptBySelection = (plan, test) => keep === null || keep(listedName(plan, test))
    const wants = (plan, test) => {
        // A plan the schema rejected fails in every layer: its tests never ran, so no layer can
        // honestly say the plan is clean.
        if (test.__planError) return true
        // A compile-only run asks only about the entries the fast layer does not run: whatever the
        // fast layer runs is compiled there already, and compiling it a second time is paying twice
        // for the same answer. What this sweep buys is the part nobody looks at.
        if (compileOnly) return testTier(plan, test) !== 'fast'
        if (tier === 'all') return testTier(plan, test) !== 'debug'
        if (tier === 'debug') return plan.debug_tier === true || testTier(plan, test) === 'debug'
        return testTier(plan, test) === tier
    }
    const indexOfName = new Map()
    const indexOfPlanName = new Map()
    order.forEach((entry, index) => {
        if (!indexOfName.has(entry.test.name)) indexOfName.set(entry.test.name, index)
        indexOfPlanName.set(`${entry.plan.id}/${entry.test.name}`, index)
    })
    const selected = new Set()
    order.forEach((entry, index) => {
        // A plan file named as a target runs in full: naming the plan is the intent, and a plan
        // whose tests are mostly slow would otherwise run almost nothing. A directory target
        // still selects the layer, so the whole-tree `--tier` stays meaningful.
        if ((namedPlans.has(entry.plan.__planPath) || wants(entry.plan, entry.test)) && keptBySelection(entry.plan, entry.test)) {
            selected.add(index)
        }
    })
    const pending = [...selected]
    while (pending.length > 0) {
        const entry = order[pending.pop()]
        const test = entry.test
        const references = []
        if (test.kind === 'differential' && test.compare_to) references.push(test.compare_to)
        if (test.__backendReference) references.push(test.__backendReference)
        for (const reference of references) {
            const index = indexOfPlanName.get(`${entry.plan.id}/${reference}`) ?? indexOfName.get(reference)
            if (index !== undefined && !selected.has(index)) {
                selected.add(index)
                pending.push(index)
            }
        }
    }
    const kept = new Set(selected)
    const selectedPlans = []
    // Declared against started, per plan, for the layer this run covers. Both are counted from the
    // loaded plans -- `declared` the entries whose own tier is this layer's, `started` the ones the
    // run launches (companions included). Never computed from `backends.length`: a swept copy
    // carries the source's list, and the multiplication reads 7339 for a layer that starts 3404.
    const tierAccounting = []
    // A plan the run reaches but starts nothing in is not a pass: nothing about it was read. Two
    // shapes must not be confused, and the author's 01:30 ruling asks for the difference to be a
    // rule rather than a habit: a plan whose own tier is another layer is *expected* to contribute
    // nothing here (the milestone layer under a fast run), and is counted so the zero is explained
    // instead of silent; a plan whose own tier is the layer this run covers and starts nothing is a
    // failure, because a plan of this layer that gives this layer no test is in the run by name
    // only. A plan that declares no test at all is a failure in every layer.
    const covered = coveredTiers(tier, compileOnly)
    const emptyByLayer = []
    const unproven = []
    let cursor = 0
    for (const plan of plans) {
        const tests = []
        for (const test of plan.tests) {
            if (kept.has(cursor)) tests.push(test)
            cursor++
        }
        if (tests.length > 0) {
            selectedPlans.push({ ...plan, tests })
            let declaredHere = 0
            for (const test of plan.tests) if (wants(plan, test) && keptBySelection(plan, test)) declaredHere++
            tierAccounting.push({
                plan: relPath(plan.__planPath),
                declared: declaredHere,
                started: tests.length,
            })
            continue
        }
        const key = relPath(plan.__planPath)
        const declared = plan.tests.length
        // Through the same rule selection uses. Read raw, a plan labelled `feat` was filed under a
        // layer `covered` never holds, so the plan's own emptiness was reported as "another layer's
        // plan" -- an explanation that is always available and therefore checks nothing.
        const own = plan.__planError ? 'failing' : planLayer(plan)
        if (plan.__planError) {
            // A schema error already fails this plan; it is counted here as read, not as unproven.
            emptyByLayer.push(plan.__planPath)
        } else if (declared === 0) {
            unproven.push(`${key}: the plan declares no test at all, so this run proves nothing about it`)
        } else if (keep !== null) {
            // A plan this recorded selection names no case of is not a plan that started nothing:
            // the selection left its entries out on purpose, and the count of what the change does
            // touch is the reading, not a zero to explain.
            emptyByLayer.push(plan.__planPath)
        } else if (covered.has(own)) {
            unproven.push(
                `${key}: its own tier is '${own}', the layer this run covers, and it declares ` +
                    `${declared} test(s) none of which this run selects -- a plan that starts no test ` +
                    'proves nothing',
            )
        } else {
            emptyByLayer.push(plan.__planPath)
        }
    }
    // The options a plan writes are the CLI's words, and a value the CLI does not take is a *plan*
    // error, not a red test: the CLI refuses it while parsing, so the test would fail with an exit
    // code that says nothing about the program. Six entries of `jit_framed` were read that way for
    // a night (`--gc-check verify=1`, diagnosed as a GC defect). The domains live in
    // `cli-arg-domains.mjs`, which checks itself against `src/tools/camel-cli/config.cpp`.
    //
    // The same shape, one field over: a regex that does not compile, an empty pattern, and two
    // assertions on one stdout are plan errors too (`assertionProblems`), found here so a run
    // refuses instead of starting a test whose verdict is already meaningless.
    const argProblemsOfPlans = []
    const reportedArgs = new Set()
    for (const plan of selectedPlans) {
        for (const test of plan.tests) {
            const found = [
                ...argProblems(test.args),
                ...assertionProblems(test),
            ]
            for (const problem of found) {
                const line = planLineOf(plan.__planPath, problem.needle ?? problem.word) ?? planLineOf(plan.__planPath, problem.option)
                const word = problem.option ? `${problem.option} ${problem.word}` : problem.word
                const key = `${plan.__planPath}|${line}|${word}`
                if (reportedArgs.has(key)) continue
                reportedArgs.add(key)
                argProblemsOfPlans.push({ planPath: plan.__planPath, line, testName: test.name, word, reason: problem.reason })
            }
        }
    }
    return { plans: selectedPlans, emptyByLayer, unproven, argProblems: argProblemsOfPlans, tierAccounting, tier }
}

/**
 * The line of a plan file where a word is written, so a plan error can be reported as
 * `plan:line`. The word came from the file, so it is in there; a word assembled by the runner
 * (an expanded `${case}`, say) is not, and then the caller falls back to the option's line.
 */
const PLAN_TEXT = new Map()
function planLineOf(planPath, needle) {
    if (!needle) return null
    let lines = PLAN_TEXT.get(planPath)
    if (!lines) {
        try {
            lines = fs.readFileSync(planPath, 'utf8').split(/\r?\n/)
        } catch {
            return null
        }
        PLAN_TEXT.set(planPath, lines)
    }
    const index = lines.findIndex((line) => line.includes(needle))
    return index === -1 ? null : index + 1
}

/**
 * The plans a selection resolves to, with every test the layer keeps from them. Exported because
 * the coverage table (`scripts/coverage-table.mjs`) must count the cases a selection selects
 * through this loader: a second definition of "which cases are in this plan" -- entries only, no
 * `[[sweeps]]` copies, no back-end expansion, no companion closure -- would agree with a run today
 * and disagree the first time a plan learned a sweep.
 */
export function loadPlans(targets, tier, layeredPlans = new Set(), compileOnly = false, emptySelection = false, keep = null) {
    // A plan file named on the command line (or by `--changed`'s self rule) runs in full; the
    // plans a directory reaches follow the layer. The same plan reached both ways is named,
    // unless `--changed` marked it `layer: true`: then the tier filter applies to it as well.
    const namedPlans = new Set()
    const discovered = []
    // A target that reaches no plan file named nothing: it is neither a layer question nor a plan
    // with a schema error. Kept apart so the callers can refuse with that sentence -- the layer
    // one below (`none of their entries is in this layer`) would be a wrong answer for a path that
    // holds no plan at all, and it is the answer a fixture directory gets: `test/plans/feat/docs_links`
    // is the directory beside `docs_links.plan.toml`, not a plan directory.
    const reachedNothing = []
    for (const target of targets) {
        const resolved = path.resolve(target)
        const found = walkPlans(target)
        if (found.length === 0) reachedNothing.push(target)
        if (fs.statSync(resolved).isFile() && !layeredPlans.has(resolved)) namedPlans.add(resolved)
        discovered.push(...found)
    }
    // No target is the whole plan tree -- that is what `node scripts/test.js` with no argument means,
    // and what `npm run test` relies on. The one caller that means the opposite says so;
    // `emptySelection` is a selection that was measured empty (0 changed files), so widening it here
    // is exactly the defect this flag exists to stop: 0 changed files used to start 336 plan files.
    if (targets.length === 0 && !emptySelection) discovered.push(...walkPlans(PLANS_ROOT))
    // `--all` names the plans root and a plan may also be named explicitly; the same plan must
    // not run twice.
    const planPaths = [...new Set(discovered.map((planPath) => path.resolve(planPath)))]
    const plans = planPaths
        .map((planPath) => {
            try {
                const { doc, tests: ownTests } = loadPlanTests(planPath)
                const tests = [...ownTests, ...expandSweeps(doc, planPath)]
                // The name a plan is recorded under, with the same fallback `loadPlanTests` gives its
                // tests. Fourteen plan files declare no `id`, and every record that read
                // `plan: entry.plan.id` then lost the key entirely (`JSON.stringify` drops an
                // undefined value), so 303 tests of those plans sat in summary.json belonging to no
                // plan, and their log directory was literally `undefined/`. A plan with no declared
                // id is now named after its file, which is what every other reader already assumed.
                const planId = doc.id || path.basename(planPath, '.plan.toml')
                return { ...doc, id: planId, __planPath: planPath, tests }
            } catch (error) {
                // A schema error is a property of this plan file: fail this plan and go on. Any
                // other error is a defect of the runner or of a sweep target, and stays fatal.
                if (!(error instanceof PlanSchemaError)) throw error
                return invalidPlan(planPath, error)
            }
        })
        .sort((lhs, rhs) => {
            const lhsOrder = Number.isFinite(lhs.order) ? lhs.order : Number.MAX_SAFE_INTEGER
            const rhsOrder = Number.isFinite(rhs.order) ? rhs.order : Number.MAX_SAFE_INTEGER
            if (lhsOrder !== rhsOrder) return lhsOrder - rhsOrder
            return lhs.__planPath.localeCompare(rhs.__planPath)
        })
    // `reachedButFiltered` is how many plan files this selection read, before the layer kept
    // anything from them. A run that starts nothing has to say which of the two zeros it is -- no
    // plan file at all (refused above, target by target) or plan files whose entries belong to
    // another layer -- and the count that separates them is the one before the filter, not after
    // it: `plans` is the post-filter list and reads 0 in that message's own case.
    return {
        ...selectTier(plans, tier, namedPlans, compileOnly === true, keep),
        reachedNothing,
        reachedButFiltered: plans.length,
    }
}

/**
 * One row per case a selection would run: `{name, plan, case, tier, refs}`. `name` is the
 * `<plan id>/<name>` pair `--list-known` prints, so a listed case can be compared with a recorded
 * one without a translation table. `case` is the entry's input path (repository-relative) when it
 * has one -- the thing a case-level selection matches a changed file against; `refs` are the
 * companions the run pulls in for a differential or back-end entry, so a narrower selection can
 * keep its closure without a second copy of that rule.
 */
function caseRows(selected) {
    const rows = new Map()
    for (const plan of selected.plans) {
        for (const test of plan.tests) {
            const planId = test.__planId || plan.id || path.basename(plan.__planPath, '.plan.toml')
            const name = listedName(plan, test)
            if (rows.has(name)) continue
            const refs = []
            if (test.kind === 'differential' && test.compare_to) refs.push(`${planId}/${test.compare_to}`)
            if (test.__backendReference) refs.push(`${planId}/${test.__backendReference}`)
            const input = test.case ? path.resolve(path.dirname(plan.__planPath), test.case) : null
            rows.set(name, {
                name,
                plan: relPath(plan.__planPath),
                case: input ? relPath(input) : null,
                tier: testTier(plan, test),
                refs: [...new Set(refs)],
            })
        }
    }
    return [...rows.values()].sort((lhs, rhs) => lhs.name.localeCompare(rhs.name))
}

/**
 * One entry of a record's `cases`: the case's name.
 *
 * `scripts/selection-cases.mjs` writes `{ name, cause }` per case -- the cause is what says why the
 * change selected it, and it is the reading the record exists for. Reading such an entry as a name
 * (`String(entry)`) makes every one of them the single string `[object Object]`, which the set then
 * de-duplicates into exactly one name: the run is told it must start a case called [object Object],
 * refuses with `[PLAN ERROR] ... names 1 case(s) this run would not start`, and starts nothing. Every
 * merge that handed its record to `--selection-cases` read exit 2 and 0 rows that way, and the
 * message blamed "a stale record or another tree".
 *
 * So an entry is its name, or an object that carries one; anything else is refused **by its shape**
 * rather than coerced, because a record whose entries this run cannot read is a record it cannot
 * execute, and a coerced name is a silent one.
 */
function selectionCaseName(entry) {
    if (typeof entry === 'string' && entry.length > 0) return entry
    if (
        entry !== null &&
        typeof entry === 'object' &&
        !Array.isArray(entry) &&
        typeof entry.name === 'string' &&
        entry.name.length > 0
    ) {
        return entry.name
    }
    const shape = entry === null
        ? 'null'
        : Array.isArray(entry)
          ? 'an array'
          : typeof entry === 'object'
            ? `an object whose keys are [${Object.keys(entry).join(', ')}]`
            : `a ${typeof entry}`
    throw new Error(
        `--selection-cases: a case entry in the record is ${shape}, and this run cannot read a name ` +
            `out of it. A case is its name, or an object that carries one -- a selection record writes ` +
            `{"name": ..., "cause": ...}. Coercing the entry instead is how every recorded case became ` +
            `the one name "[object Object]".`
    )
}

/**
 * A refusal that is a plan error, not a crash: `main` prints one `[PLAN ERROR]` line and exits 2.
 * A selection this run cannot read is not measured -- nothing starts -- and 2 is the code this
 * runner already gives "not measured" (a missing install, an unreadable base). A throw would exit 1,
 * which says a test failed: the wrong answer, because no test ran.
 */
function selectionRefusal(message) {
    const error = new Error(message)
    error.planRefusal = true
    return error
}

/**
 * `--selection-cases`: the names a run must execute. An empty or unreadable list is refused rather
 * than read as "no narrowing": the two mean opposite things, and only one of them is a selection.
 *
 * "Empty" has two causes that must not read the same: a selection that names nothing (refused -- it
 * must not become a plan expansion), and a record that kept a count and a digest instead of its rows
 * because the list was too long to store. The second one cannot narrow a run at all, so it is named
 * as the digest it is rather than reported as "names no case".
 *
 * The record's own `plans` travel with the names: they are the targets the engine computed the list
 * for (`scripts/selection-cases.mjs --targets`), and they are what says whether the record describes
 * this run or another one (`refuseRecordThatNarrowsLayer`).
 */
function readCaseSelection(file) {
    if (!fs.existsSync(file)) throw selectionRefusal(`--selection-cases: ${file} does not exist`)
    let doc
    try {
        doc = JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (error) {
        throw selectionRefusal(`--selection-cases: ${file} is not JSON (${error.message})`)
    }
    const names = Array.isArray(doc) ? doc : doc && doc.cases
    if (!Array.isArray(names)) {
        throw selectionRefusal(
            `--selection-cases: ${file} carries no 'cases' array; a selection record writes one ` +
                '(scripts/selection-cases.mjs --names writes the rows a run executes)'
        )
    }
    if (names.length === 0) {
        if (doc && doc.cases_recorded === false) {
            throw selectionRefusal(
                `--selection-cases: ${file} keeps ${doc.case_count} case(s) as ${doc.case_digest} and ` +
                    `not as rows, so it cannot name what a run must start. Write the run's list with ` +
                    'scripts/selection-cases.mjs --names, or replay this record with --verify.'
            )
        }
        throw selectionRefusal(
            `--selection-cases: ${file} names no case; an empty selection must not become a plan expansion`
        )
    }
    return {
        names: new Set(names.map(selectionCaseName)),
        doc: doc && !Array.isArray(doc) ? doc : {},
        file,
    }
}

/**
 * The names a recorded selection carries that this run would not start: a stale record, a record
 * taken at another tier, or a case another lane removed. Empty means the run is exactly the list.
 */
function missingCaseNames(plans, wantedNames) {
    if (wantedNames === null) return []
    const started = new Set()
    for (const plan of plans) for (const test of plan.tests) started.add(listedName(plan, test))
    return [...wantedNames].filter((name) => !started.has(name))
}

/** The targets a record was computed for, in the form `scripts/selection-cases.mjs` writes them. */
function recordedTargets(record) {
    const plans = record && Array.isArray(record.plans) ? record.plans : []
    const seen = new Set()
    for (const plan of plans) {
        const text = String(plan).replace(/\\/g, '/').replace(/\/+$/, '')
        if (text) seen.add(path.posix.normalize(text))
    }
    return [...seen].sort()
}

/** The targets this run names, in the same form. No target is the whole plan tree. */
function runTargets(options) {
    const named = options.targets.length > 0 ? options.targets : [PLANS_ROOT]
    const seen = new Set()
    for (const target of named) seen.add(relPath(path.resolve(target)).replace(/\/+$/, ''))
    return [...seen].sort()
}

/** Whether this run names the whole plan tree and nothing else: what `--all` and `-Plan all` name. */
function namesWholePlanTree(options) {
    const targets = runTargets(options)
    return targets.length === 1 && targets[0] === relPath(PLANS_ROOT)
}

/** How many cases a loaded selection starts. */
function countSelectedCases(selected) {
    return selected.plans.reduce((total, plan) => total + plan.tests.length, 0)
}

/**
 * A record may narrow a run only when it describes that run.
 *
 * `scripts/selection-cases.mjs --targets` writes the targets it computed the list for into `plans`,
 * and `--changed` hands over the record it computed for exactly the targets it starts -- there the
 * narrowing is the whole point, and this function leaves it alone. A record whose `plans` name
 * another set was computed for another run: a change selection handed to a layer run. Using it as
 * `keep` makes the layer run start a subset of the layer while still printing the layer's name, and
 * every reading a gate takes (exit 0, a `[SUMMARY]` with no failure, a `tests:` count nobody
 * compares with the layer's) reads the same as a complete layer. Nothing in the run's output says
 * which of the two it was, which is why the refusal is a number and not a warning.
 *
 * A whole-tree run is the floor under that rule: a record that covers fewer of its cases than the
 * layer holds is a change's list whatever its `plans` say, because no change selection names the
 * plan tree as its target. The denominator is this layer's own list, read through the same
 * `loadPlans` a run of it uses, so it is the number `scripts/tier-report.mjs` reports for this tree.
 */
function refuseRecordThatNarrowsLayer(options, selection, selected) {
    // A record that names no target makes no claim: the synthetic probes of the self-test write
    // `plans: []`, and their refusal is about the case names they carry.
    const described = recordedTargets(selection.doc)
    if (described.length === 0) return
    const started = runTargets(options)
    const describesThisRun =
        described.length === started.length && described.every((target, index) => target === started[index])
    if (describesThisRun && !namesWholePlanTree(options)) return
    const layer = loadPlans(
        options.targets,
        options.tier,
        options.layeredPlans,
        options.compileOnly === true,
        options.selectionEmpty === true,
        null
    )
    const covered = countSelectedCases(selected)
    const layerCases = countSelectedCases(layer)
    if (covered >= layerCases) return
    const shown = (list) => {
        const head = list.slice(0, 3).join(', ')
        return list.length > 3 ? `${head}, ...` : head
    }
    const where = describesThisRun
        ? `The record names this run's target(s) -- ${shown(started)} -- and covers ${covered} of ` +
          `their ${layerCases} cases: its case list is a change's, not this layer's.`
        : `The record was computed for ${described.length} target(s) (${shown(described)}) and this ` +
          `run names ${started.length} (${shown(started)}).`
    throw selectionRefusal(
        `the selection covers ${covered} of ${layerCases} cases in this layer; a layer run cannot ` +
            `sign for the layer if the record narrows it. ${selection.file}: ${where} A record ` +
            'narrows the run it describes: run the targets the record names, or compute the record ' +
            "for this run with scripts/selection-cases.mjs --targets."
    )
}

/**
 * `--print-time-sensitive`: one row per plan file named as a target, `<path>` TAB `<token>`, where
 * the token is what *this* reader made of the plan's top-level `time_sensitive`:
 *
 *   true        the field is the boolean true: this plan asks for the host to itself
 *   false       the field is the boolean false
 *   absent      there is no top-level `time_sensitive`
 *   invalid     the field is there and is not a boolean; `validatePlanSchema` refuses such a plan
 *   unreadable  the file could not be read or parsed (exit 2, with the reason on stderr)
 *
 * The gate that takes the host reads this and nothing else. Before the flag existed it answered the
 * question with two regular expressions of its own over the plan's lines, so the field had two
 * readers -- and a field that decides whether a run owns the machine cannot be read two ways: the
 * reading that matters is the one the runner will act on, so the gate asks instead of deciding.
 *
 * `invalid` and `unreadable` are readings, not failures of this command: the plan they describe is
 * one the runner will refuse. Only `unreadable` sets the exit code, because a file this program
 * cannot open is a caller that asked about the wrong tree.
 */
function printTimeSensitive(options) {
    let status = 0
    for (const target of options.targets) {
        const shown = relPath(target)
        let doc
        try {
            doc = parseTomlFile(target)
        } catch (error) {
            console.log(`${shown}\tunreadable`)
            console.error(
                `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${shown}: ${String(error && error.message).split('\n')[0]}`
            )
            status = 2
            continue
        }
        const value = doc.time_sensitive
        const token = value === undefined ? 'absent' : typeof value === 'boolean' ? String(value) : 'invalid'
        console.log(`${shown}\t${token}`)
    }
    return status
}

/**
 * `--list-cases`: the cases this selection would run, printed as JSON rows, nothing started and no
 * install consulted. It reads the plan tree through `loadPlans`, so the `[[sweeps]]` copies, the
 * layer filter and the companion closure are the run's own; a listing that agreed with the run only
 * by construction would be a second definition of the same selection.
 */
function listSelectedCases(options) {
    const selection = options.selectionCases === null ? null : readCaseSelection(options.selectionCases)
    const keep = selection === null ? null : (name) => selection.names.has(name)
    const selected = loadPlans(
        options.targets,
        options.tier,
        options.layeredPlans,
        options.compileOnly === true,
        false,
        keep
    )
    if (selection !== null) refuseRecordThatNarrowsLayer(options, selection, selected)
    if (selected.reachedNothing.length > 0) {
        for (const target of selected.reachedNothing) {
            console.error(
                `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${relPath(path.resolve(target))} holds no ` +
                    '*.plan.toml, so it names no case'
            )
        }
        return 2
    }
    const missing = missingCaseNames(selected.plans, selection === null ? null : selection.names)
    if (missing.length > 0) {
        const shown = missing.slice(0, 5).join(', ')
        const rest = missing.length > 5 ? ` and ${missing.length - 5} more` : ''
        console.error(
            `${COLORS.red}[PLAN ERROR]${COLORS.reset} --selection-cases names ${missing.length} case(s) ` +
                `this selection would not start: ${shown}${rest}`
        )
        return 2
    }
    const rows = caseRows(selected)
    for (const row of rows) console.log(JSON.stringify(row))
    console.log(`cases: ${rows.length}`)
    return 0
}

/** The result of a test the runner itself could not run: its failure is reported like any other. */
function runnerErrorResult(casePath, message) {
    return {
        command: '',
        exitCode: null,
        signal: null,
        timedOut: false,
        errorMessage: message,
        wallMs: 0,
        stdout: '',
        stderr: '',
        output: '',
        normalized: { stdout: '', stderr: '', output: '' },
        diagnostics: [],
        benchmark: null,
        testTmp: null,
        testTmpRelative: null,
        verification: null,
        cpuBudgetMs: null,
        cpuMs: null,
        cpuReason: null,
        // Nothing ran, so there is no reading and no line to read it against; the keys are present
        // rather than absent, for the reason the verify payload keeps its null fields.
        cpuLimitMs: null,
        cpuMarginX: null,
        cpuMarginBasis: null,
        failures: [`runner error: ${message}`],
        status: 'fail',
        casePath,
    }
}

/**
 * Slot accounting. "The child is slow" and "the runner is busy" are different findings, and a
 * plan wall alone cannot tell them apart: both show up as time the plan took. Every test records
 * when it took a slot, when its child process came into being, when that child was gone, and when
 * the slot went back. The two intervals that answer the question are `pre_spawn_ms` (slot acquired
 * -> child created) and `post_exit_ms` (child gone -> slot released); `no_child_ms` is their sum,
 * which is the slot held while no test process was running.
 *
 * A total is not a finding, so each of the two is cut at the marks that name a mechanism:
 *
 *   pre_spawn_ms = scratch + probe + prep + spawn
 *                  the per-test directory's `mkdir`, the `skip_unless` probe, the runner's own
 *                  argument and environment expansion, and the `spawn` call itself.
 *   post_exit_ms = settle + validate + verify + cleanup + tail
 *                  resuming from the child's `close` event and normalizing what it printed, the
 *                  assertions on that output (a golden is a synchronous read), the verification
 *                  script, removing the scratch directory, and the last hair before the semaphore
 *                  goes back.
 *
 * Neither `probe_ms` nor `verify_ms` is the test program: a `skip_unless` probe is a check of the
 * host, and a verification script reads the result after the child is gone. Each is recorded whole
 * as well as in its in-slot part, so a version that moves one out of the slot can be compared with
 * one that does not.
 */
function roundMs(value) {
    return Math.round(value * 10) / 10
}

/** Milliseconds of the absolute interval `[start, end]` that fall inside any of `windows`. */
function coveredMs(windows, start, end) {
    let total = 0
    for (const window of windows) {
        total += Math.max(0, Math.min(end, window.end) - Math.max(start, window.start))
    }
    return total
}

function slotTiming(entry, clock, marks, windows) {
    if (clock.acquiredAt === undefined && clock.probeAt === undefined) return null
    const base = clock.runStart
    const at = (value) => (value === undefined ? null : Math.round(value - base))
    const hasSlot = clock.acquiredAt !== undefined
    const acquiredAt = clock.acquiredAt ?? base
    const releasedAt = clock.releasedAt ?? performance.now()
    const spawnedAt = marks.spawnedAt
    const exitedAt = marks.exitedAt
    const clamp = (value) => Math.max(0, value)
    // The whole cost of a step, whether or not it ran while the slot was held: every step is
    // reported both ways, because a step that moved out of the slot still costs the plan its time.
    const probeMs = clock.probeAt === undefined ? 0 : (clock.preconditionAt ?? releasedAt) - clock.probeAt
    const settleMs = clock.normalizedAt === undefined ? 0 : clock.normalizedAt - (exitedAt ?? clock.normalizedAt)
    const validateMs =
        clock.validationAt === undefined || clock.normalizedAt === undefined
            ? 0
            : clock.validationAt - clock.normalizedAt
    const cleanupMs = clock.removeMs ?? 0
    // The permit queue ahead of the verification script: runner work, but not the script's.
    const verifyWaitMs =
        clock.validationAt === undefined || clock.verifyPermitAt === undefined
            ? 0
            : clock.verifyPermitAt - clock.validationAt
    // Everything this entry still owed after it let the slot go, including the printing its result
    // waited behind. Summed over the entries and divided by the width, this is the part of the run
    // that no slot can cover.
    const postReleaseMs = clock.settledAt === undefined ? 0 : clock.settledAt - releasedAt
    const childMs = spawnedAt !== undefined && exitedAt !== undefined ? exitedAt - spawnedAt : 0
    // A test the precondition skipped never took a slot: its whole cost is the probe.
    const preSpawnMs = hasSlot ? (spawnedAt ?? releasedAt) - acquiredAt : 0
    const postExitMs = hasSlot ? releasedAt - (exitedAt ?? releasedAt) : 0
    // Walk the marks in order and take what each one added since the previous one, bounded by the
    // slot's release: a mark that is absent, or that lies outside the slot, contributes nothing.
    // That makes each family a partition of its interval whether or not a step happened, and it
    // stays correct when a step moves out of the slot.
    const walk = (from) => {
        let cursor = from
        return (mark) => {
            if (mark === undefined) return 0
            const bounded = Math.min(mark, releasedAt)
            if (bounded < cursor) return 0
            const span = bounded - cursor
            cursor = bounded
            return span
        }
    }
    const beforeSlot = walk(acquiredAt)
    const scratchMs = hasSlot ? beforeSlot(clock.scratchAt) : 0
    const probeInSlotMs = hasSlot ? beforeSlot(clock.preconditionAt) : 0
    const spawnMs = marks.spawnMs ?? 0
    const prepMs = hasSlot ? clamp(preSpawnMs - scratchMs - probeInSlotMs - spawnMs) : 0
    const afterExit = walk(exitedAt ?? releasedAt)
    const settleInSlotMs = hasSlot ? afterExit(clock.normalizedAt) : 0
    const validateInSlotMs = hasSlot ? afterExit(clock.validationAt) : 0
    const verifyInSlotMs = hasSlot ? afterExit(clock.verifyDoneAt) : 0
    const cleanupInSlotMs = hasSlot ? afterExit(clock.removeAt) : 0
    const tailMs = hasSlot
        ? clamp(postExitMs - settleInSlotMs - validateInSlotMs - verifyInSlotMs - cleanupInSlotMs)
        : 0
    const result = entry.outcome ? entry.outcome.result : null
    const verification = result ? result.verification : null
    const verifyMs = verification && verification.wallMs ? verification.wallMs : 0
    // The one figure in this record that is the case's own work rather than its share of the
    // host's: its CPU time, on the record of every case, beside the intervals above. `cpu_reading`
    // says where the figure came from, or why this platform has none; a skipped case did not run,
    // so its record says that instead of quoting a cost of zero.
    const skipped = entry.outcome ? entry.outcome.status === 'skip' : false
    const reading = cpuReadingRecord({
        cpuMs: result ? result.cpuMs : null,
        cpuReason: result ? result.cpuReason : null,
        canReadCpu: CAN_READ_CHILD_CPU,
    })
    return {
        plan: entry.plan.id,
        plan_path: entry.plan.__planPath,
        test: entry.test.name,
        status: entry.outcome ? entry.outcome.status : 'fail',
        // The child was ended at its limit: what separates a case that is slow from one that is wrong.
        timed_out: result ? result.timedOut === true : false,
        serial: clock.serial === true,
// This test's own CPU time (user + system, the whole process tree), the figure its
        // `cpu_budget_ms` and the timeout margin are judged against -- and `cpu_reason` where this
        // host reported none, so a null here is a stated absence and not a zero. `cpu_reading` says
        // the same to a reader of `meta.json`, and a case that was skipped says it did not run
        // instead of quoting a cost of zero.
        cpu_ms: skipped ? null : reading.cpuMs,
        cpu_reading: skipped
            ? 'not run: the case was skipped, so this run charged it no CPU'
            : reading.cpuReading,
        cpu_budget_ms: result && typeof result.cpuBudgetMs === 'number' ? roundMs(result.cpuBudgetMs) : null,
        cpu_reason: result ? result.cpuReason ?? null : null,
        // How many of the run's slot permits this test held: a `parallel = false` test and a
        // benchmark ask for all of them, so it is the one case where a held slot is not one
        // eighth of the run's capacity.
        tokens: hasSlot ? clock.tokens ?? 1 : 0,
        slot_ms: hasSlot ? roundMs(releasedAt - acquiredAt) : 0,
        child_ms: roundMs(childMs),
        pre_spawn_ms: roundMs(preSpawnMs),
        post_exit_ms: roundMs(postExitMs),
        no_child_ms: roundMs(preSpawnMs + postExitMs),
        // Slot time that is neither a child nor a probe of the host: the runner's own work, which
        // is the part a scheduler change can move or make cheap.
        idle_ms: roundMs(clamp(preSpawnMs + postExitMs - probeInSlotMs)),
        queue_ms: hasSlot
            ? roundMs(acquiredAt - (clock.dependenciesDoneAt ?? clock.enqueuedAt ?? acquiredAt))
            : 0,
        // `_ms` is the step whole wherever it ran; `_in_slot_ms` is the part of it that held a slot.
        probe_ms: roundMs(probeMs),
        probe_in_slot_ms: roundMs(probeInSlotMs),
        settle_ms: roundMs(settleMs),
        validate_ms: roundMs(validateMs),
        verify_ms: roundMs(verifyMs),
        verify_in_slot_ms: roundMs(verifyInSlotMs),
        verify_child_ms: roundMs(verification && verification.childMs ? verification.childMs : 0),
        verify_wait_ms: roundMs(verifyWaitMs),
        post_release_ms: roundMs(postReleaseMs),
        cleanup_ms: roundMs(cleanupMs),
        cleanup_in_slot_ms: roundMs(cleanupInSlotMs),
        components_ms: {
            scratch: roundMs(scratchMs),
            probe: roundMs(probeInSlotMs),
            prep: roundMs(prepMs),
            spawn: roundMs(spawnMs),
            settle: roundMs(settleInSlotMs),
            validate: roundMs(validateInSlotMs),
            verify: roundMs(verifyInSlotMs),
            cleanup: roundMs(cleanupInSlotMs),
            tail: roundMs(tailMs),
        },
        log_write_ms: 0,
        // How much of this test's post-exit window was the runner writing log files for *other*
        // tests: log writing is synchronous on the runner's only thread, so it blocks the event
        // loop for everyone and shows up here rather than in the writer's own record.
        log_block_ms: hasSlot ? roundMs(coveredMs(windows, exitedAt ?? acquiredAt, releasedAt)) : 0,
        // When this test held a slot, relative to the start of the run: the occupancy timeline
        // the empty-slot question needs. A slot is held from `acquired` to `released`; a test
        // child runs from `spawned` to `exited`.
        at: {
            enqueued: at(clock.enqueuedAt),
            acquired: at(clock.acquiredAt),
            spawned: at(spawnedAt),
            exited: at(exitedAt),
            released: at(hasSlot ? clock.releasedAt : clock.preconditionAt),
            settled: at(clock.settledAt),
        },
    }
}

function percentile(sorted, fraction) {
    if (sorted.length === 0) return 0
    const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil(fraction * sorted.length) - 1))
    return sorted[index]
}

function distribution(values) {
    const sorted = [...values].sort((lhs, rhs) => lhs - rhs)
    const total = sorted.reduce((sum, value) => sum + value, 0)
    return {
        median: roundMs(percentile(sorted, 0.5)),
        p90: roundMs(percentile(sorted, 0.9)),
        p99: roundMs(percentile(sorted, 0.99)),
        max: roundMs(sorted.length > 0 ? sorted[sorted.length - 1] : 0),
        total: Math.round(total),
    }
}

function topContributors(records, field, count = 20) {
    return [...records]
        .sort((lhs, rhs) => rhs[field] - lhs[field])
        .slice(0, count)
        .map((record) => ({
            plan: record.plan,
            test: record.test,
            status: record.status,
            tokens: record.tokens,
            ms: roundMs(record[field]),
            child_ms: record.child_ms,
            slot_ms: record.slot_ms,
            pre_spawn_ms: record.pre_spawn_ms,
            post_exit_ms: record.post_exit_ms,
            probe_ms: record.probe_ms,
            verify_ms: record.verify_ms,
            verify_wait_ms: record.verify_wait_ms,
            cleanup_ms: record.cleanup_ms,
            log_block_ms: record.log_block_ms,
            components_ms: record.components_ms,
        }))
}

/**
 * Occupancy over the run, one bucket per second: how many slots were held, and in how many of
 * them a test child was running. An empty slot (held - children) is work the scheduler never
 * handed out, and a held slot with no child is runner work. Both are in the same picture, which
 * is the only way to tell them apart after the fact.
 */
function buildTimeline(records, planWallMs, jobs, bucketMs = 1000) {
    const buckets = Math.max(1, Math.ceil(planWallMs / bucketMs))
    const held = new Array(buckets).fill(0)
    const children = new Array(buckets).fill(0)
    const overlap = (start, end, bucket) => {
        const bucketStart = bucket * bucketMs
        return Math.max(0, Math.min(end, bucketStart + bucketMs) - Math.max(start, bucketStart)) / bucketMs
    }
    for (const record of records) {
        const { acquired, released, spawned, exited } = record.at
        if (acquired === null || released === null) continue
        for (let bucket = Math.floor(acquired / bucketMs); bucket < buckets && bucket * bucketMs < released; bucket++) {
            held[bucket] += overlap(acquired, released, bucket)
        }
        if (spawned !== null && exited !== null) {
            for (let bucket = Math.floor(spawned / bucketMs); bucket < buckets && bucket * bucketMs < exited; bucket++) {
                children[bucket] += overlap(spawned, exited, bucket)
            }
        }
    }
    const round = (value) => Math.round(value * 100) / 100
    return {
        bucket_ms: bucketMs,
        slots_held: held.map(round),
        children: children.map(round),
    }
}

/**
 * Event-loop lag. The runner has one JavaScript thread, and every synchronous thing it does around
 * a test happens there: creating a scratch directory, reading a golden, writing the log files. A
 * synchronous section blocks *every* slot at once, and it hides from the per-test intervals: a
 * child that finished while the runner was writing another test's log is not seen to have finished
 * until the write returns, so the wait is charged to that child's `child_ms` and its own
 * `post_exit_ms` stays small. The one measurement that cannot hide it is a timer that reports how
 * late it fired: `gap - period` is time the loop could not service.
 */
const LOOP_SAMPLE_MS = 25

function startLoopMonitor() {
    const stalls = []
    let last = performance.now()
    const timer = setInterval(() => {
        const now = performance.now()
        const gap = now - last
        last = now
        if (gap >= LOOP_SAMPLE_MS * 2) {
            stalls.push({ start: now - gap, end: now, ms: gap })
        }
    }, LOOP_SAMPLE_MS)
    if (typeof timer.unref === 'function') timer.unref()
    return () => {
        clearInterval(timer)
        return stalls
    }
}

/**
 * What this run could and could not read about CPU, for `summary.json`. A `cpu_ms: null` on every
 * test is not "every test used no CPU": on this host class it is "the kernel offered no figure", and
 * a record read a week later has to be able to tell that apart from a runner that stopped measuring.
 * So the run states it once, with the reasons it recorded per test and how many tests declared a
 * budget, instead of leaving the absence to be inferred from 8,000 nulls.
 */
function summarizeCpuReadings(records) {
    const reasons = new Map()
    let withReading = 0
    let budgeted = 0
    let budgetWithReading = 0
    for (const record of records) {
        if (Number.isFinite(record.cpu_ms)) withReading += 1
        else {
            const reason = record.cpu_reason || NO_CPU_TIME_SKIP
            reasons.set(reason, (reasons.get(reason) || 0) + 1)
        }
        if (typeof record.cpu_budget_ms === 'number') {
            budgeted += 1
            if (Number.isFinite(record.cpu_ms)) budgetWithReading += 1
        }
    }
    return {
        available: withReading > 0,
        platform: process.platform,
        tests: records.length,
        with_reading: withReading,
        without_reading: records.length - withReading,
        cpu_budget_tests: budgeted,
        cpu_budget_with_reading: budgetWithReading,
        reasons: [...reasons.entries()]
            .sort((lhs, rhs) => rhs[1] - lhs[1] || lhs[0].localeCompare(rhs[0]))
            .map(([reason, count]) => ({ reason, count })),
    }
}

/**
 * The whole run's slot accounting, for `summary.json`. A total without names is how the wrong
 * thing gets optimized, so every total that has a distribution also names its top contributors.
 */
function summarizeSlotTimings(records, planWallMs, jobs, stalls, logWindows, runStartMs) {
    const sum = (field) => records.reduce((total, record) => total + (record[field] || 0), 0)
    const childTotal = sum('child_ms')
    const componentNames = ['scratch', 'probe', 'prep', 'spawn', 'settle', 'validate', 'verify', 'cleanup', 'tail']
    const componentTotals = {}
    for (const name of componentNames) {
        componentTotals[name] = Math.round(
            records.reduce((total, record) => total + (record.components_ms[name] || 0), 0)
        )
    }
    const sortedStalls = [...stalls].sort((lhs, rhs) => rhs.ms - lhs.ms)
    const stallTotal = stalls.reduce((total, stall) => total + stall.ms, 0)
    // A stall is the runner's own thread being unavailable. Naming which of its known synchronous
    // sections the stall overlapped is what turns "the loop was busy" into "the loop was writing
    // logs": the log windows are exact, so the overlap is not a guess.
    const stallCovered = stalls.reduce(
        (total, stall) => total + coveredMs(logWindows, stall.start, stall.end),
        0
    )
    // A `parallel = false` test and a benchmark hold every permit, so `jobs * wall` minus the
    // one-permit sum is not the idle capacity: `slot_ms` counts their slot once. Both quantities
    // are reported, because "the run never handed out this slot" and "this test locked the others
    // out on purpose" are different answers to why the ratio is below `jobs`.
    const capacityMs = records.reduce((total, record) => total + (record.tokens || 1) * record.slot_ms, 0)
    const serialExclusiveMs = records.reduce(
        (total, record) => total + ((record.tokens || 1) - 1) * record.slot_ms,
        0
    )
    // The cases that carry a CPU figure, and the sentences of the ones that do not: the run states
    // which half of its own record answers "what did this cost", so a reading of 0 cases is a fact
    // about the host and never an empty total that reads as a cheap layer.
    const cpuRecords = records.filter((record) => Number.isFinite(record.cpu_ms))
    const absentReadings = [...new Set(
        records.filter((record) => !Number.isFinite(record.cpu_ms)).map((record) => record.cpu_reading)
    )].sort()
    return {
        tests: records.length,
        plan_wall_ms: Math.round(planWallMs),
        cpu: {
            read: cpuRecords.length,
            absent: records.length - cpuRecords.length,
            cpu_ms_total: Math.round(cpuRecords.reduce((total, record) => total + record.cpu_ms, 0)),
            cpu_ms: distribution(cpuRecords.map((record) => record.cpu_ms)),
            top_cpu_ms: topContributors(cpuRecords, 'cpu_ms'),
            absent_reading: absentReadings,
        },
        // Inside a slot: the test child, and the runner work that still happens while the slot is
        // held (creating the child).
        child_ms_total: Math.round(childTotal),
        slot_ms_total: Math.round(sum('slot_ms')),
        no_child_ms_total: Math.round(sum('no_child_ms')),
        idle_ms_total: Math.round(sum('idle_ms')),
        // Each step whole, wherever it ran, and the part of it that held a slot. The runner's own
        // work is the sum of these; moving one out of the slot changes `_in_slot_ms`, not `_ms`.
        scratch_ms_total: Math.round(componentTotals.scratch),
        prep_ms_total: Math.round(componentTotals.prep),
        spawn_ms_total: Math.round(componentTotals.spawn),
        settle_ms_total: Math.round(sum('settle_ms')),
        settle_in_slot_ms_total: Math.round(componentTotals.settle),
        validate_ms_total: Math.round(sum('validate_ms')),
        validate_in_slot_ms_total: Math.round(componentTotals.validate),
        cleanup_ms_total: Math.round(sum('cleanup_ms')),
        cleanup_in_slot_ms_total: Math.round(sum('cleanup_in_slot_ms')),
        probe_ms_total: Math.round(sum('probe_ms')),
        probe_in_slot_ms_total: Math.round(sum('probe_in_slot_ms')),
        verify_ms_total: Math.round(sum('verify_ms')),
        verify_in_slot_ms_total: Math.round(sum('verify_in_slot_ms')),
        verify_child_ms_total: Math.round(
            records.reduce((total, record) => total + (record.verify_child_ms || 0), 0)
        ),
        // The queue in front of the verification width, and what each entry still owed after it
        // released its slot. `post_release_ms_total / jobs` is the part of the wall no slot covers,
        // so it is the number that says whether the run is still slot-bound or not.
        verify_wait_ms_total: Math.round(sum('verify_wait_ms')),
        post_release_ms_total: Math.round(sum('post_release_ms')),
        post_release_per_slot_ms: Math.round(sum('post_release_ms') / jobs),
        last_release_ms: Math.round(
            records.reduce((total, record) => Math.max(total, record.at.released || 0), 0)
        ),
        last_settled_ms: Math.round(
            records.reduce((total, record) => Math.max(total, record.at.settled || 0), 0)
        ),
        log_write_ms_total: Math.round(sum('log_write_ms')),
        // The same writing, charged to the tests whose slot it blocked instead of to the writer.
        log_block_ms_total: Math.round(sum('log_block_ms')),
        log_writes: records.filter((record) => record.log_write_ms > 0).length,
        probes: records.filter((record) => record.probe_ms > 0).length,
        verifications: records.filter((record) => record.verify_ms > 0).length,
        // The reported ratio: how many tests' children ran at once, on average, over the plan.
        avg_concurrency: planWallMs > 0 ? Math.round((childTotal / planWallMs) * 100) / 100 : 0,
        // What the slots were actually doing: every held slot, whether or not a child ran in it.
        avg_slots_held: planWallMs > 0 ? Math.round((sum('slot_ms') / planWallMs) * 100) / 100 : 0,
        // Capacity, counted in permits: `jobs * wall` is what the run had, minus what the tests
        // held (a serial test counts `jobs` times) is what nobody used.
        capacity_slot_ms_total: Math.round(planWallMs * jobs),
        serial_tests: records.filter((record) => (record.tokens || 1) > 1).length,
        serial_exclusive_ms_total: Math.round(serialExclusiveMs),
        empty_slot_ms_total: Math.max(0, Math.round(planWallMs * jobs - capacityMs)),
        no_child_ms: distribution(records.map((record) => record.no_child_ms)),
        pre_spawn_ms: distribution(records.map((record) => record.pre_spawn_ms)),
        post_exit_ms: distribution(records.map((record) => record.post_exit_ms)),
        idle_ms: distribution(records.map((record) => record.idle_ms)),
        queue_ms: distribution(records.map((record) => record.queue_ms)),
        child_ms: distribution(records.map((record) => record.child_ms)),
        probe_ms: distribution(records.map((record) => record.probe_ms)),
        verify_ms: distribution(records.map((record) => record.verify_ms)),
        verify_wait_ms: distribution(records.map((record) => record.verify_wait_ms)),
        post_release_ms: distribution(records.map((record) => record.post_release_ms)),
        cleanup_ms: distribution(records.map((record) => record.cleanup_ms)),
        log_block_ms: distribution(records.map((record) => record.log_block_ms)),
        // The runner's own thread, measured instead of inferred.
        loop_lag: {
            sample_ms: LOOP_SAMPLE_MS,
            threshold_ms: LOOP_SAMPLE_MS * 2,
            stall_count: stalls.length,
            stall_ms_total: Math.round(stallTotal),
            stall_ms_covered_by_log_write: Math.round(stallCovered),
            max_stall_ms: roundMs(sortedStalls.length > 0 ? sortedStalls[0].ms : 0),
            top_stalls: sortedStalls.slice(0, 20).map((stall) => ({
                at_ms: Math.round(stall.start - runStartMs),
                ms: roundMs(stall.ms),
            })),
        },
        // `scratch`, `probe`, `prep`, `spawn`, `settle`, `validate`, `verify`, `cleanup` and `tail`
        // are the partition of every held slot's not-child time, so they sum to `no_child_ms_total`;
        // the `_ms_total` fields above are the same steps whole, wherever they ran.
        components_ms: componentTotals,
        timeline: buildTimeline(records, planWallMs, jobs),
        top_no_child_ms: topContributors(records, 'no_child_ms'),
        top_pre_spawn_ms: topContributors(records, 'pre_spawn_ms'),
        top_post_exit_ms: topContributors(records, 'post_exit_ms'),
        top_idle_ms: topContributors(records, 'idle_ms'),
        top_child_ms: topContributors(records, 'child_ms'),
        top_probe_ms: topContributors(records, 'probe_ms'),
        top_verify_ms: topContributors(records, 'verify_ms'),
        top_verify_wait_ms: topContributors(records, 'verify_wait_ms'),
        top_post_release_ms: topContributors(records, 'post_release_ms'),
        top_cleanup_ms: topContributors(records, 'cleanup_ms'),
        top_log_block_ms: topContributors(records, 'log_block_ms'),
    }
}

/**
 * The tests whose own work is in progress right now -- from before their precondition to after
 * their verification script -- by the name a reader knows them by.
 *
 * One consumer: the tree watcher (`[TREE]`, test/tools/tree-guard.mjs). When a path appears under
 * the checkout while a test is running, the set is what it was while it appeared, so the line that
 * reports a file the run left behind names the tests that could have written it instead of asking
 * the next person to bisect a 9,465-test layer. It is a diagnosis only: the verdict is `git status`
 * read before and after the run, and a path nobody was running when it appeared is still a failure.
 */
const inFlight = new Map()
let inFlightNext = 1

function enterInFlight(test) {
    const id = inFlightNext++
    // The case as the plan writes it is relative to the plan file (`../../../cases/...`), which is
    // not a path a reader can open: the absolute one, made relative to the checkout, is.
    const fromRoot = test.case && test.__planPath
        ? path.relative(REPO_ROOT, entryCasePath(test)).split(path.sep).join('/')
        : test.case || ''
    inFlight.set(id, { plan: test.__planId || '', name: test.name, case: fromRoot })
    return id
}

function leaveInFlight(id) {
    inFlight.delete(id)
}

/**
 * Run every selected test, at most `options.jobs` a time, and print the results in plan and
 * test order: a status line, its failure details and its log files are the same as a serial run's,
 * in the same place, whichever test finishes first. A test that compares against an earlier one
 * waits for it; a test marked serial (`isSerial`) holds every slot while it runs.
 */
async function executePlans(plans, options, sharedVars, logDir) {
    const entries = []
    for (const plan of plans) {
        for (const test of plan.tests) {
            entries.push({ plan, test, index: entries.length, dependencies: [], settled: false, outcome: null })
        }
    }
    const byName = new Map()
    const byPlanName = new Map()
    for (const entry of entries) {
        if (!byName.has(entry.test.name)) byName.set(entry.test.name, entry)
        byPlanName.set(`${entry.plan.id}/${entry.test.name}`, entry)
    }
    for (const entry of entries) {
        const references = []
        if (entry.test.kind === 'differential' && entry.test.compare_to) references.push(entry.test.compare_to)
        if (entry.test.__backendReference) references.push(entry.test.__backendReference)
        entry.dependencies = references
            .map((name) => byPlanName.get(`${entry.plan.id}/${name}`) || byName.get(name))
            .filter((dependency) => dependency && dependency !== entry)
    }

    const slots = new Slots(options.jobs)
    // Two things around a test are processes of their own but are not the test program, and neither
    // needs the test slot: a `skip_unless` precondition is the runner asking the host a question,
    // and a verification script reads the result after the child is gone. Each gets its own width,
    // so a probe that hangs for its whole timeout cannot own one of the run's slots.
    // Measured: 1,920 verification scripts booting node inside their slot were 168-265 s, and 75-83
    // probes inside theirs 44-304 s, of the 255-532 s this plan spent holding slots with no test
    // process in them.
    // One width for both, `--jobs`: a probe may hang for its whole `skip_unless_timeout_ms`, and a
    // one-wide lane turns one hung probe into a stall of every probe behind it. Measured with a
    // one-wide probe lane: two 120 s MNIST preconditions left 42 probe tests queued and the run's
    // last 150 s with no slot held at all. `jobs`-wide, the same timeouts cost one lane each.
    const probes = new Slots(options.jobs)
    const verifications = new Slots(options.jobs)
    const summary = {
        pass: 0, fail: 0, skip: 0, xfail: 0, timedOut: 0, crashed: 0,
        // How many cases were given a second attempt because their first attempt timed out, and
        // how many of those came back green. The green one is already in `pass`; these two say that
        // it was not green first time (see `printRunSummary`).
        rerun: 0, rerunPassed: 0,
        // One row per known failure this run accepted: the name the `[XFAIL]` line printed and the
        // sentence the plan wrote for what would end it. Collected where the verdict is counted,
        // because that is the one place that knows the mark fired -- a roster read off the plan tree
        // instead would list marks that did not run here (a back end this host skipped, a tier the
        // run did not select) as if they had.
        knownRows: [],
    }
    // Results by test name, as a serial run had them: a differential test and a back end after
    // the first read the result of the test they compare against.
    const completed = new Map()
    const startedAt = performance.now()
    // Windows in which the runner was synchronously writing a test's log files: the one section it
    // can point at when a slot's post-exit time turns out to be someone else's work.
    const logWindows = []
    const stopLoopMonitor = startLoopMonitor()
    let printed = 0
    let printedPlan = null
    // The record of the case lines this run prints, written beside the printer that prints them
    // (test/tools/transcript.mjs). `main` reads it back at the end and refuses the run when the
    // file does not hold what the summary counted: a line printed and a line counted are one act
    // here, so a transcript that is short is output that did not arrive, and a run whose record is
    // short measured nothing a gate can use.
    const transcript = openTranscript(logDir)

    const printResult = (entry) => {
        const { plan, test, outcome } = entry
        entry.logWriteMs = 0
        // A case its own bound took is not printed here: it gets a second attempt at the end of the
        // run (`executePlans`), and its one line then carries both attempts. Printing the timeout
        // now would print the case's verdict twice -- once red, once whatever the second attempt
        // decided -- and a reader counting lines would count two verdicts for one case. The first
        // attempt's logs are written now, under `rerun/first/`, so the timeout's own output and the
        // bound that produced it are kept instead of being overwritten by the second attempt.
        if (rerunCandidate(entry, options)) {
            entry.rerunPending = true
            const writeStarted = performance.now()
            writeLogFiles(plan, test, outcome.result, outcome.status, logDir, RERUN_FIRST_DIR)
            entry.logWriteMs = performance.now() - writeStarted
            return
        }
        // The one output `--list-known` has, recorded in the order the results are printed -- plan
        // and test order, whichever test finished first -- and before the skip below returns, since
        // a skipped case is one of the four verdicts this list exists to tell apart. `xfail` is
        // printed as `known`: the run's own word for it is the plan's mark, and the list is a
        // reading of the marks, not a second vocabulary.
        if (options.listKnownRows) {
            // `reading` is the comparable form of this verdict's first line, and it is present for
            // every verdict rather than for failures only: `known -> fail` is one of the moves a
            // merge gate has to see, and a field that exists only once something is red cannot see
            // it. A `skip` carries its reason, a pass carries nothing, and both say so instead of
            // being absent -- an absent key and a null one read the same to a careless comparison,
            // and only one of them means "this run had nothing to say".
            const first = outcome.status === 'skip'
                ? outcome.reason
                : Array.isArray(outcome.failures) && outcome.failures.length > 0 ? outcome.failures[0] : null
            options.listKnownRows.push({
                name: listedName(plan, test),
                verdict: outcome.status === 'xfail' ? 'known' : outcome.status,
                reading: normalizeReading(first),
                reading_version: READING_NORMAL_VERSION,
            })
        }
        if (plan !== printedPlan) {
            printPlanHeader(plan)
            printedPlan = plan
        }
        if (outcome.status === 'skip') {
            summary.skip++
            transcript.caseLine(printStatusLine('skip', test.name))
            // Where the case came from, before why it did not run: a skip count is read per plan,
            // and for a copy the plan a reader would open is the sweeper's.
            const source = sweepSourceLine(test)
            if (source) printDetailLines([source])
            printDetailLines([outcome.reason])
            return
        }
        summary[outcome.status]++
        // The known failure's own row for the closing roster: the name the red printed, and the
        // sentence the plan says would end it. Pushed here rather than derived from the plan tree
        // afterwards, so the roster is a reading of the run -- exactly the marks that fired in it.
        if (outcome.status === 'xfail') {
            summary.knownRows.push({ name: listedName(plan, test), endsWhen: test.__knownFailEndsWhen || null })
        }
        // A failing run that ended without an exit code is one of two things, and the summary says
        // which: the timeout took it, or a signal did. (A marked failure is counted as `xfail`
        // above and is not classified here -- the mark's own note says what it is.)
        if (outcome.status === 'fail' && outcome.result) {
            if (outcome.result.timedOut) summary.timedOut++
            else if (outcome.result.signal) summary.crashed++
        }
        transcript.caseLine(printStatusLine(outcome.status, test.name, outcome.metric))
        // The file this red belongs to, printed beside the red: the verdict line and the header
        // above it name the sweeper that ran the copy, and a reader who opens the sweeper finds
        // no such test. See `sweepSourceLine`.
        if (outcome.status !== 'pass') {
            const source = sweepSourceLine(test)
            if (source) printDetailLines([source])
        }
        // Who ends this red, printed beside the red. An owner that exists only in the plan file is a
        // promise the person reading the gate never sees, and the field would then be read by
        // nothing but the load-time check -- which is the shape this project already called worse
        // than no mark at all.
        if (test.__knownFail && typeof test.known_fail_owner === 'string') {
            printDetailLines([`known_fail_owner: ${test.known_fail_owner}`])
        }
        if (outcome.failures.length > 0) {
            const note = test.__knownFail ? test.known_fail_note || test.failure_note : test.failure_note
            if (note) {
                printDetailLines([`note: ${note}`])
            }
            // The pointer to the whole output, computed once: it is printed before the twelve-line
            // tail, and again on the line that cuts the tail, so a reader who stops at the tail has
            // been told twice where the rest is.
            const pointer = outcome.status === 'fail'
                ? fullOutputLine(logPathOf(plan, test, outcome.status, logDir))
                : ''
            if (outcome.status === 'fail') {
                printDetailLines([pointer])
            }
            printDetailLines(outcome.failures)
            // A regex that matched nothing prints its pattern and the head of what it read, before
            // the tail: the two ends of the output are two different readings.
            if (outcome.status === 'fail') printMatchDiagnostics(outcome.result && outcome.result.matchDiagnostics)
            if (outcome.status === 'fail') printFailedOutput(outcome.result, pointer)
        }
        const writeStarted = performance.now()
        writeLogFiles(plan, test, outcome.result, outcome.status, logDir)
        entry.logWriteMs = performance.now() - writeStarted
        logWindows.push({ start: writeStarted, end: performance.now() })
    }

    // Results are printed strictly in plan and test order: whichever test finishes first, the
    // output is the serial run's output. A result waits until every earlier one is out.
    const drain = () => {
        while (printed < entries.length && entries[printed].settled) {
            printResult(entries[printed])
            printed++
        }
    }

    for (const entry of entries) {
        // When the slot was taken, when the child existed and when the slot went back: the
        // scheduler's own accounting, recorded for every test whether it passes, fails or skips.
        const clock = {}
        const marks = {}
        clock.runStart = startedAt
        entry.promise = (async () => {
            clock.enqueuedAt = performance.now()
            if (entry.dependencies.length > 0) {
                await Promise.all(entry.dependencies.map((dependency) => dependency.promise))
            }
            clock.dependenciesDoneAt = performance.now()
            const serial = isSerial(entry.plan, entry.test)
            const tokens = serial ? options.jobs : 1
            clock.serial = serial
            clock.tokens = tokens
            const wantsProbe = Array.isArray(entry.test.skip_unless) && entry.test.skip_unless.length > 0
            // A test that needs another test's result queues at the front: it starts right after
            // its reference instead of behind every test of the run, which would also delay the
            // in-order printer until the end.
            const priority = entry.dependencies.length > 0
            let scratch = null
            let probeHeld = false
            let slotAcquired = false
            let slotReleased = false
            // The test's own work starts when it holds a slot: before that it is a promise waiting
            // in the queue, and every entry in the run has one of those from the moment the plan is
            // expanded -- `enterInFlight` at the top of this function put 7,270 of them on the
            // `[TREE]` line at once. `runOneTest` releases the slot the moment the test's own
            // process is gone, which is the other end of the same window.
            let flight = null
            // Idempotent: `runOneTest` releases the slot the moment the test's own process is gone,
            // and the finally block releases it for a test that never got that far (a skip, a
            // runner error).
            const releaseSlot = () => {
                if (slotReleased) return
                slotReleased = true
                if (slotAcquired) slots.release(tokens)
                clock.releasedAt = performance.now()
            }
            try {
                if (wantsProbe && !serial) {
                    // The probe runs before the slot is taken: it checks the host, it does not run
                    // the test, and the two MNIST probes plus five device probes on a host that
                    // cannot download held slots for 4-17 s each. A serial test keeps everything
                    // inside its slot: nothing may run next to it at all, probe included.
                    await probes.acquire(1)
                    probeHeld = true
                    scratch = createTestScratchDir(entry.test)
                    clock.scratchAt = performance.now()
                    const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                    // Before the probe, and before the card: "is my product there" is a question
                    // about this build and not about the host, and its answer fails the test.
                    const absent = missingArtifacts(entry.test, sharedVars, scratchRelative, options)
                    if (absent.length > 0) {
                        const casePath = entryCasePath(entry.test)
                        const result = missingArtifactResult(casePath, absent)
                        entry.outcome = { status: 'fail', result, metric: '', failures: result.failures }
                        return
                    }
                    // The host checks begin here: the card selection and the `skip_unless` probe
                    // are both questions about the machine, and they are counted as such.
                    clock.probeAt = performance.now()
                    // The card goes before the probe: the probe is this test's own question about
                    // the host, and it must be asked against the card the test will get.
                    const deviceSkip = await selectCudaDeviceFor(entry.test)
                    if (deviceSkip) {
                        probes.release(1)
                        probeHeld = false
                        entry.outcome = { status: 'skip', reason: deviceSkip }
                        return
                    }
                    const skipReason = await checkPrecondition(entry.test, sharedVars, scratch, scratchRelative)
                    clock.preconditionAt = performance.now()
                    probes.release(1)
                    probeHeld = false
                    if (skipReason) {
                        entry.outcome = { status: 'skip', reason: skipReason }
                        return
                    }
                }
                await slots.acquire(tokens, priority)
                slotAcquired = true
                clock.acquiredAt = performance.now()
                // Here, and not at the top: from this line to the `finally` below, anything that
                // appears under the checkout is this test's doing (the `[TREE]` line says so by
                // name). A `skip_unless` probe runs before this point and is not covered -- it asks
                // the host a question, and the two things it may start answer on stdout.
                flight = enterInFlight(entry.test)
                if (!wantsProbe || serial) {
                    scratch = createTestScratchDir(entry.test)
                    clock.scratchAt = performance.now()
                    const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                    // The same question the probe branch asks first, and for the same reason: a
                    // product this test reads is this build's, not the host's, and its absence fails.
                    const absent = missingArtifacts(entry.test, sharedVars, scratchRelative, options)
                    if (absent.length > 0) {
                        const casePath = entryCasePath(entry.test)
                        const result = missingArtifactResult(casePath, absent)
                        entry.outcome = { status: 'fail', result, metric: '', failures: result.failures }
                        return
                    }
                    // A serial test (or one with no probe) picks its card inside its own slot:
                    // nothing may run next to a serial test, and the selection is a child process.
                    const deviceSkip = await selectCudaDeviceFor(entry.test)
                    if (deviceSkip) {
                        entry.outcome = { status: 'skip', reason: deviceSkip }
                        return
                    }
                    // The mark is only taken for a test that has a precondition: `performance.now()`
                    // around a call that returns at once is a rounding artefact (0.1 ms) on
                    // thousands of tests, not a probe, and it would be counted as one.
                    if (wantsProbe) clock.probeAt = performance.now()
                    const skipReason = await checkPrecondition(entry.test, sharedVars, scratch, scratchRelative)
                    if (wantsProbe) clock.preconditionAt = performance.now()
                    if (skipReason) {
                        entry.outcome = { status: 'skip', reason: skipReason }
                        return
                    }
                }
                const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                let result
                try {
                    result = await runOneTest(
                        entry.test,
                        sharedVars,
                        // A serial test passes no hook and keeps its slot to the end: it was marked
                        // because it shares something outside its own scratch directory, and its
                        // verification script may read that same thing.
                        { options, completed, planId: entry.plan.id, clock, verifications, releaseSlot: serial ? null : releaseSlot },
                        scratch,
                        scratchRelative,
                        marks
                    )
                } catch (error) {
                    const casePath = entryCasePath(entry.test)
                    result = runnerErrorResult(casePath, error && error.message ? error.message : String(error))
                }
                clock.validatedAt = performance.now()
                completed.set(`${entry.plan.id}/${entry.test.name}`, result)
                completed.set(entry.test.name, result)
                if (entry.test.__knownFail) {
                    // A known failure that fails is reported and counted apart, and does not fail
                    // the run. One that passes fails the run: the mark is out of date.
                    if (result.status === 'fail') {
                        // A mark says "this case fails", not "this case fails for this reason". A
                        // case that starts failing differently is a new defect wearing an old name,
                        // so a plan may pin a fragment of the message it expects: one that no
                        // longer appears turns the run red, the same way a mark that starts
                        // passing does. Unset is legal and pins nothing.
                        const pinned = entry.test.__knownFailMsg
                        // This column IS the verdict and NOT the case's output: the failure lines
                        // the runner assembled about the case, then what the case printed. Only
                        // `known_fail_msg` reads it, and it has to read the first part: an entry with
                        // a verification script is judged by two programs, and `verifyFailureLines`
                        // carries the second one's text here (`verification output: ...`). Measured
                        // on the gate host at 1e01eaa0a9, by narrowing this column to the case's own
                        // output and running every plan that carries a pin: the two pins of
                        // `feat.linear.effects_model_edges` stopped matching (`CTRL edge(s)`,
                        // `CTRL input(s)` -- both are lines their verifiers print), and every other
                        // pin in the tree still matched, so the runner's own text is read by those
                        // two verifiers and by no pin that names the case's output.
                        //
                        // A `prints` condition must never be matched against this column: a failure
                        // line quotes the line a run did not find (`output does not contain '<line>'`),
                        // so the condition would be satisfied by the sentence saying it was not (see
                        // `observedOutput`).
                        const verdictText = `${(result.failures || []).join('\n')}\n${result.stdout || ''}\n${result.stderr || ''}`
                        if (pinned !== null && !verdictText.includes(pinned)) {
                            const first = `${(result.failures || [])[0] || result.stderr || result.stdout || '(no output)'}`
                                .split('\n')[0]
                            result.status = 'fail'
                            result.failures.push(
                                `known failure changed its mind: expected "${pinned}", got "${first.trim()}"`,
                            )
                        } else if (endConditionHasArrived(entry.test, observedOutput(result))) {
                            // Strict xfail: a mark whose ending has arrived stops being an excuse the
                            // moment the run reads it. Left as an XFAIL it would be a green nobody has
                            // to look at again -- the debt the STALE snippet entries carried, where
                            // the thing to do is delete the entry and the reading exists to make
                            // somebody do it.
                            result.status = 'fail'
                            result.failures.push(
                                `known failure has ended: this entry already ${describeEndCondition(entry.test.__knownFailEndsWhen)}` +
                                    ' -- delete the mark',
                            )
                        } else {
                            result.status = 'xfail'
                        }
                    } else {
                        result.status = 'fail'
                        result.failures.push(
                            `marked known_fail` +
                                (entry.test.__backend
                                    ? ` on '${entry.test.__backend}' but passes: remove the mark`
                                    : ' but passes, so the mark is out of date and must be removed'),
                        )
                    }
                }
                entry.outcome = {
                    status: result.status,
                    result,
                    metric: formatMetric(result),
                    failures: result.failures,
                }
            } finally {
                if (flight !== null) leaveInFlight(flight)
                if (probeHeld) probes.release(1)
                // The test's own directory is not this run's evidence: it goes whether the
                // test passed or failed. The run root stays on failure (main prints it).
                const removeStarted = performance.now()
                if (scratch) {
                    fs.rmSync(scratch, { recursive: true, force: true })
                }
                clock.removeAt = performance.now()
                clock.removeMs = clock.removeAt - removeStarted
                releaseSlot()
            }
        })()
            .catch((error) => {
                // A failure of the runner itself (a precondition helper, a verification script the
                // scheduler lost) must not lose the output of the tests around it: it becomes the
                // result of this test and the run continues to its summary.
                const casePath = entryCasePath(entry.test)
                const result = runnerErrorResult(casePath, error && error.message ? error.message : String(error))
                entry.outcome = { status: 'fail', result, metric: '', failures: result.failures }
            })
            .finally(() => {
                clock.settledAt = performance.now()
                entry.timing = slotTiming(entry, clock, marks, logWindows)
                entry.settled = true
                drain()
            })
    }

    await Promise.all(entries.map((entry) => entry.promise))
    drain()
    // A timeout is the one verdict a loaded host can hand a case that did nothing wrong: the child
    // is killed at its bound, and a starved case and a slow one read the same in that kill. So every
    // case whose bound took it gets one more attempt -- at the end of the run, one at a time, with
    // twice the bound (`runOneTest` doubles it from `logContext.rerun`). Nothing else is re-run: a
    // case that failed with an exit code, a diagnostic or a verification failure has answered its
    // question with its own output.
    //
    // Serial and after every other case, because a timeout that came from being starved must not be
    // re-tested beside the same crowd it was starved by. Nothing takes a slot here: there is nothing
    // left to run beside it, and the verification lane is the only permit still needed.
    //
    // The second attempt is judged by `runOneTest` -- the same assertions, the same normalizers, the
    // same CPU-budget and timeout-margin rules -- so the re-run decides exactly what the first
    // attempt would have decided given room. It is a run, not an excuse: a case that times out
    // again stays red, and one that dies by a signal on the second attempt is a failure too.
    const rerunWaiting = entries.filter((entry) => entry.rerunPending === true)
    for (const entry of rerunWaiting) {
        const { plan, test } = entry
        const first = entry.outcome.result
        const load = hostLoadReading()
        // A plan header for a case whose plan's block is already above: the line is printed where
        // the second attempt happened, and a bare case name at the end of a multi-plan run would
        // not say which plan it belongs to. Printed only when the plan changes, so a run whose
        // timeouts all sit in one plan gets one header.
        if (plan !== printedPlan) {
            printPlanHeader(plan)
            printedPlan = plan
        }
        const scratch = createTestScratchDir(test)
        const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
        const flight = enterInFlight(test)
        let result
        try {
            result = await runOneTest(
                test,
                sharedVars,
                {
                    options,
                    completed,
                    planId: plan.id,
                    // Not `slotTiming`'s: a second attempt is not a slot of the layer's schedule,
                    // and pretending it were would move the `[SLOT]` line's intervals by a number
                    // the layer never paid. Its duration is on its line and in its `meta.json`.
                    clock: null,
                    verifications,
                    releaseSlot: null,
                    rerun: { attempt: 2 },
                },
                scratch,
                scratchRelative,
                {}
            )
        } catch (error) {
            result = runnerErrorResult(
                entryCasePath(test),
                error && error.message ? error.message : String(error)
            )
        } finally {
            leaveInFlight(flight)
        }
        // Both attempts on the case's record: the first attempt's bound and duration, the load the
        // second was taken under. `first_pass` is named as an attempt, so nothing here reads as a
        // field of the case that has two meanings.
        result.rerun = {
            attempt: 2,
            load,
            first_pass: { wall_ms: first.wallMs, timeout_ms: first.timeoutMs, timed_out: true },
        }
        const outcome = { status: result.status, result, metric: formatMetric(result), failures: result.failures }
        // The row of this case in `test_timings[]` is written when its first attempt settles; the
        // verdict that decides the case is the second one, so the row is brought up to it. A reader of
        // the summary (the promotion) then finds one final status per case and does not re-run it.
        if (entry.timing) {
            entry.timing.status = outcome.status
            entry.timing.timed_out = result.timedOut === true
            entry.timing.rerun = { first_timed_out: true, status: outcome.status }
        }
        // One case, one verdict, one line: the summary counts the case once (`total` does not move
        // with an attempt), `rerun` counts the attempt, and `pass` counts the verdict it reached.
        summary.rerun += 1
        if (outcome.status === 'pass') {
            summary.pass += 1
            summary.rerunPassed += 1
        } else {
            summary.fail += 1
            if (result.timedOut) summary.timedOut += 1
            else if (result.signal) summary.crashed += 1
        }
        transcript.caseLine(printStatusLine(outcome.status, test.name, rerunNote(outcome.status, result, load)))
        if (options.listKnownRows) {
            const firstFailure = Array.isArray(outcome.failures) && outcome.failures.length > 0
                ? outcome.failures[0]
                : null
            options.listKnownRows.push({
                name: listedName(plan, test),
                verdict: outcome.status,
                reading: normalizeReading(firstFailure),
                reading_version: READING_NORMAL_VERSION,
            })
        }
        if (outcome.status !== 'pass') {
            const pointer = fullOutputLine(logPathOf(plan, test, outcome.status, logDir, RERUN_SECOND_DIR))
            printDetailLines([pointer])
            printDetailLines(outcome.failures)
            printMatchDiagnostics(result.matchDiagnostics)
            printFailedOutput(result, pointer)
        }
        writeLogFiles(plan, test, result, outcome.status, logDir, RERUN_SECOND_DIR)
        // What a later case reads: a `passes` condition that waits on this case's verdict sees the
        // attempt that decided it, not the one that timed out.
        completed.set(`${plan.id}/${test.name}`, result)
        completed.set(test.name, result)
        fs.rmSync(scratch, { recursive: true, force: true })
    }
    settlePassesConditions(entries, completed, summary)
    const stalls = stopLoopMonitor()
    const timings = []
    for (const entry of entries) {
        if (!entry.timing) continue
        entry.timing.log_write_ms = roundMs(entry.logWriteMs || 0)
        timings.push(entry.timing)
    }
    return { summary, wallMs: performance.now() - startedAt, timings, stalls, logWindows, startedAt, transcript }
}

async function main() {
    let options
    try {
        options = parseArgs(process.argv.slice(2))
    } catch (error) {
        if (!error || !error.argvRefusal) throw error
        // One line for the cause, then this tree's own argv and commit: those two are what a caller
        // comparing versions needs, and they are printed here because the run never starts -- there
        // is no summary, no provenance line and no log directory to look in.
        console.error(`${COLORS.red}[ERROR]${COLORS.reset} ${error.message}`)
        console.error(`[ARGV] this runner knows: ${KNOWN_ARGV.join('  ')}`)
        const sha = gitOutput(['rev-parse', 'HEAD']) || 'unknown'
        const when = gitOutput(['log', '-1', '--format=%cI']) || 'unknown'
        console.error(`[ARGV] this tree is ${sha} (committed ${when}); a word this list lacks is newer than it`)
        process.exit(1)
    }
    if (options.printTimeSensitive) {
        // A question about plan files, not a run: nothing executes and no lock is taken, which is
        // what lets the gate ask it before it decides to take the host.
        const status = printTimeSensitive(options)
        process.exitCode = status
        return status
    }
    if (options.listCases) {
        // A question about the plan tree, not a run: nothing executes, so no install is consulted.
        const status = listSelectedCases(options)
        process.exitCode = status
        return status
    }
    // An empty selection starts no plan, so no binary is consulted: refusing here for a missing
    // install would turn "nothing to run" into a red about a build, which is a different question.
    if (!options.selectionEmpty) preflightBuild(options)
    // Writing golden files from several tests at once would make the content depend on which
    // test finished last; update-golden is a deliberate, serial maintenance run.
    if (options.updateGolden && options.jobs !== 1) {
        console.log(`${COLORS.cyan}[INFO]${COLORS.reset} --update-golden writes golden files: running serially (--jobs 1)`)
        options.jobs = 1
    }
    const sharedVars = loadVars(options)
    const namedPlans = options.targets.filter((target) => {
        try {
            return fs.statSync(target).isFile() && !options.layeredPlans.has(path.resolve(target))
        } catch {
            return false
        }
    })
    // A recorded case selection (`--changed` hands one over) runs exactly those cases. It is a
    // narrowing of the same load, not a second one: the plans are still the selection's targets, and
    // `keep` decides which of their entries the change touched.
    const selection = options.selectionCases === null ? null : readCaseSelection(options.selectionCases)
    const keep = selection === null ? null : (name) => selection.names.has(name)
    const selected = loadPlans(
        options.targets,
        options.tier,
        options.layeredPlans,
        options.compileOnly === true,
        options.selectionEmpty === true,
        keep
    )
    const plans = selected.plans
    if (selection !== null) refuseRecordThatNarrowsLayer(options, selection, selected)
    // A name the recorded selection carries that this run would not start is a stale or wrong
    // record, and running the rest of it would be a run that silently proves less than the record
    // says. It is refused with the base the record names, never dropped.
    if (selection !== null) {
        const missing = missingCaseNames(plans, selection.names)
        if (missing.length > 0) {
            const shown = missing.slice(0, 5).join(', ')
            const rest = missing.length > 5 ? ` and ${missing.length - 5} more` : ''
            console.error(
                `${COLORS.red}[PLAN ERROR]${COLORS.reset} --selection-cases names ${missing.length} case(s) ` +
                    `this run would not start: ${shown}${rest}. The record is stale or was taken against ` +
                    'another tree, so nothing is started.'
            )
            process.exitCode = 2
            return 2
        }
    }
    // A target that reached no plan file refuses the run before anything starts: nothing would run,
    // so no [SUMMARY] could say anything true about it. It gets a sentence of its own -- the layer
    // sentence further down answers "these plans belong to another tier", a different question and
    // the wrong answer for a path that holds no plan -- and it names the plan file beside the
    // directory when there is one, because that is the mistake this refusal exists for:
    // `test/plans/feat/docs_links` is the fixture directory beside `docs_links.plan.toml`.
    // Written to stdout on purpose: the helper that runs this on the host loses the run's stderr,
    // so a refusal written there would be a red with no reason (the old
    // `[FAIL] this run selected no test at all` line never reached the operator from a host run).
    if (selected.reachedNothing.length > 0) {
        for (const target of selected.reachedNothing) {
            const beside = `${path.resolve(target)}.plan.toml`
            const hint = fs.existsSync(beside) ? `; the plan file beside it is ${relPath(beside)}` : ''
            console.log(
                `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${relPath(path.resolve(target))} holds no *.plan.toml, ` +
                    `so this run would start nothing${hint}. A target is a *.plan.toml or a directory that ` +
                    'contains one.',
            )
        }
        console.log(
            `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${selected.reachedNothing.length} target(s) named no plan, ` +
                'so nothing was started: a selection error, not a run of zero tests.',
        )
        process.exitCode = 2
        return 2
    }
    // A plan that names an option value the CLI does not take is a plan error, and the run refuses
    // instead of starting it: the CLI would exit 1 while parsing, and that exit code is not a
    // reading about the program under test. `plan:line` and the legal values are in the message so
    // the fix is at the file, not in a GC investigation.
    if (selected.argProblems.length > 0) {
        for (const problem of selected.argProblems) {
            const where = problem.line ? `${relPath(problem.planPath)}:${problem.line}` : relPath(problem.planPath)
            console.error(
                `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${where}: test '${problem.testName}': ` +
                    `${problem.word} -- ${problem.reason}; the plan cannot say that`,
            )
        }
        console.error(
            `${COLORS.red}[PLAN ERROR]${COLORS.reset} ${selected.argProblems.length} problem(s) in ` +
                'the plans this run selected make their verdict meaningless, so nothing was started: this ' +
                'is a plan error, not a failing test. Option values are listed in ' +
                'test/tools/cli-arg-domains.mjs (checked against src/tools/camel-cli/config.cpp); the stdout ' +
                'assertions are described in docs/test-plans.md.',
        )
        process.exitCode = 2
        return 2
    }
    // A plan this run reaches and starts nothing in is not a silent zero. The ones whose own tier
    // is another layer are counted and named on one line -- the milestone layer under a fast run is
    // the usual case -- and the ones whose own tier is this layer are failures: the run would print
    // a [SUMMARY] that says nothing about them.
    if (selected.unproven.length > 0) {
        for (const problem of selected.unproven) console.error(`${COLORS.red}[FAIL]${COLORS.reset} ${problem}`)
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} ${selected.unproven.length} plan file(s) started no test ` +
                'in this run and their own tier is the layer it covers; the run proves nothing about them',
        )
        process.exitCode = 1
        return 1
    }
    // `--list-known` adds a list from here on: one line per test, `{name, verdict, reading}`, after
    // the run's own output. The narration is kept, and that is a change of contract with a reason:
    // the list used to replace the narration because its only reader was a person filtering a whole
    // transcript, and a status line among the rows would have been one more thing to filter. The
    // job that reads it now (`internal/tools/camel-remote.ps1 -ListKnown`) brackets the rows between two markers on the way back, so
    // nothing has to be filtered and the run that records rows is still a run whose console a
    // person can watch. Nothing else changes: the same tests in the same order, the same log files
    // and summary.json, the same exit code.
    if (options.listKnown) options.listKnownRows = []
    // What the layer declares against what this run starts. The two numbers differ for a reason
    // that is not a defect -- `backends` expands one entry into several tests, `compare_to` pulls a
    // companion in -- and for a whole night the layer's size was quoted as one of them while a run
    // of it was the other (`3330 declared / 3404 started`). Printing both is the cure; the check is
    // the direction that is a defect: an entry declared for this layer that neither ran nor was
    // skipped appears in no summary, so a plan would prove less than its file says while looking
    // green. The started count is counted, never computed: `backends.length` times the entries reads
    // 7339 for a layer that starts 3404.
    {
        const rows = selected.tierAccounting || []
        console.log(`${COLORS.cyan}[tier]${COLORS.reset} ${tierAccountingLine(selected.tier, rows)}`)
        const dropped = tierAccountingProblems(rows)
        if (dropped.length > 0) {
            for (const problem of dropped) console.error(`${COLORS.red}[FAIL]${COLORS.reset} ${problem}`)
            console.error(
                `${COLORS.red}[FAIL]${COLORS.reset} ${dropped.length} plan file(s) declare more tests of ` +
                    `the '${selected.tier}' layer than this run started; a declared entry that neither runs ` +
                    'nor is skipped is a defect in the selection, not a plan that proves nothing',
            )
            process.exitCode = 1
            return 1
        }
    }
    // The pid keeps two runs started in the same second (the self-test does this) out of one
    // directory, whose logs and summary.json would otherwise overwrite each other.
    const logDir = path.join(LOG_ROOT, `${timestamp()}_${process.pid}`)
    // Retention before the record. The tree this run is about to add a directory to is the one that
    // grew without bound (test/tools/runner-output.mjs has the policy and the evidence): it keeps
    // the newest RUN_DIRS_KEPT run directories, never one a live run owns, and never one this
    // runner did not write. The current run's directory is named here, before it exists, so the
    // policy protects it by identity as well.
    const retention = pruneRunnerOutput({
        resultsRoot: RESULTS_ROOT,
        keep: resultsKeep(),
        currentRunDir: logDir,
        currentScratchRoot: TEST_SCRATCH_RUN,
    })
    ensureDir(LOG_ROOT)
    // A directory left by an earlier process whose pid this run reused is not this run's log, the
    // same rule the scratch root follows below: its files must not be read as this run's.
    fs.rmSync(logDir, { recursive: true, force: true })
    ensureDir(logDir)
    writeRunManifest(logDir, options, plans)
    const testCount = plans.reduce((count, plan) => count + plan.tests.length, 0)

    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} camel executable (${options.build}): ${camelBinary(options)}`)
    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} discovered plans: ${plans.length}`)
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} tier: ${options.tier}, tests: ${testCount}, jobs: ${options.jobs}`
    )
    if (testCount === 0 && options.targets.length > 0) {
        // A run that starts nothing is not a pass: every claim it could make is about tests it did
        // not run. (No target at all is not that zero: `loadPlans` reads it as the whole plan tree.
        // That widening is why the `--changed` caller never lets a measured empty selection arrive
        // here -- `test/runner.mjs#announceEmptySelection` prints `changed: 0 target(s)` and starts
        // no plan, so the zero is a reading and not a run of the layer.) A target that reached no
        // plan file at all was already refused above: this is the other zero -- the plan files were
        // read and none of their entries belongs to this layer -- so the number is the count before
        // the filter (`plans` is the post-filter list and reads 0 here) and the flag that lifts it
        // is named. Both are counted, never inferred from each other.
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} this run selected no test at all: ` +
                `${selected.reachedButFiltered} plan file(s) were read and none of their entries is in the ` +
                `'${options.tier}' layer (--tier all runs every layer)`,
        )
        process.exitCode = 1
        return 1
    }
    if (selected.emptyByLayer.length > 0) {
        // Named, so that a zero is a reading and not an absence: these plan files were reached and
        // started nothing because their own tier says they belong to another layer.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} plans of another layer, started nothing here: ` +
                `${selected.emptyByLayer.length} (their own tier selects them: --tier slow, --tier milestone, --tier debug)`
        )
    }
    if (!CAN_READ_CHILD_CPU) {
        // Said once, and only where it changes what the run means: on this host no test's timeout
        // margin is judged, because the kernel offers no CPU figure and the wall clock is not a
        // reading of the test's work. The timeout itself is still the child's own bound.
        console.log(
            `${COLORS.yellow}[WARN]${COLORS.reset} no /proc/self/stat here: a test's timeout margin is ` +
                `${NO_CPU_TIME_SKIP} on this host (the shared host judges it on CPU time); a test that ` +
                `runs past its timeout still fails, and this rule only asks whether a passing test left room`
        )
    }
    console.log(`${COLORS.cyan}[RETAIN]${COLORS.reset} ${formatRetention(retention)}`)
    if (options.layeredPlans.size > 0 && options.tier !== 'all') {
        // `--changed` marked these plans `layer: true`: naming them is not "run the whole file",
        // it is "run the cases of this layer", so the slow cases that must not run from an
        // ordinary change are filtered while the fast ones still run.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} selected plan target(s) follow --tier ${options.tier}: ${options.layeredPlans.size}`
        )
    }
    if (namedPlans.length > 0 && options.tier !== 'all') {
        // Naming a plan is the user's intent: it runs in full, whatever `--tier` says. A
        // directory target still selects the layer (see loadPlans).
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} named plan target(s) run in full: ${namedPlans.length} (the tier filter leaves a named plan alone)`
        )
    }
    if (options.selectionEmpty) {
        // The zero below is the answer, not a layer that failed to fill: the selection was measured
        // empty before this process started. It says which zero it is, because the summary of a run
        // that starts nothing is the same three numbers as the summary of a green one.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} --selection-empty: ${plans.length} plan file(s) selected, ` +
                '0 started. The plan tree was not widened to fill the selection; name a plan or --all ' +
                'to run a layer'
        )
    } else if (testCount === 0 && options.tier !== 'all') {
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} this selection has no ${options.tier} tests: --tier all runs both layers`
        )
    }

    // The run owns tmp/test/run-<pid>: a root left by an earlier process with this pid (the pid
    // was reused) must not be read as this run's files.
    fs.rmSync(TEST_SCRATCH_RUN, { recursive: true, force: true })
    // What the tree was before the first test, and a watcher that records when a path appears
    // beside the tests that were running at that moment. Both are read here and judged after the
    // last test: the question is what *this run* left, so the reading has to bracket it
    // (test/tools/tree-guard.mjs). **One phenomenon, one verdict**: a nested run's window lies
    // inside this one's, so a nested run reads no tree and starts no watcher -- a file a sibling
    // test wrote inside that window and removed again is an annotation here and was a leak there.
    // Everything this run starts is inside its window, so every child inherits `nested` and any
    // runner among them defers; marking the environment once is what keeps a fourth spawn site from
    // quietly becoming a second judge.
    const treeNested = TREE_ROLE === 'nested'
    markChildrenNested(process.env)
    // The pre-test window is two readings that do not depend on one another: the tree's `git status`
    // and the watcher over it. Both are taken before the first test -- the walk is the coverage and
    // cannot be skipped or deferred (test/tools/tree-guard.mjs#readTreeStateAsync records what was
    // measured and rejected) -- but they are started together, so the run pays the longer of the two
    // instead of their sum. `treeSeenAt` is the window's start for the same reason: the timestamps
    // the attribution prints are measured from before both readings, not from between them.
    const treeSeenAt = performance.now()
    const treeBeforeReading = treeNested ? null : readTreeStateAsync(REPO_ROOT)
    const treeSeen = new Map()
    let treeWatch = null
    if (!treeNested) {
        treeWatch = startTreeWatch(REPO_ROOT, {
            onPath: (relative) => {
                // The first moment it appeared: a file written in one burst would otherwise move its own
                // timestamp on every event, and the answer is which tests were running when it was made.
                if (treeSeen.has(relative)) return
                treeSeen.set(relative, { at: performance.now() - treeSeenAt, running: [...inFlight.values()] })
            }
        })
    }
    const treeBefore = treeNested ? null : await treeBeforeReading
    const outcome = await executePlans(plans, options, sharedVars, logDir)
    // Both readings before `stop()`: it closes the watchers and forgets how many there were, and
    // the number is what says the walk found the tree instead of watching nothing.
    const treeWatched = treeWatch ? treeWatch.watched() : 0
    const treeWatchFailure = treeWatch ? treeWatch.failure() : null
    if (treeWatch) treeWatch.stop()
    let treeVerdict
    if (treeNested) {
        treeVerdict = judgeTree({ role: TREE_ROLE })
    } else {
        treeVerdict = judgeTree({
            before: treeBefore,
            after: readTreeState(REPO_ROOT),
            firstSeen: treeSeen,
            updateGolden: options.updateGolden,
            failure: treeWatchFailure,
            watched: treeWatched,
            role: TREE_ROLE
        })
    }
    const testRootKept = outcome.summary.fail > 0
    if (testRootKept) {
        // The per-test directories are gone, but what a program wrote outside them (and the
        // root itself) is evidence: say where it is instead of removing it.
        console.log(`${COLORS.cyan}[INFO]${COLORS.reset} test scratch kept: ${TEST_SCRATCH_RUN}`)
    } else {
        fs.rmSync(TEST_SCRATCH_RUN, { recursive: true, force: true })
    }

    const split = gateSplit(outcome.wallMs)
    const cpuReadings = summarizeCpuReadings(outcome.timings)
    const slotTiming = summarizeSlotTimings(
        outcome.timings,
        outcome.wallMs,
        options.jobs,
        outcome.stalls,
        outcome.logWindows,
        outcome.startedAt
    )
    fs.writeFileSync(
        path.join(logDir, 'summary.json'),
        JSON.stringify(
            {
                generated_at: new Date().toISOString(),
                camel_exe: camelBinary(options),
                build: options.build,
                update_golden: options.updateGolden,
                tier: options.tier,
                jobs: options.jobs,
                wall_ms: outcome.wallMs,
                build_seconds: split.buildSeconds,
                test_seconds: split.testSeconds,
                total_seconds: split.totalSeconds,
                test_root: TEST_SCRATCH_RUN,
                test_root_kept: testRootKept,
                summary: outcome.summary,
                // Where the slots went, for every test: the slot held before a child existed and
                // after it was gone, plus the aggregates and the tests that own most of it.
                slot_timing: slotTiming,
                // Whether this host's CPU figures exist at all, and why not where they do not: the
                // same statement the `[WARN]` line makes, in the record, so that `cpu_ms: null` in a
                // `test_timings[]` row has a reason attached to the run it belongs to.
                cpu_readings: cpuReadings,
                test_timings: outcome.timings,
                // What the run left in the checkout, in the same record as everything else it
                // measured: the paths, the tests that were running when each appeared, and the two
                // readings the verdict is the difference of (test/tools/tree-guard.mjs).
                tree: treeVerdict,
            },
            null,
            2
        )
    )

    // A run that starts no test measures nothing, and the code says which kind of nothing: 2 is
    // "not measured" -- the same code a missing install and an unreadable selection get -- while 0
    // would read as a green run (a zero-row `[SUMMARY]` is the same three numbers a green one
    // prints) and 1 would blame a test that never ran. `--selection-empty` is the way a gate reaches
    // this on purpose: a change whose plans hold no case in this layer. Refusing to call that a pass
    // is the point -- a run that proves nothing is not a run that found nothing wrong.
    //
    // The count is the summary's own four fields, the same sum `printRunSummary` prints as `total`:
    // the summary has no `total` field, and reading one would have made this condition always false
    // -- the run would have exited 0 while these words said 2.
    const nothingStarted =
        outcome.summary.pass + outcome.summary.fail + outcome.summary.skip + outcome.summary.xfail === 0
    if (nothingStarted) {
        // Before the `[provenance]` line, which has to stay right before `[SUMMARY]`
        // (test/cases/tools/provenance.mjs pins the pair): the sentence explains the numbers that
        // follow, so it goes on the near side of them.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} nothing was started, so nothing was measured: ` +
                'exit 2, which is not a green run and not a red one'
        )
    }
    printProvenance(options)
    printRunSummary(outcome.summary, outcome.transcript)
    // After the numbers, and not between them and the line that states what they belong to: a
    // `[provenance]` line sits immediately before `[SUMMARY]` by its own rule
    // (test/cases/tools/provenance.mjs pins it), and this is the one line that says whether the
    // tree that line named is still the tree that ran.
    printTreeVerdict(treeVerdict)
    printKnownRoster(outcome.summary)
    if (marginSkippedTests > 0) {
        // The rule's own reading, so a run on a host without a CPU figure says what it did not judge
        // and how often, instead of leaving 105 bounds looking judged.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} timeout margin ${NO_CPU_TIME_SKIP} on ` +
                `${marginSkippedTests} test(s): this host's wall clock is not a reading of a test's work`
        )
    }
    if (budgetSkippedTests > 0) {
        // The other half of the same reading: a `cpu_budget_ms` entry whose budget this host cannot
        // read at all. Printed apart from the pass count, because a bound nobody read is not a bound
        // that held.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} cpu_budget_ms ${NO_CPU_TIME_SKIP} on ` +
                `${budgetSkippedTests} test(s): a cost bound this host cannot read is not a bound it crossed`
        )
    }
    // One line for the flow, with the whole accounting behind it in summary.json: the ratio the
    // task is about, and the intervals that decide whether the ratio is the host's or the runner's.
    console.log(
        `${COLORS.cyan}[SLOT]${COLORS.reset} concurrency ${slotTiming.avg_concurrency}/${options.jobs}`
        + `, slots held ${slotTiming.avg_slots_held}/${options.jobs}`
        + `, children ${(slotTiming.child_ms_total / 1000).toFixed(1)} s`
        + `, no-child ${(slotTiming.no_child_ms_total / 1000).toFixed(1)} s`
        + ` (pre-spawn ${(slotTiming.pre_spawn_ms.total / 1000).toFixed(1)} s,`
        + ` post-exit ${(slotTiming.post_exit_ms.total / 1000).toFixed(1)} s)`
        + `, probes ${(slotTiming.probe_ms_total / 1000).toFixed(1)} s`
        + ` (in slot ${(slotTiming.probe_in_slot_ms_total / 1000).toFixed(1)} s)`
        + `, verifications ${(slotTiming.verify_ms_total / 1000).toFixed(1)} s`
        + ` (in slot ${(slotTiming.verify_in_slot_ms_total / 1000).toFixed(1)} s)`
        + `, log writes ${(slotTiming.log_write_ms_total / 1000).toFixed(1)} s`
        + ` (blocking ${(slotTiming.log_block_ms_total / 1000).toFixed(1)} s of slots)`
        + `, serial ${slotTiming.serial_tests} tests locking ${(slotTiming.serial_exclusive_ms_total / 1000).toFixed(1)} s`
        + `, empty capacity ${(slotTiming.empty_slot_ms_total / 1000).toFixed(1)} s`
        + `, after the last release ${((slotTiming.plan_wall_ms - slotTiming.last_release_ms) / 1000).toFixed(1)} s`
        + ` (${(slotTiming.post_release_per_slot_ms / 1000).toFixed(1)} s of it per slot)`
        + `, loop stalls ${(slotTiming.loop_lag.stall_ms_total / 1000).toFixed(1)} s`
    )
    // The per-case CPU record, in one line: how many cases carry a figure of their own and what
    // the rest say. A run where the second number is everything is not a run whose costs are
    // unknown by accident -- it is a run on a host that cannot report them, and the line quotes
    // the sentence its cases carry rather than leaving the column empty.
    console.log(
        `${COLORS.cyan}[CPU]${COLORS.reset} ${slotTiming.cpu.read}/${slotTiming.tests} case(s) carry`
        + ` the CPU time they cost (${(slotTiming.cpu.cpu_ms_total / 1000).toFixed(1)} s)`
        + (slotTiming.cpu.absent > 0
            ? `, ${slotTiming.cpu.absent} say why not: ${slotTiming.cpu.absent_reading.join(' | ')}`
            : '')
    )
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} wall: ${(outcome.wallMs / 1000).toFixed(1)} s, jobs: ${options.jobs}, tier: ${options.tier}`
    )
    // The pool's own reading, beside the wall clock it is paid out of: how many cases took a token, the
    // high-water mark the pool recorded for itself, and the time the run spent queuing rather than
    // running. `peak` is the number a person checks against "two runs and a build must stay under 24".
    if (TOKEN_POOL) {
        let peak = 0
        try {
            for (const name of fs.readdirSync(process.env.CAMEL_TOKEN_DIR)) {
                const match = /^peak-(\d+)$/.exec(name)
                if (match) peak = Math.max(peak, Number(match[1]))
            }
        } catch {
            // The pool directory can be gone by now (a caller that cleans up after itself). The rest of
            // the line is still the run's own reading, so it is printed without the peak.
        }
        console.log(
            `${COLORS.cyan}[TOKEN]${COLORS.reset} pool ${process.env.CAMEL_TOKEN_DIR} peak=${peak}`
            + `, cases with a token ${tokenTaking.cases}`
            + `, queued ${tokenTaking.queued}`
            + `, tokenWaitSeconds=${(tokenTaking.waitMs / 1000).toFixed(1)}`
        )
    }
    console.log(formatGateSplit(split))

    // Two ways to fail, and both are red: a test that failed, and a run that left a path behind in
    // the checkout it was measuring. The second is not cosmetic -- it is what makes the next
    // `[provenance]` line unable to name the tree the reading belongs to, and what the wrapper
    // carries to the host on the next run. A tree that could not be read at all counts as the
    // second: `could not ask` is not `there is nothing` (test/tools/tree-guard.mjs#judgeTree).
    // `nothingStarted` keeps the 2 it always had, and the transcript verdict below can only raise
    // this to 2: neither is 1, because neither is a measurement that came out wrong.
    let exitCode = nothingStarted ? 2 : outcome.summary.fail > 0 || treeVerdict.failed === true ? 1 : 0
    // A third answer, and not a variant of the two above: whether this run's record holds the
    // cases it counted. The count is the summary's own total and is not adjusted; the transcript is
    // the file the run wrote, read back from disk (test/tools/transcript.mjs). A run whose record
    // and count disagree did not measure a set of cases, so it is refused with exit 2 -- the code
    // this runner already gives a run that could not be read (`preflightBuild`, the `--changed`
    // refusal): 1 means "measured, and not right", and this is neither.
    const transcriptVerdict = judgeTranscript(outcome.transcript, summaryTotal(outcome.summary))
    if (!transcriptVerdict.ok) {
        console.log('')
        console.log(`${COLORS.red}[PLAN ERROR]${COLORS.reset} ${transcriptRefusal(transcriptVerdict)}`)
        console.log(
            `    ${relPath(transcriptVerdict.file)} is the record this run wrote: ${transcriptVerdict.lines} ` +
                `case line(s) read back from it, ${transcriptVerdict.total} counted by the summary above. ` +
                'The count was not changed to match the transcript, and the truncation was not switched ' +
                'off: a run whose record is short of its count is not a red reading, it is one that cannot ' +
                'be read at all (exit 2).'
        )
        exitCode = 2
    }
    // The whole of `--list-known`'s output, emitted last: one `{name, verdict, reading}` per test.
    // Last because the reader is a program that takes the lines after this point, and every line
    // before it is the run a person reads -- the summary, the tree verdict, the roster, the
    // timings are all already printed, through their own functions, exactly as they are in a run
    // without the flag.
    if (options.listKnown) {
        for (const row of options.listKnownRows) console.log(JSON.stringify(row))
    }
    // This block is the reason the flush exists: it is the one output printed as one burst at the
    // very end, and on a host run the exit used to end the process with part of it still queued.
    await flushOutput()
    process.exit(exitCode)
}

// Only a command that names this file as its entry point starts a run: the plan inventory imports
// `loadPlanTests` and `expandSweeps` from here, and importing a module must not run the tests.
if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    main().catch((error) => {
        if (error && error.planRefusal === true) {
            // A recorded selection this run cannot read: one line and exit 2, no stack. The stack a
            // throw prints says which line of this file refused, which is not the question a reader
            // of a gate log has; and exit 1 would say a test failed when nothing was started.
            console.error(`${COLORS.red}[PLAN ERROR]${COLORS.reset} ${error.message}`)
            console.error(
                `${COLORS.cyan}[INFO]${COLORS.reset} nothing was started, so nothing was measured: exit 2`
            )
            process.exit(2)
        }
        console.error(error && error.stack ? error.stack : String(error))
        if (fs.existsSync(TEST_SCRATCH_RUN)) {
            console.error(`${COLORS.cyan}[INFO]${COLORS.reset} test scratch kept: ${TEST_SCRATCH_RUN}`)
        }
        process.exit(1)
    })
}
