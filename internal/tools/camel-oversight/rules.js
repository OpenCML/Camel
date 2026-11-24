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
        cooldown: 60,
        check: ({ m, limits }) => {
            const b = readable(m.branches) && m.branches
            if (!b || (b.unmerged <= limits.unmerged && b.maxBehind <= limits.maxBehind)) return null
            return {
                // The news is that the gate is crossed, not each count while it stays crossed: a key
                // holding the counts turned every new branch into a fresh urgent interruption of a
                // coordinator who had already stopped dispatching.
                key: 'crossed',
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
            // The host's load does not hold lanes back: every run reaches the host through a slot, so a
            // lane added while the host is busy queues for its slot and reads and edits meanwhile. (The
            // build host is shared; most of its load can be other people's work.)
            const h = readable(m.host) && m.host
            const load = h ? `构建主机负载 ${h.load[0]}/${h.cores}（上主机都走槽排队，不挡开车道），` : ''
            return {
                key: String(lanes.running),
                text: `在跑车道 ${lanes.running} 条（目标 ${limits.minLanes}–${limits.maxLanes}）。${load}当轮从 backlog 补派到 ${limits.minLanes} 条以上。`,
            }
        },
    },
    {
        // A dispatch waits in the queue until the coordinator's turn ends. A turn kept open by long
        // waits on background jobs holds every dispatch behind it, however urgent; a single call that
        // blocks is caught elsewhere, a chain of shorter waits is caught here.
        id: 'dispatch-stuck',
        level: 'urgent',
        hold: 0,
        cooldown: 20,
        check: ({ stuck, limits }) => {
            const late = (stuck || []).filter((entry) => entry.minutes >= limits.stuckMinutes)
            if (late.length === 0) return null
            return {
                key: 'stuck',
                text: `${late.length} 条派工排在你这一轮之后没送到（${late.map((e) => `${e.id} ${e.minutes} 分钟`).join('、')}）。后台任务留着跑，结束这一轮；读数好了推送会叫醒你（铁律：你只做判断，不在会话里久等）。`,
            }
        },
    },
    {
        // A turn that ends on a promise or on waiting for someone's nod: the session goes idle and the
        // promised work waits for a message that may never come (the author: it says it will do
        // something, ends the turn, and moves on to the next message instead).
        id: 'promise-at-turn-end',
        level: 'wake',
        hold: 2,
        cooldown: 20,
        check: ({ lastTurn, target, now }) => {
            if (!lastTurn || lastTurn.aborted || !target || target.status === 'running') return null
            if (now - lastTurn.at > 60 * 60_000) return null
            const tail = lastTurn.text.trim().slice(-600)
            const PROMISE = /(我会|我将|我这就|我马上|接下来我|下一步我|随后|稍后|落地后|跑完后|合完后|推完后|之后再|再推|再合|等[^。\n]{0,24}(后|再|完)|待[^。\n]{0,16}再|你说一声|不说我就|等你|你点头)/
            const m = PROMISE.exec(tail)
            if (!m) return null
            const sentence = tail.slice(Math.max(0, tail.lastIndexOf('。', m.index) + 1), Math.min(tail.length, m.index + 60)).replace(/\s+/g, ' ').trim()
            return {
                key: String(lastTurn.at),
                text: `你上一轮结束在一句承诺或等待上：「${sentence}」。不要以"我会…/等…再…"结束一轮：能做的这一轮就做；要等读数就起后台任务，写明在等什么、完成时由谁叫醒你；做不了的记进 backlog 一行。不等负责人点头（铁律：不问、不等）。`,
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
            // With an integration branch, lanes land there and develop follows on promotion: the branch
            // that stalls when the merge seats stall is integration; develop is late by design.
            const i = readable(m.integration) && !m.integration.absent && m.integration
            const d = i || (readable(m.develop) && m.develop)
            const name = i ? 'integration' : 'develop'
            if (!d || d.ageMinutes <= limits.developStallMinutes) return null
            if (!readable(lanes) || lanes.running === 0) return null
            return {
                key: d.head,
                text: `${name} ${d.ageMinutes} 分钟没有前进（${d.head}），而有 ${lanes.running} 条车道在跑。问合并位在等什么；交回的车道排进合并队列，排着的多就多开合并位。`,
            }
        },
    },
    {
        // Promotion: develop is advanced from integration after a full fast-layer run. Late by design,
        // but not without end.
        id: 'promotion-due',
        level: 'quiet',
        hold: 0,
        cooldown: 60,
        check: ({ m, limits }) => {
            const i = readable(m.integration) && !m.integration.absent && m.integration
            const d = readable(m.develop) && m.develop
            if (!i || !d || i.developBehind === 0 || d.ageMinutes <= limits.promotionMinutes) return null
            return {
                key: i.head,
                text: `develop 落后 integration ${i.developBehind} 笔、上次前进在 ${d.ageMinutes} 分钟前：在 integration 尖端跑一遍全快层，与上次晋级的读数逐条比，干净就把 develop 快进过去。`,
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
            if (!f || !f.notLanded) return null
            return {
                key: f.head,
                text: `fsh-dev 有 ${f.notLanded} 笔还没合进 integration 或 develop（${f.head}）：决策者的原则、计划或答复还没进施工树。整枝合 fsh-dev。`,
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
            const shown = missing.slice(0, 5).join('、') + (missing.length > 5 ? ` 等 ${missing.length} 条` : '')
            return {
                key: String(Math.ceil(missing.length / 5)),
                text: `回执了 taken 超过一小时仍不在 backlog 里：${shown}。接了的活要落一行。`,
            }
        },
    },
    {
        id: 'backlog-stale',
        level: 'quiet',
        hold: 0,
        // The list is long when the backlog is behind, and repeating it hourly says nothing new: the
        // reminder names the count and the oldest few, and comes back when the count grows or after four
        // hours.
        cooldown: 240,
        check: ({ m, now, limits }) => {
            if (!readable(m.backlog)) return null
            // A row recorded under its own id and again under the item it belongs to is named once,
            // by the id written in the table.
            const seen = new Set()
            const stale = [...m.backlog.entries()]
                .filter(([, row]) => row.doing && row.updated !== null && now - row.updated > limits.staleMinutes * 60_000)
                .filter(([, row]) => !seen.has(row) && seen.add(row))
                .sort((a, b) => a[1].updated - b[1].updated)
            if (stale.length === 0) return null
            const shown = stale.slice(0, limits.staleShown).map(([id, row]) => `${id}（${Math.round((now - row.updated) / 3_600_000)} 小时）`)
            const more = stale.length > shown.length ? `等` : ``
            return {
                // A bucket of five: a few rows coming and going does not make it news again.
                key: String(Math.ceil(stale.length / 5)),
                text: `backlog 里「在做」超过 ${limits.staleMinutes / 60} 小时没更新的有 ${stale.length} 条，最久的是 ${shown.join('、')}${more}。逐条处理：卡住的标卡住并写原因，做完或作废的收掉，还在做的更新时间——backlog 只放手上真有的活。`,
            }
        },
    },
    {
        id: 'checkout',
        level: 'quiet',
        hold: 15,
        cooldown: 60,
        // The main checkout is the author's own workspace: nothing is merged, built or tested in it.
        // The only thing asked of the coordinator is to keep it in step with origin/develop while it
        // stands on develop with nothing uncommitted; on another branch, or with changes, it is his.
        check: ({ m }) => {
            const c = readable(m.checkout) && m.checkout
            if (!c || c.head !== 'develop' || c.dirty !== 0 || c.behind === 0) return null
            return {
                key: String(c.behind),
                text: `主检出在 develop 上、没有改动，落后 origin/develop ${c.behind} 笔：fast-forward 合上（只做 git merge --ff-only，不做别的）。`,
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
                text: `已合进 develop 却没删的本地分支 ${g.mergedLocal.length} 条（上限 ${limits.mergedLocal}），清掉（按清理那条铁律：先确认工作树干净、没有活会话在里面）。`,
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
        // A coordinator idle while its lanes run is waiting on their reports, which is its job. Idle with
        // no lane running and work open -- after a restart, or when the last lane has just finished --
        // nothing moves at all, and nothing else will start it. Only that is woken: after a few
        // minutes, and again every ten while it lasts, with the items it can start on named. Too few
        // lanes while some run is the lanes-below rule's business, not this one's.
        id: 'coordinator-idle',
        level: 'wake',
        hold: 3,
        cooldown: 10,
        check: ({ m, target, backlogOpen, now, lanes }) => {
            if (!target || target.status !== 'idle' || backlogOpen === 0) return null
            if (readable(lanes) && lanes.running > 0) return null
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
    promotionMinutes: 180,
    staleShown: 5,
    slotWaitMinutes: 30,
    dmOfflineMinutes: 45,
    stuckMinutes: 15,
    dayEndMinutes: 23 * 60 + 30,
}

export const LEVELS = { quiet: 0, wake: 1, urgent: 2 }
