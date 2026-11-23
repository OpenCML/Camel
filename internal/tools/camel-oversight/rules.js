// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The watch rules: what is checked, when it is news, and how loudly it is said.
//
// A rule reads the measured state and answers null (nothing to say) or an alert. The engine in
// index.js decides delivery from the alert's level:
//   quiet  -- context for the coordinator's next step (`agent.inject`); never wakes it;
//   wake   -- the same while it is running; a new turn when it is idle (`agent.followup`);
//   urgent -- the nearest step (`agent.steer`), whatever it is doing.
// `hold` is how long a condition must last before it is news (a branch that is behind for a minute
// is not a reminder); `cooldown` is how long the same alert stays quiet after it was delivered,
// unless what it says changed.
//
// A number under its limit is not news, so a healthy hour delivers nothing at all: every reminder
// costs the coordinator context, and context is what it runs out of.

const readable = (value) => value && !value.unreadable && !value.unconfigured

export const RULES = [
    {
        id: 'branch-gate',
        level: 'urgent',
        hold: 0,
        cooldown: 15,
        check: ({ m, limits }) => {
            const b = readable(m.branches) && m.branches
            if (!b || (b.unmerged <= limits.unmerged && b.maxBehind <= limits.maxBehind)) return null
            return {
                key: `${b.unmerged}/${b.maxBehind}`,
                text: `分支闸门破线：未合车道分支 ${b.unmerged}（上限 ${limits.unmerged}），最落后 ${b.maxBehind} 笔（上限 ${limits.maxBehind}）。本轮停派新分支，转审计合并。`,
            }
        },
    },
    {
        id: 'lanes-below',
        level: 'wake',
        hold: 10,
        cooldown: 30,
        check: ({ m, lanes, limits }) => {
            if (!readable(lanes) || lanes.running >= limits.minLanes) return null
            const b = readable(m.branches) && m.branches
            if (b && (b.unmerged > limits.unmerged || b.maxBehind > limits.maxBehind)) return null
            const h = readable(m.host) && m.host
            const full = h && h.cores > 0 && h.load[0] / h.cores > limits.hostFull
            if (full) return null
            const load = h ? `构建主机负载 ${h.load[0]}/${h.cores}，` : ''
            return {
                key: String(lanes.running),
                text: `在跑车道 ${lanes.running} 条（目标 ${limits.minLanes}–${limits.maxLanes}）。${load}当轮从 backlog 补派到 ${limits.minLanes} 条以上。`,
            }
        },
    },
    {
        id: 'unacked',
        level: 'wake',
        hold: 0,
        cooldown: 30,
        check: ({ unacked }) => {
            if (unacked.length === 0) return null
            return {
                key: unacked.join(','),
                text: `决策者的派工超过 20 分钟没有回执：${unacked.join('、')}。用 oversight_ack 回执（taken 并记进 backlog / declined 写理由 / done 写提交）。`,
            }
        },
    },
    {
        id: 'develop-stalled',
        level: 'quiet',
        hold: 0,
        cooldown: 30,
        check: ({ m, lanes, limits }) => {
            const d = readable(m.develop) && m.develop
            if (!d || d.ageMinutes <= limits.developStallMinutes) return null
            if (!readable(lanes) || lanes.running === 0) return null
            return {
                key: d.head,
                text: `develop ${d.ageMinutes} 分钟没有前进（${d.head}），而有 ${lanes.running} 条车道在跑。问合并车道在等什么；每条车道一落地就合、就推。`,
            }
        },
    },
    {
        id: 'fsh-dev-ahead',
        level: 'quiet',
        hold: 30,
        cooldown: 60,
        check: ({ m }) => {
            const f = readable(m.fshDev) && m.fshDev
            if (!f || f.notOnDevelop === 0) return null
            return {
                key: f.head,
                text: `fsh-dev 有 ${f.notOnDevelop} 笔不在 develop 上（${f.head}）：决策者的原则、计划或答复还没进施工树。整枝合 fsh-dev。`,
            }
        },
    },
    {
        id: 'taken-not-recorded',
        level: 'quiet',
        hold: 15,
        cooldown: 60,
        check: ({ m, taken }) => {
            if (!readable(m.backlog)) return null
            const missing = taken.filter((id) => !m.backlog.has(id))
            if (missing.length === 0) return null
            return {
                key: missing.join(','),
                text: `回执了 taken 却不在 internal/backlog.md 里：${missing.join('、')}。接了的活不进 backlog 就等于不存在，补上这几行。`,
            }
        },
    },
    {
        id: 'backlog-stale',
        level: 'quiet',
        hold: 0,
        cooldown: 60,
        check: ({ m, now, limits }) => {
            if (!readable(m.backlog)) return null
            const stale = [...m.backlog.entries()]
                .filter(([, row]) => row.doing && row.updated !== null && now - row.updated > limits.staleMinutes * 60_000)
                .map(([id]) => id)
            if (stale.length === 0) return null
            return {
                key: stale.join(','),
                text: `backlog 里「在做」超过 ${limits.staleMinutes / 60} 小时没更新：${stale.join('、')}。卡住了就标卡住并写原因，停了就收，还在做就更新时间。`,
            }
        },
    },
    {
        id: 'checkout',
        level: 'quiet',
        hold: 15,
        cooldown: 60,
        check: ({ m }) => {
            const c = readable(m.checkout) && m.checkout
            if (!c || (c.dirty === 0 && c.behind === 0)) return null
            return {
                key: `${c.dirty}/${c.behind}`,
                text: `主检出（${c.head}）${c.dirty ? `有 ${c.dirty} 个未提交改动` : ''}${c.dirty && c.behind ? '，且' : ''}${c.behind ? `落后 origin/develop ${c.behind} 笔` : ''}。在它上面量的读数都是旧的：脏就存 patch 再还原，落后就 ff 合上。`,
            }
        },
    },
    {
        id: 'merged-leftover',
        level: 'quiet',
        hold: 0,
        cooldown: 120,
        check: ({ m, limits }) => {
            const g = readable(m.merged) && m.merged
            if (!g || g.mergedLocal.length <= limits.mergedLocal) return null
            return {
                key: String(g.mergedLocal.length),
                text: `已合进 develop 却没删的本地分支 ${g.mergedLocal.length} 条（上限 ${limits.mergedLocal}），清掉。远端的已合分支只在读数里列出，删它们要负责人另行授权，不在提醒里催。`,
            }
        },
    },
    {
        id: 'slot-wait',
        level: 'quiet',
        hold: 0,
        cooldown: 60,
        check: ({ m, limits }) => {
            const p = readable(m.panel) && m.panel
            if (!p || !p.running) return null
            const waiting = p.running.filter((run) => run.queueSeconds !== null && run.queueSeconds > limits.slotWaitMinutes * 60)
            if (waiting.length === 0) return null
            return {
                key: waiting.map((run) => run.worktree).join(','),
                text: `主机上有运行等槽超过 ${limits.slotWaitMinutes} 分钟：${waiting.map((run) => `${run.worktree}（${Math.round(run.queueSeconds / 60)} 分钟）`).join('、')}。看是谁占着槽、是不是该排到后面。`,
            }
        },
    },
    {
        id: 'index-lock',
        level: 'wake',
        hold: 0,
        cooldown: 15,
        check: ({ m }) => {
            const lock = m.indexLock
            if (!lock || lock.unreadable || lock.ageSeconds <= 120) return null
            return {
                key: String(Math.floor(lock.ageSeconds / 600)),
                text: `主检出的 .git/index.lock 已存在 ${Math.round(lock.ageSeconds / 60)} 分钟：合并都会失败。用 scripts/check-index-lock.mjs 看是谁持有——先找那个只读的后台程序（git 面板补丁是否被更新覆盖：internal/tools/patches/），别怪合并；确认没有 git 进程在用再删锁文件。`,
            }
        },
    },
    {
        id: 'fetch-failed',
        level: 'quiet',
        hold: 10,
        cooldown: 60,
        check: ({ m }) => {
            if (!m.fetched || !m.fetched.unreadable) return null
            return { key: m.fetched.unreadable, text: `git fetch 失败（${m.fetched.unreadable}）：本轮所有读数都建在没刷新的引用上，读不到不等于没变化。` }
        },
    },
    {
        // An idle coordinator with work open is the most expensive state there is: every lane it would
        // have opened stands still. It is woken after a few minutes, and again every ten while it stays
        // idle, with the items it could start on named, so the wake-up is a work list and not a nudge.
        id: 'coordinator-idle',
        level: 'wake',
        hold: 3,
        cooldown: 10,
        check: ({ m, target, backlogOpen, now, lanes, limits }) => {
            if (!target || target.status !== 'idle' || backlogOpen === 0) return null
            // Idle while its lanes are full is the coordinator waiting on their reports, which is its
            // job; only idle with lanes to spare (or unknown) is time lost.
            if (readable(lanes) && lanes.running >= limits.minLanes) return null
            const minutes = target.since ? Math.round((now - target.since) / 60_000) : null
            const rows = m.backlog instanceof Map ? [...m.backlog.entries()] : []
            const open = rows.filter(([, row]) => !row.done && !row.doing && !row.blocked).map(([id]) => id).slice(0, 8)
            const doing = rows.filter(([, row]) => row.doing).map(([id]) => id).slice(0, 8)
            return {
                key: 'idle',
                text: `协调者会话空闲${minutes !== null ? ` ${minutes} 分钟` : ''}，backlog 还有 ${backlogOpen} 条没做完。现在开工：先看在做的（${doing.join('、') || '无'}）有没有停着的车道，再从待做的（${open.join('、') || '无'}）补派，车道开到 6–8 条；催合并、跑闸门。不等人。`,
            }
        },
    },
    {
        id: 'decision-maker-offline',
        level: 'quiet',
        hold: 0,
        cooldown: 120,
        check: ({ dmLastSeen, now, limits }) => {
            if (!dmLastSeen || now - dmLastSeen < limits.dmOfflineMinutes * 60_000) return null
            const minutes = Math.round((now - dmLastSeen) / 60_000)
            return {
                key: 'offline',
                text: `决策者 ${minutes} 分钟没有连上通道（可能掉线或在重启）。照 backlog 与铁律继续，不等他：回执与 oversight_tell 都记在日志里，他回来会补读；原则问题照写问题页、按默认建议开工。`,
            }
        },
    },
    {
        id: 'day-end',
        level: 'wake',
        hold: 0,
        cooldown: 20 * 60,
        check: ({ now, limits }) => {
            const d = new Date(now)
            const minutes = d.getHours() * 60 + d.getMinutes()
            if (minutes < limits.dayEndMinutes) return null
            return {
                key: d.toDateString(),
                text: '收工：在合并后的 develop 上跑一轮 GitHub Actions，读数写进车道的合并说明或 internal/reports/；红的逐条进 backlog。',
            }
        },
    },
]

export const DEFAULT_LIMITS = {
    minLanes: 6,
    maxLanes: 8,
    unmerged: 20,
    maxBehind: 100,
    mergedLocal: 20,
    developStallMinutes: 25,
    staleMinutes: 120,
    hostFull: 0.9,
    slotWaitMinutes: 30,
    dmOfflineMinutes: 45,
    dayEndMinutes: 23 * 60 + 30,
}

export const LEVELS = { quiet: 0, wake: 1, urgent: 2 }
