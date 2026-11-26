// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The GUI half of camel-oversight: what the owner sees without asking anyone.
//
//   a band above the composer of the coordinator's session (conversation.input.dock): lanes running,
//     develop's age, unacknowledged dispatches, the build host, open alerts; click it for the alerts,
//     the recent reminders and what the coordinator and the other side said to each other;
//   toasts (shell.overlay): each reminder the watch delivers, each acknowledgement and each message the
//     coordinator sends, as they happen; an urgent reminder stays until it is dismissed;
//
// The data comes from the host half's own routes, same origin: /oversee/state every 15 seconds and
// on every pushed event, /oversee/events as an EventSource for the pushes themselves.

window.__ModuleLoader__.load({
    id: '@local/camel-oversight',
    factory(require) {
        const React = require('react')
        const h = React.createElement

        // ---- one store, shared by every component ------------------------------------------------
        const store = { state: null, toasts: [], error: null, version: 0, reloading: false, reload: null }
        const listeners = new Set()
        const changed = () => {
            store.version += 1
            for (const listener of listeners) listener()
        }
        const subscribe = (listener) => {
            listeners.add(listener)
            return () => listeners.delete(listener)
        }
        const useStore = () => React.useSyncExternalStore(subscribe, () => store.version)

        const refresh = async () => {
            try {
                const response = await fetch('/oversee/state', { cache: 'no-store' })
                store.state = await response.json()
                store.error = null
            } catch (error) {
                store.error = String(error && error.message ? error.message : error)
            }
            changed()
        }

        let toastSeq = 0
        const toast = (event) => {
            const sticky = (event.kind === 'reminder' && event.level === 'urgent') || (event.kind === 'reload' && event.ok === false)
            const entry = { seq: ++toastSeq, event, sticky }
            store.toasts = [...store.toasts.slice(-4), entry]
            changed()
            if (!sticky) setTimeout(() => dismiss(entry.seq), 12_000)
        }
        const dismiss = (seq) => {
            store.toasts = store.toasts.filter((entry) => entry.seq !== seq)
            changed()
        }

        // The reload button's one call. It is the only place that writes to the live directory, and it
        // is the host route that decides -- this half only reports what came back, including the file
        // and line of a refusal and the commit sha of a success.
        const reloadPlugin = async () => {
            if (store.reloading) return
            store.reloading = true
            changed()
            let value
            try {
                const response = await fetch('/oversee/reload', { method: 'POST', cache: 'no-store' })
                value = await response.json()
                value.ok = value.ok !== false && response.ok
            } catch (error) {
                value = { ok: false, stage: 'request', message: String(error && error.message ? error.message : error) }
            }
            store.reloading = false
            store.reload = { ...value, at: Date.now() }
            toast({ kind: 'reload', ...value, at: Date.now() })
            changed()
            // The plugin's own restart closes the event stream and answers nothing for a moment; one
            // poll a second later reads the new state instead of the gap.
            setTimeout(refresh, 1500)
        }

        let source = null
        const connect = () => {
            if (source) return
            source = new EventSource('/oversee/events')
            source.onmessage = (message) => {
                let event
                try {
                    event = JSON.parse(message.data)
                } catch {
                    return
                }
                if (event.kind !== 'beat' && !(event.kind === 'reminder' && event.rules && event.rules[0] === 'rules-refresh')) toast(event)
                refresh()
            }
            source.onerror = () => {
                // EventSource reconnects by itself; the band shows the last state meanwhile.
            }
        }

        // ---- presentation ----------------------------------------------------------------------------
        const color = {
            urgent: 'var(--dsw-alias-text-danger, #d4380d)',
            wake: 'var(--dsw-alias-text-warning, #d48806)',
            quiet: 'var(--dsw-alias-text-secondary, #8c8c8c)',
            ok: 'var(--dsw-alias-text-success, #389e0d)',
        }
        const ago = (at) => {
            if (!at) return '—'
            const minutes = Math.round((Date.now() - at) / 60_000)
            return minutes < 1 ? '刚刚' : minutes < 60 ? `${minutes} 分钟前` : `${Math.round(minutes / 60)} 小时前`
        }
        const describe = (event) => {
            switch (event.kind) {
                case 'reminder':
                    return { title: `提醒协调者（${event.level}）`, body: event.text.replace(/^\[oversight · [^\]]*\]\n?/, ''), level: event.level }
                case 'ack':
                    return { title: `回执 ${event.id}：${event.status}`, body: event.note || '', level: 'ok' }
                case 'tell':
                    return { title: event.urgent ? '协调者（急）' : '协调者', body: event.text, level: event.urgent ? 'urgent' : 'quiet' }
                case 'session-ended':
                    return { title: '协调者会话结束', body: '新会话要重新认领（oversight_claim）。', level: 'urgent' }
                case 'reload': {
                    if (event.ok === false) {
                        const where = event.file ? `${event.file}${event.line ? `:${event.line}` : ''}` : ''
                        const why = event.message || event.reason || '未知原因'
                        return {
                            title: '重载插件失败',
                            body: `${event.stage ? `${event.stage}：` : ''}${where ? `${where} ` : ''}${why}${event.liveUnchanged ? '（live 目录未变）' : ''}（点击关闭）`,
                            level: 'urgent',
                        }
                    }
                    return {
                        title: '重载插件',
                        body: event.changed
                            ? `已从 ${event.ref || 'origin/develop'} 同步并重载，版本 ${event.sha ? event.sha.slice(0, 9) : '?'}`
                            : `内容未变（${event.sha ? event.sha.slice(0, 9) : ''}），没有重载`,
                        level: 'ok',
                    }
                }
                default:
                    return { title: event.kind, body: '', level: 'quiet' }
            }
        }

        const chip = (label, value, tone) =>
            h('span', { style: { marginRight: 14, whiteSpace: 'nowrap' } },
                h('span', { style: { opacity: 0.7 } }, label + ' '),
                h('b', { style: { color: tone ? color[tone] : undefined } }, value))

        function Band(props) {
            useStore()
            const [open, setOpen] = React.useState(false)
            const s = store.state
            const sessionId = (props.session && props.session.id) || props.sessionId || null
            if (!s || !s.target || (sessionId && s.target.id !== sessionId)) return null
            const lanes = s.lanes || {}
            const r = s.reading || {}
            const host = r.host && r.host.load ? `${r.host.load[0]}/${r.host.cores}` : '—'
            const develop = r.develop && r.develop.ageMinutes !== undefined ? `${r.develop.ageMinutes} 分钟前` : '—'
            const alerts = s.alerts || []
            const worst = alerts.some((a) => a.level === 'urgent') ? 'urgent' : alerts.some((a) => a.level === 'wake') ? 'wake' : alerts.length ? 'quiet' : 'ok'
            const lanesTone = lanes.running === undefined ? undefined : lanes.running < 6 ? 'wake' : 'ok'
            const unacked = (s.unacknowledged || []).length
            // How long since the dispatching client (`client=dm`) last reached the channel. It polls
            // every few seconds and checks in every twenty minutes, so an hour of silence means it is
            // cut off, not merely idle, and the band says so without anything being opened.
            const dmSeen = s.decisionMaker && s.decisionMaker.lastSeen
            const dmMinutes = dmSeen ? Math.round((Date.now() - dmSeen) / 60_000) : null
            const dmText = dmMinutes === null ? '未连' : dmMinutes < 5 ? '在线' : `${dmMinutes} 分钟没连上`
            const dmTone = dmMinutes === null ? undefined : dmMinutes >= 60 ? 'urgent' : dmMinutes >= 30 ? 'wake' : 'ok'
            // The branch gate: lanes waiting to be merged, and whether that holds new lanes back (the
            // gate is crossed exactly when its rule is among the open alerts).
            const branches = r.branches && r.branches.unmerged !== undefined ? r.branches : null
            const gateShut = alerts.some((a) => a.id === 'branch-gate')
            // Which version of the plugin is running, and whether the live directory holds a newer one
            // than this process loaded: `synced` is what the last sync published, `loaded` is what was
            // read at start, so the two differ exactly between a sync and the reload that follows it.
            const plugin = s.plugin || {}
            const syncedSha = plugin.synced ? String(plugin.synced).slice(0, 7) : null
            const pending = Boolean(plugin.synced && plugin.loaded && plugin.synced !== plugin.loaded)
            return h('div', {
                    style: {
                        // Aligned with the composer card the way DSH's own docks are (QueueDock): the
                        // same width and max-width expressions over the composer's layout variables,
                        // centred, so the band follows the input box whatever its width becomes.
                        boxSizing: 'border-box',
                        width: 'calc(100% - 2 * var(--dsh-composer-side-clearance, 0px) - 2 * var(--dsh-composer-dock-inset, 0px))',
                        maxWidth: 'calc(var(--dsh-composer-card-max-width, 952px) - 2 * var(--dsh-composer-dock-inset, 0px))',
                        margin: '0 auto 6px', padding: '6px 10px', borderRadius: 8, fontSize: 12, cursor: 'pointer',
                        background: 'var(--dsw-alias-bg-layer-2, rgba(127,127,127,0.08))',
                        borderLeft: `3px solid ${color[worst]}`,
                    },
                    onClick: () => setOpen(!open),
                },
                h('div', { style: { display: 'flex', flexWrap: 'wrap', alignItems: 'center' } },
                    h('b', { style: { marginRight: 14 } }, 'Camel 协作'),
                    chip('协调者', s.target.status === 'running' ? '在跑' : '空闲', s.target.status === 'running' ? 'ok' : 'wake'),
                    chip('决策者', dmText, dmTone),
                    chip('车道', lanes.running === undefined ? '—' : `${lanes.running}/${lanes.total}`, lanesTone),
                    chip('待合', branches ? `${branches.unmerged} 条${gateShut ? ' · 闸门关' : ''}` : '—', gateShut ? 'urgent' : 'ok'),
                    chip('develop', develop),
                    chip('未回执', String(unacked), unacked ? 'wake' : 'ok'),
                    chip('主机', host),
                    chip('提醒', alerts.length ? `${alerts.length} 条` : '无', worst === 'ok' ? 'ok' : worst),
                    chip('插件', syncedSha ? `${syncedSha}${pending ? '（待重载）' : ''}` : '未同步', pending ? 'wake' : undefined),
                    store.error ? chip('读取', '失败', 'urgent') : null,
                    // The band is the button's home because the person who decides to reload is looking
                    // at it: the click asks the host to sync and reload, and the answer lands in a toast
                    // and in this row's version chip. Clicking it must not also open the details.
                    h('button', {
                            onClick: (event) => {
                                event.stopPropagation()
                                void reloadPlugin()
                            },
                            disabled: store.reloading,
                            title: '从 origin/develop 取插件目录到 live 目录，只重载 camel-oversight；坏文件会带着文件名和行号被拒绝，live 目录不变',
                            style: {
                                marginLeft: 'auto', fontSize: 12, padding: '2px 10px', borderRadius: 6, color: 'inherit',
                                border: '1px solid var(--dsw-alias-border, rgba(127,127,127,0.35))',
                                background: 'var(--dsw-alias-bg-layer-1, transparent)',
                                cursor: store.reloading ? 'default' : 'pointer',
                            },
                        }, store.reloading ? '重载中…' : '重载插件')),
                open ? h(Details, { s }) : null)
        }

        function Details({ s }) {
            const alerts = s.alerts || []
            const reminders = (s.reminders || []).slice().reverse()
            return h('div', { style: { marginTop: 8, lineHeight: 1.6 } },
                h('div', { style: { fontWeight: 600 } }, '插件版本'),
                h('div', { style: { opacity: 0.85 } },
                    `${s.plugin && s.plugin.loaded ? s.plugin.loaded.slice(0, 9) : '未知'}（加载自 ${(s.plugin && s.plugin.loading) || '?'}；` +
                    `live 目录 ${(s.plugin && s.plugin.target) || '?'}${s.plugin && s.plugin.fromLive === false ? '，两者不同，重载会被拒绝' : ''}）`),
                store.reload
                    ? h('div', { style: { color: store.reload.ok ? color.ok : color.urgent } },
                          `上次重载：${store.reload.ok
                              ? `${store.reload.changed ? '已同步并重载' : '内容未变，未重载'} ${store.reload.sha ? store.reload.sha.slice(0, 9) : ''}`
                              : `${store.reload.stage || ''} ${store.reload.message || ''}`}`)
                    : null,
                h('div', { style: { fontWeight: 600 } }, '当前提醒'),
                alerts.length === 0
                    ? h('div', { style: { opacity: 0.7 } }, '无：读数都在线内。')
                    : alerts.map((a) => h('div', { key: a.id, style: { color: color[a.level] } },
                          `• ${a.text}（${ago(a.since)}起，${a.lastDelivered ? `${ago(a.lastDelivered)}已提醒` : '尚未提醒'}）`)),
                h('div', { style: { fontWeight: 600, marginTop: 6 } }, '最近送出的提醒'),
                reminders.length === 0
                    ? h('div', { style: { opacity: 0.7 } }, '无')
                    : reminders.map((e, i) => h('div', { key: i, style: { opacity: 0.85 } },
                          `${ago(e.at)} · ${e.mode} · ${(e.rules || []).join('、')}`)),
                h('div', { style: { fontWeight: 600, marginTop: 6 } }, '构建与测试队列'),
                h('div', { style: { opacity: 0.85 } }, ['buildQueue', 'testQueue'].map((key) => {
                    const q = s.reading && s.reading.panel && s.reading.panel[key]
                    const label = key === 'buildQueue' ? '构建' : '测试'
                    if (!q || q.unreadable) return `${label}：读不到`
                    const holder = q.holder ? `${q.holder.worktree}（${q.holder.phase || '进行中'}）` : '空闲'
                    const waiting = (q.waiters || []).map((w) => `${w.worktree}${w.waitedSeconds ? ` ${Math.round(w.waitedSeconds / 60)} 分钟` : ''}`).join('、')
                    return `${label}：${holder}${waiting ? `；排队 ${waiting}` : ''}`
                }).join('　')),
                h('div', { style: { fontWeight: 600, marginTop: 6 } }, '主机上在跑的'),
                h('div', { style: { opacity: 0.85 } },
                    ((s.reading && s.reading.panel && s.reading.panel.running) || [])
                        .map((run) => `${run.worktree || '?'}（${run.phase || '?'}${run.slot !== null && run.slot !== undefined ? `，槽 ${run.slot}` : ''}${run.queueSeconds ? `，已等 ${Math.round(run.queueSeconds / 60)} 分钟` : ''}）`)
                        .join('、') || '无'),
                h('div', { style: { fontWeight: 600, marginTop: 6 } }, '会话与车道'),
                h('div', { style: { opacity: 0.85 } },
                    ((s.lanes && s.lanes.lanes) || []).map((l) => `${l.name || l.path || l.id}（${l.activity === 'running' ? '在跑' : '闲'}）`).join('、') || '无活子代理'))
        }

        function Toasts() {
            useStore()
            if (store.toasts.length === 0) return null
            return h('div', {
                    style: { position: 'fixed', top: 56, right: 16, width: 360, zIndex: 1000, display: 'flex', flexDirection: 'column', gap: 8 },
                },
                store.toasts.map(({ seq, event, sticky }) => {
                    const d = describe(event)
                    return h('div', {
                            key: seq,
                            onClick: () => dismiss(seq),
                            style: {
                                pointerEvents: 'auto', cursor: 'pointer', padding: '8px 12px', borderRadius: 8, fontSize: 12,
                                background: 'var(--dsw-alias-bg-layer-1, #fff)', boxShadow: '0 4px 14px rgba(0,0,0,0.18)',
                                borderLeft: `3px solid ${color[d.level] || color.quiet}`,
                            },
                        },
                        h('div', { style: { fontWeight: 600, color: color[d.level] } }, `Camel · ${d.title}${sticky ? '（点击关闭）' : ''}`),
                        d.body ? h('div', { style: { whiteSpace: 'pre-wrap', marginTop: 4, maxHeight: 160, overflow: 'auto' } }, d.body) : null)
                }))
        }

        const module = { exports: {} }
        module.exports.inject = ['slots']
        module.exports.apply = (ctx) => {
            refresh()
            connect()
            const timer = setInterval(refresh, 15_000)
            ctx.slots.inject('conversation.input.dock', () =>
                ctx.slots.register({ name: 'conversation.input.dock', id: 'camel-oversight', order: -10 }, Band))
            ctx.slots.inject('shell.overlay', () =>
                ctx.slots.register({ name: 'shell.overlay', id: 'camel-oversight-toasts' }, Toasts))
            if (ctx.on) {
                ctx.on('dispose', () => {
                    clearInterval(timer)
                    if (source) source.close()
                })
            }
        }
        return module.exports
    },
})
