<template>
  <div
    :class="['gir-node-inner', shapeClass, kindClass, { 'gir-node-subgraph-inner': isSubgraph, 'gir-node-disabled': data?.unreachable }]"
    :title="data?.tooltipText || data?.tooltip"
  >
    <Handle v-if="!isSubgraph" type="target" :position="Position.Left" />
    <span
      v-if="!isSubgraph"
      class="gir-breakpoint-toggle"
      :class="{ 'gir-breakpoint-toggle--active': data?.hasBreakpoint }"
      :title="data?.hasBreakpoint ? 'Remove breakpoint' : 'Set breakpoint'"
      aria-label="Toggle breakpoint"
    />
    <span v-if="data?.isPaused" class="gir-node-paused-badge">●</span>
    <span class="gir-node-label">
      <template v-if="labelFragments.length">
        <span
          v-for="(frag, i) in labelFragments"
          :key="i"
          :class="frag.class"
        >{{ frag.text }}</span>
      </template>
      <template v-else>{{ displayText }}</template>
    </span>
    <Handle v-if="!isSubgraph" type="source" :position="Position.Right" />
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { Handle, Position } from '@vue-flow/core'

const props = defineProps({
  id: { type: String, required: true },
  data: { type: Object, default: () => ({}) },
  position: Object
})

const isSubgraph = computed(() => props.data?.type === 'subgraph')

const displayText = computed(() => props.data?.displayLabel ?? props.data?.label ?? props.id)

function tokenizeForHighlight(text) {
  if (!text || typeof text !== 'string') return []
  const fragments = []
  let i = 0
  const re = /("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|[^\s"']+)/g
  let m
  while ((m = re.exec(text)) !== null) {
    const raw = m[0]
    if ((raw.startsWith('"') && raw.endsWith('"')) || (raw.startsWith("'") && raw.endsWith("'"))) {
      fragments.push({ text: raw, class: 'gir-token-string' })
    } else if (/^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(raw)) {
      fragments.push({ text: raw, class: 'gir-token-number' })
    } else {
      fragments.push({ text: raw, class: '' })
    }
    i = m.index + raw.length
  }
  if (i < text.length) fragments.push({ text: text.slice(i), class: '' })
  return fragments
}

const labelFragments = computed(() => {
  if (props.data?.type !== 'DATA' && !props.data?.dataRepr) return []
  const t = displayText.value
  const frags = tokenizeForHighlight(t)
  return frags.length > 1 || (frags.length === 1 && frags[0].class) ? frags : []
})

const shapeClass = computed(() => {
  if (isSubgraph.value) return 'gir-shape-subgraph'
  const shape = (props.data?.shape || 'circle').toLowerCase()
  if (shape === 'doublecircle') return 'gir-shape-doublecircle'
  if (shape === 'diamond' || shape === 'mdiamond') return 'gir-shape-diamond'
  return 'gir-shape-circle'
})

const kindClass = computed(() => {
  if (isSubgraph.value) return 'gir-kind-subgraph'
  const type = (props.data?.type || '').toUpperCase()
  if (type === 'DATA' || type === 'PORT') return 'gir-kind-data'
  if (type === 'EXIT') return 'gir-kind-exit'
  if (type === 'FUNC') return 'gir-kind-func'
  return 'gir-kind-op'
})
</script>

<style scoped>
.gir-node-inner {
  padding: 6px 14px;
  min-width: 60px;
  max-width: 200px;
  text-align: center;
  font-size: 0.75rem;
  font-weight: 500;
  border: 1px solid var(--vf-node-border, #30363d);
  background: var(--vf-node-bg, #21262d);
  color: var(--vf-node-text, #e4e6eb);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  border-radius: 6px;
  box-sizing: border-box;
  position: relative;
}
.gir-node-label {
  display: inline-block;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  vertical-align: middle;
}
/* Clickable breakpoint toggle: always visible on non-subgraph nodes */
.gir-breakpoint-toggle {
  position: absolute;
  top: 50%;
  right: 6px;
  transform: translateY(-50%);
  width: 10px;
  height: 10px;
  border-radius: 50%;
  border: 1.5px solid #8b949e;
  background: transparent;
  cursor: pointer;
  flex-shrink: 0;
  z-index: 2;
}
.gir-breakpoint-toggle:hover {
  border-color: #da3633;
  background: rgba(218, 54, 51, 0.2);
}
.gir-breakpoint-toggle--active {
  background: #da3633;
  border-color: #da3633;
}
.gir-breakpoint-toggle--active:hover {
  background: #f85149;
  border-color: #f85149;
}
.gir-node-paused-badge {
  position: absolute;
  left: 4px;
  top: 50%;
  transform: translateY(-50%);
  color: #d29922;
  font-size: 0.5rem;
  animation: gir-pulse 1s ease-in-out infinite;
}
@keyframes gir-pulse {
  50% { opacity: 0.6; }
}

/* 语法高亮：字面量 */
.gir-token-string { color: #7ee787; }
.gir-token-number { color: #79c0ff; }

/* 圆形节点（常量等） */
.gir-shape-circle {
  border-radius: 999px;
}

/* 菱形节点（操作/调用） */
.gir-shape-diamond {
  border-radius: 4px;
  clip-path: polygon(50% 0%, 100% 50%, 50% 100%, 0% 50%);
  min-width: 56px;
  min-height: 36px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 8px 16px;
}

/* 双层圆（EXIT） */
.gir-shape-doublecircle {
  border-radius: 999px;
  border-width: 2px;
  border-style: double;
  padding: 4px 12px;
}

/* 按类别颜色 */
.gir-kind-data { border-color: #7ee787; background: rgba(126, 231, 135, 0.08); }
.gir-kind-op { border-color: #d2a8ff; background: rgba(210, 168, 255, 0.06); }
.gir-kind-exit { border-color: #79c0ff; background: rgba(121, 192, 255, 0.08); }
.gir-kind-func { border-color: #a371f7; border-style: dashed; background: rgba(163, 113, 247, 0.08); }
.gir-kind-subgraph.gir-node-subgraph-inner {
  border-style: dashed;
  border-color: #388bfd;
  color: #58a6ff;
  background: rgba(56, 139, 253, 0.08);
}

/* Unreachable from EXIT */
.gir-node-disabled {
  opacity: 0.35;
  box-shadow: none !important;
  filter: grayscale(0.5);
}
</style>
