#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
//
// Turns the three rounds of `run-timing.mjs` output into the handover table.
//
// The grid the dispatch asks for is 5 programs x 4 back ends x 3 points = 60 cells. `std::prl`
// is ONE of those four, and its three thread counts are three readings inside that one cell, so
// the main table has 20 rows and every `prl` cell carries `1t / 4t / 16t` side by side. The
// per-thread expansion (30 rows) is written to the JSON beside the page for anyone who wants it.
//
// Every number is a WALL-CLOCK reading taken on the owner's workstation while it was contended:
// the seven runaway `check-operator-panic.mjs` processes held ~7 of 28 cores throughout. The page
// says so in the header and each section; nothing here is offered as an idle-machine baseline.

import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const RESULTS = path.join(HERE, 'results')

const POINTS = [
    { label: 'audit-15165a9333', short: '11-13 审计', sha: '15165a9333' },
    { label: 'pre-stage2-1fa180c4f', short: '阶段 2 之前', sha: '1fa180c4f' },
    { label: 'integration-6fcee16d0c', short: '当前 integration', sha: '6fcee16d0c' },
]

const PROGRAMS = [
    { id: 'call', name: 'fib(30)' },
    { id: 'loop', name: 'small_calls (2e6 循环)' },
    { id: 'tensor', name: 'tensor_train' },
    { id: 'branch', name: 'MCTS' },
    { id: 'closure', name: 'closures' },
]

const BACKENDS = ['nvm', 'fvm', 'jit', 'prl']

function readJson(p) {
    return JSON.parse(fs.readFileSync(p, 'utf8'))
}

function loadPoint(p) {
    const cells = new Map()
    for (const name of [`${p.label}.timeit.json`, `${p.label}.process.json`]) {
        const f = path.join(RESULTS, name)
        if (!fs.existsSync(f)) continue
        const doc = readJson(f)
        for (const r of doc.results) {
            cells.set(`${r.program}/${r.backend}`, { ...r, point: p.label, busy: doc.busy_cores })
        }
    }
    const stdoutFile = path.join(RESULTS, `${p.label}.stdout.json`)
    const stdout = fs.existsSync(stdoutFile) ? readJson(stdoutFile) : null
    const timeitDoc = path.join(RESULTS, `${p.label}.timeit.json`)
    const processDoc = path.join(RESULTS, `${p.label}.process.json`)
    const meta = {
        busy_cores: null,
        started: null,
        measurement_mixed: fs.existsSync(processDoc),
        build: null,
    }
    if (fs.existsSync(timeitDoc)) {
        const d = readJson(timeitDoc)
        meta.busy_cores = d.busy_cores
        meta.started = d.started
        meta.host = d.host
        meta.cores = d.cores
    }
    return { cells, stdout, meta }
}

const points = POINTS.map((p) => ({ ...p, ...loadPoint(p) }))

function num(x) {
    return typeof x === 'number' && isFinite(x) ? x : null
}

function fmt(x) {
    if (x === null) return '—'
    return x >= 100 ? Math.round(x).toString() : x.toFixed(1)
}

/** One cell of the main table: `first / steady`, or the prl triple. */
function cellText(pt, program, backend) {
    const ids = backend === 'prl' ? ['prl-1', 'prl-4', 'prl-16'] : [backend]
    const parts = []
    for (const id of ids) {
        const c = pt.cells.get(`${program}/${id}`)
        if (!c) {
            parts.push('未取')
            continue
        }
        if (c.exit !== 0 || num(c.steady_ms) === null) {
            parts.push('失败')
            continue
        }
        const tag = backend === 'prl' ? `${id.split('-')[1]}t ` : ''
        parts.push(`${tag}${fmt(num(c.first_ms))} / ${fmt(num(c.steady_ms))}`)
    }
    return parts.join(' · ')
}

function ratio(newer, older) {
    if (newer === null || older === null || older === 0) return null
    return newer / older
}

const lines = []
const W = lines.push.bind(lines)

W('# 后端提速：三个点的读数（工作站的读数，contended）')
W('')
W('**机器 `KOGLER-WORKSTATION`，28 逻辑核。全部是本页量的，不与 88 主机构建机的读数相比。**')
W('')
W('> **contended（原因：七个 `scripts/check-operator-panic.mjs` 各 ~1.00 核、累计 4000–5400 CPU-秒，25–31% 背景）**')
W('> ⇒ **只做点对点比较，不是空闲基线**；每行带实测 load；安静窗口出现就整组重跑；**交付数一律不取工作站的 contended 值**。')
W('')
W('安静窗口的硬线：忙核数 < 核数的 15%（28 核 ⇒ < 4.2）且连续 60 秒不越过（`load-gate.ps1`）。本轮 60 秒窗口的 15 个采样全部在 7.66–13.89 之间，判 CONTENDED。')
W('')

W('## 读这张表必须知道的四件事')
W('')
W('### 一、三个点的构建选项不完全相同')
W('')
W('| 点 | `build-info.json` 的选项哈希 | 选项 |')
W('|---|---|---|')
W('| 11-13 审计 | `97805b1e103e` | `CAMEL_ENABLE_PYTHON=ON`, `CAMEL_TENSOR_BLAS=ON`, **`ENABLE_FASTVM_COMPUTED_GOTO=ON`**, `ENABLE_FASTVM_JIT=ON` |')
W('| 阶段 2 之前 | `3cb13550dff5` | `CAMEL_ENABLE_PYTHON=ON`, `CAMEL_ENABLE_PYTHON_BINDINGS=ON`, `CAMEL_PACKAGE_MINIMAL=OFF`, `CAMEL_TENSOR_BLAS=ON`, `ENABLE_FASTVM_JIT=ON` |')
W('| 当前 integration | `3cb13550dff5` | 同上 |')
W('')
W('`ENABLE_FASTVM_COMPUTED_GOTO` 在审计那个点存在、在后两个点不存在。**所以「审计 → 后两点」的比较混了代码与构建配置两件事**，')
W('不能全归给代码。**「阶段 2 之前 → 当前」的两个点是同一套选项，那个区间是干净的** —— 本页的大退化全部在这个区间里。')
W('')
W('### 二、五个程序的文本在三个点上不同，但计算是同一个')
W('')
W('`func f(x: int): int` 在审计那个点这样写，当前点写成 `func f(x: int) => int`；`LINES` 那张表只是换了排版。')
W('`fib`、`small_calls`、`tensor_train`、`MCTS` 四处逐行看过 diff：**只有返回类型写法与排版**，算的东西一样。')
W('')
W('**例外是 `closures`**：审计版用 `var count` 让闭包自己记状态，那在今天是一个编译错误')
W('（`SemanticDiag::ClosureAssignsCapturedName`），当前版改成把状态当值传进传出。**这一格的时间差不是后端的差。**')
W('')
W('### 三、两种测量混在一张表里')
W('')
W('`nvm`/`fvm`/`jit`/`prl` 的 `fib`/`small_calls`/`tensor_train`/`closures` 是 **pass 时间**（`--timeit`，进程内、全新图、不含起动与解析）。')
W('`MCTS` 那一行是**整进程墙钟**（含起动与解析，约 +65 ms 的常量），因为它在当前点的第 2 次 `--timeit` 迭代编译不过。')
W('**两列的绝对值不能互相比较**；点与点之间比的是同一种测量，那才是有效的。')
W('')
W('### 四、`std::nvm` 与 `std::jit` 有几格在一个进程内越跑越慢')
W('')
W('`small_calls` 在当前点上：`nvm` 的 5 次是 `954, 949, 950, 1430, 1458`，`jit` 的 5 次是 `1752, 2147, 2799, 2857, 2902`。')
W('审计点与阶段 2 之前的同一格都是平的（`jit`: `115, 114, 112, 111, 111`）。**这是当前点新出现的行为**，')
W('使 `steady` 混了「变化」与「漂移」；所以下面的 >20% 表把 `first` 与 `steady` 都列出来，并给 `drift`。')
W('')
W('## 三个点')
W('')
W('| 点 | 提交 | 构建 | 本轮实测忙核数 / 28 | 60 秒 load-gate（15 个采样） |')
W('|---|---|---|---|---|')
const GATES = {
    'audit-15165a9333': 'avg 8.97，max 9.84，15/15 在线上 → CONTENDED',
    'pre-stage2-1fa180c4f': 'avg 9.46，max 10.95，15/15 在线上 → CONTENDED',
    'integration-6fcee16d0c': 'avg 10.19，max 13.89，15/15 在线上 → CONTENDED',
}
for (const pt of points) {
    W(`| ${pt.short} | \`${pt.sha}\` | ${pt.meta.build || '见构建日志'} | ${pt.meta.busy_cores ?? '—'} | ${GATES[pt.label] || '—'} |`)
}
W('')

W('## 60 格：`第一次 / 稳定后`，单位毫秒')
W('')
W('每格 N 次（`fib/small_calls/tensor/MCTS` 各 5 次，`closures` 9 次；**取稳定后那几次的中位数**）。')
W('`first` 是第 1 次执行，`steady` 是其后各次的中位数。**JIT 的第一次付款最多（编译），不要把它的稳定值当第一次。**')
W('')
W('`nvm`/`fvm`/`jit` 列是 pass 时间（`--timeit`，进程内、全新图、不含起动）。')
W('`MCTS` 行是**整进程**墙钟（起动+解析+pass+收尾），因为它的第 2 次 `--timeit` 迭代编译失败，见下。')
W('')

const header = []
header.push('| 程序 | 后端 |')
for (const pt of points) header.push(` ${pt.short}<br>\`${pt.sha}\` first / steady |`)
W(header.join('') )
W('|---|---|' + points.map(() => '---|').join(''))

for (const prog of PROGRAMS) {
    for (const be of BACKENDS) {
        const row = [`| ${prog.name} | \`${be === 'prl' ? 'std::prl' : 'std::' + be}\` |`]
        for (const pt of points) row.push(` ${cellText(pt, prog.id, be)} |`)
        W(row.join(''))
    }
}
W('')

// ---- the >20% list -------------------------------------------------------------------------
W('## 超过 20% 的变化')
W('')
W('每一格两个读数都算：`first`（第 1 次执行）与 `steady`（其后的中位数）。**任一超过 20% 就列出**，')
W('并写明是哪一个触发的 —— 因为在 `std::nvm` 与 `std::jit` 上有几格**在一个进程内越跑越慢**，`steady`')
W('于是混了「变化」与「漂移」两件事，`first` 反而更干净。`drift` 列给 `steady` 的 max/min：')
W('超过 1.3 的格，`steady` 不代表任何一个稳定状态。')
W('')

function vals(pt, program, be) {
    const c = pt.cells.get(`${program}/${be}`)
    if (!c || c.exit !== 0) return { first: null, steady: null, drift: null }
    const s = (c.samples_ms || []).filter((x) => typeof x === 'number')
    const rest = s.slice(1)
    const drift = rest.length ? Math.max(...rest) / Math.min(...rest) : null
    return { first: num(c.first_ms), steady: num(c.steady_ms), drift }
}

const regressions = []
const improvements = []
for (const prog of PROGRAMS) {
    for (const be of ['nvm', 'fvm', 'jit', 'prl-1', 'prl-4', 'prl-16']) {
        for (let i = 1; i < points.length; i++) {
            const a = vals(points[i - 1], prog.id, be)
            const b = vals(points[i], prog.id, be)
            const rFirst = ratio(b.first, a.first)
            const rSteady = ratio(b.steady, a.steady)
            if (rFirst === null && rSteady === null) continue
            const worst = Math.max(rFirst ?? 1, rSteady ?? 1)
            const best = Math.min(rFirst ?? 1, rSteady ?? 1)
            const entry = {
                program: prog.id,
                programName: prog.name,
                backend: be,
                from: points[i - 1].short,
                to: points[i].short,
                fromSha: points[i - 1].sha,
                toSha: points[i].sha,
                aFirst: a.first,
                aSteady: a.steady,
                bFirst: b.first,
                bSteady: b.steady,
                rFirst,
                rSteady,
                drift: b.drift,
            }
            if (worst >= 1.2) regressions.push(entry)
            else if (best <= 1 / 1.2) improvements.push(entry)
        }
    }
}

function pct(r) {
    return r === null ? '—' : `${r >= 1 ? '+' : ''}${((r - 1) * 100).toFixed(1)}%`
}

if (regressions.length === 0) W('没有一格变慢 20% 以上。')
else {
    W('| 程序 | 后端 | 区间 | first 前 → 后 | first | steady 前 → 后 | steady | drift |')
    W('|---|---|---|---|---|---|---|---|')
    for (const e of regressions.sort((x, y) => Math.max(y.rFirst ?? 1, y.rSteady ?? 1) - Math.max(x.rFirst ?? 1, x.rSteady ?? 1))) {
        W(
            `| ${e.programName} | \`${e.backend}\` | ${e.from} → ${e.to} | ${fmt(e.aFirst)} → ${fmt(e.bFirst)} | **${pct(e.rFirst)}** | ${fmt(e.aSteady)} → ${fmt(e.bSteady)} | ${pct(e.rSteady)} | ${e.drift === null ? '—' : e.drift.toFixed(2)} |`
        )
    }
    W('')
    W(`区间 SHA 对：` + points.map((p) => `${p.short}=\`${p.sha}\``).join('，'))
}
W('')
if (improvements.length) {
    W('反向（两个读数里较好的那个变快 20% 以上）供对照：')
    W('')
    W('| 程序 | 后端 | 区间 | first 前 → 后 | first | steady 前 → 后 | steady |')
    W('|---|---|---|---|---|---|---|')
    for (const e of improvements.sort((x, y) => Math.min(x.rFirst ?? 9, x.rSteady ?? 9) - Math.min(y.rFirst ?? 9, y.rSteady ?? 9))) {
        W(
            `| ${e.programName} | \`${e.backend}\` | ${e.from} → ${e.to} | ${fmt(e.aFirst)} → ${fmt(e.bFirst)} | ${pct(e.rFirst)} | ${fmt(e.aSteady)} → ${fmt(e.bSteady)} | ${pct(e.rSteady)} |`
        )
    }
    W('')
}

// ---- stdout agreement ----------------------------------------------------------------------
W('## stdout 一致性')
W('')
let rowsOk = 0
let rowsDiff = 0
const diffs = []
for (const prog of PROGRAMS) {
    for (const be of BACKENDS) {
        for (const id of be === 'prl' ? ['prl-1', 'prl-4', 'prl-16'] : [be]) {
            const outs = points.map((pt) => {
                const s = pt.stdout
                if (!s) return null
                const r = s.results.find((x) => x.program === prog.id && x.backend === id)
                return r ? { exit: r.exit, stdout: r.stdout } : null
            })
            if (outs.some((o) => o === null)) {
                diffs.push(`${prog.id}/${id}: 有的点没有 stdout 读数`)
                rowsDiff++
                continue
            }
            const base = outs[0].stdout
            const sameExit = outs.every((o) => o.exit === outs[0].exit)
            const same = outs.every((o) => o.stdout === base) && sameExit
            if (same) rowsOk++
            else {
                rowsDiff++
                diffs.push(`${prog.id}/${id}: 输出不同`)
            }
        }
    }
}
W('逐格比：同一个格（程序 × 后端 × 线程）在三个点上的 stdout。')
W('')
W(`- 三个点 stdout 逐字节相同的：**${rowsOk}** 格`)
W(`- 不同的：**${rowsDiff}** 格`)
if (diffs.length) {
    W('')
    for (const d of diffs) W(`  - ${d}`)
}
W('')

const md = lines.join('\n')
fs.writeFileSync(path.join(HERE, 'table.cn.md'), md + '\n')

const expanded = []
for (const prog of PROGRAMS) {
    for (const be of ['nvm', 'fvm', 'jit', 'prl-1', 'prl-4', 'prl-16']) {
        const row = { program: prog.id, backend: be, points: {} }
        for (const pt of points) {
            const c = pt.cells.get(`${prog.id}/${be}`)
            row.points[pt.label] = c
                ? {
                      first_ms: num(c.first_ms),
                      steady_ms: num(c.steady_ms),
                      samples_ms: c.samples_ms,
                      iters: c.iters,
                      exit: c.exit,
                      measurement: c.measurement || 'pass (timeit)',
                  }
                : null
        }
        expanded.push(row)
    }
}
fs.writeFileSync(
    path.join(HERE, 'table.expanded.json'),
    JSON.stringify({ points: POINTS, regressions, improvements, cells: expanded }, null, 2)
)
process.stderr.write(`wrote table.cn.md and table.expanded.json; regressions=${regressions.length}\n`)