// Client half of the lane-state bundle: renders the reading the host half already put in front
// of the model, so the same numbers are visible in the page instead of only in the transcript.
//
// The reading is not fetched: the host half registers it as runtime context, it travels with
// the conversation, and this component reads it back out of the conversation it is already
// mounted beside. A second channel would be a second truth.
//
// While the carrier is not yet known, this renders a short diagnostic instead of nothing. An
// absent element cannot be told apart from a broken one, so this version says which hooks it
// reached and what each of them answered. Once the carrier is established the diagnostic goes
// away and only the reading is drawn.

window.__ModuleLoader__.load({
    id: '@local/camel-lanes',
    factory(require) {
        const React = require('react')
        const h = React.createElement

        const MARKER = 'Camel lane state'

        /// Every string in an unknown value. A hook that answers a nested snapshot still yields
        /// its text; the alternative is asserting one exact shape and rendering nothing when the
        /// shape moves, which reads exactly like "no reading".
        function strings(value, depth = 0) {
            if (depth > 10 || value === null || value === undefined) return []
            if (typeof value === 'string') return [value]
            if (typeof value === 'number' || typeof value === 'boolean') return []
            if (typeof value === 'function') return []
            if (Array.isArray(value)) return value.flatMap((item) => strings(item, depth + 1))
            const out = []
            for (const key of Object.keys(value)) {
                try {
                    out.push(...strings(value[key], depth + 1))
                } catch {
                    // A getter that throws is not a reading; skip it and keep the rest.
                }
            }
            return out
        }

        /// Calls one standard prop and reports what happened, so a failure has a name here
        /// instead of being swallowed into an empty render.
        function ask(props, name) {
            const hook = props && props[name]
            if (typeof hook !== 'function') return { name, state: 'absent' }
            try {
                // These are selector hooks: called with no selector they throw, which is how the first version learned it. An identity selector reads the whole snapshot and the walk finds the text wherever it sits.
                const value = hook((snapshot) => snapshot)
                return { name, state: 'ok', value, strings: strings(value) }
            } catch (error) {
                return { name, state: 'threw: ' + String(error && error.message ? error.message : error) }
            }
        }

        function LaneState(props) {
            const sources = [ask(props, 'useConversation'), ask(props, 'useChat'), ask(props, 'useSession')]

            let block = null
            for (const source of sources) {
                if (!source.strings) continue
                const found = source.strings
                    .filter((text) => text.includes(MARKER))
                    .sort((a, b) => b.length - a.length)
                if (found.length > 0) {
                    block = found[0]
                    break
                }
            }

            const style = {
                display: 'flex',
                alignItems: 'center',
                gap: '0.5rem',
                // Pushed to the far end of the row it shares with the permission, project and
                // model chips, so it reads as one more status chip instead of a new line.
                marginLeft: 'auto',
                padding: '0 0.5rem',
                font: 'inherit',
                fontSize: '0.75rem',
                lineHeight: 1.4,
                fontVariantNumeric: 'tabular-nums',
                whiteSpace: 'nowrap',
            }

            // Nothing to say before the block arrives. A placeholder here would claim the state
            // is absent when the truth is that it has not been read yet.
            if (!block) return null

            const lines = block.split(/\r?\n/).map((line) => line.trim())
            const over = lines.filter((line) => line.startsWith('- OVER'))
            let shown
            let warn = false
            if (over.length > 0) {
                shown = over.map((line) => line.replace(/^- /, ''))
                warn = true
            } else {
                const pick = (prefix) => lines.find((line) => line.startsWith(prefix)) || ''
                // Compact on purpose: this sits in a row of chips, so each reading is shortened
                // to the part that changes what the reader does next.
                shown = [
                    pick('- origin/develop:').replace(/- origin\/develop: (\S+).*/, '$1'),
                    pick('- unmerged lane branches:').replace(
                        /^- unmerged lane branches: (\d+), worst (\d+).*$/,
                        'U$1 max$2',
                    ),
                    pick('- worktrees besides the main checkout:').replace(
                        /^- worktrees besides the main checkout: (\d+).*$/,
                        '$1 trees',
                    ),
                    // Labelled "live" and not "running" on purpose: this counts the agent sessions
                    // the process still holds, which includes the ones that have finished. Calling
                    // it "running" would name a number this side cannot measure.
                    pick('- agents live in this process:').replace(
                        /^- agents live in this process: (\d+).*$/,
                        '$1 live',
                    ),
                ].filter(Boolean)
            }

            return h(
                'div',
                {
                    style: {
                        ...style,
                        color: warn
                            ? 'var(--color-warning, #b45309)'
                            : 'var(--color-text-secondary, #6b7280)',
                    },
                },
                shown.map((line, index) => h('span', { key: index, style: { whiteSpace: 'nowrap' } }, line)),
            )
        }

        return {
            inject: ['slots'],
            apply(ctx) {
                ctx.slots.inject('conversation.composer.dock', () =>
                    ctx.slots.register(
                        { name: 'conversation.composer.dock', id: 'camel-lanes', order: 5 },
                        LaneState,
                    ),
                )
            },
        }
    },
})