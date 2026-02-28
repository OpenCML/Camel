<template>
  <div class="gir-view">
    <header class="gir-header">
      <div class="gir-header-text">
        <h2 class="gir-title">Visual GIR</h2>
        <p class="gir-desc">Graph IR for the current script. Run (or Continue when loaded) first, then Refresh. Click a subgraph to expand; double‑click a node to set a breakpoint.</p>
      </div>
      <div class="gir-toolbar">
        <button type="button" class="btn btn-continue" @click="fetchAndRender">Refresh</button>
        <button type="button" class="btn btn-secondary" :disabled="!canGoBack" @click="goBack">Back</button>
      </div>
    </header>
    <p v-if="graphStatus" :class="['gir-status', { error: graphError }]" aria-live="polite">{{ graphStatus }}</p>
    <div class="gir-canvas-wrap" ref="graphContainerRef">
      <p v-if="!graphLoaded" class="gir-empty">Load a script and run it, then click Refresh to show the graph.</p>
      <VueFlow
        v-else
        v-model:nodes="flowNodes"
        v-model:edges="flowEdges"
        :default-viewport="{ zoom: 0.85 }"
        fit-view-on-init
        class="gir-flow"
        @node-click="onNodeClick"
        @node-double-click="onNodeDoubleClick"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { VueFlow } from '@vue-flow/core'
import '@vue-flow/core/dist/style.css'
import '@vue-flow/core/dist/theme-default.css'
import dagre from 'dagre'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const graphContainerRef = ref(null)
const graphStatus = ref('Click Refresh to fetch the GIR graph for the current run; if not run yet, use the header Run/Continue first.')
const graphError = ref(false)
const graphLoaded = ref(false)
const flowNodes = ref([])
const flowEdges = ref([])
const graphStack = ref([])
const currentGraphId = ref(null)
const breakpointNodeIds = ref(new Set())
const pausedNodeId = ref(null)
let statePollTimer = null

const canGoBack = computed(() => graphStack.value.length > 0)

const NODE_WIDTH = 100
const NODE_HEIGHT = 40

function applyDagreLayout(nodes, edges) {
  const g = new dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}))
  g.setGraph({ rankdir: 'LR', nodesep: 40, ranksep: 60 })
  nodes.forEach((n) => g.setNode(n.id, { width: NODE_WIDTH, height: NODE_HEIGHT }))
  edges.forEach((e) => g.setEdge(e.source, e.target))
  dagre.layout(g)
  return nodes.map((n) => {
    const pos = g.node(n.id)
    return {
      ...n,
      position: { x: pos.x - NODE_WIDTH / 2, y: pos.y - NODE_HEIGHT / 2 }
    }
  })
}

function graphToFlow(graph) {
  if (!graph || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
    return { nodes: [], edges: [] }
  }
  const nodes = graph.nodes.map((n) => ({
    id: n.id,
    position: { x: 0, y: 0 },
    data: {
      label: n.label || n.type || n.id,
      type: n.type,
      shape: n.shape,
      style: n.style,
      tooltip: n.tooltip
    },
    class: 'gir-node node-type-' + (n.type || 'default')
  }))
  const edges = graph.edges.map((e) => ({
    id: e.id,
    source: e.sourceId,
    target: e.targetId,
    sourceHandle: e.sourcePortIndex != null ? String(e.sourcePortIndex) : undefined,
    targetHandle: e.targetPortIndex != null ? String(e.targetPortIndex) : undefined
  }))
  let laidOut = applyDagreLayout(nodes, edges)
  let maxX = 0
  let maxY = 0
  laidOut.forEach((n) => {
    maxX = Math.max(maxX, n.position.x + NODE_WIDTH)
    maxY = Math.max(maxY, n.position.y + NODE_HEIGHT)
  })
  if (Array.isArray(graph.children) && graph.children.length > 0) {
    graph.children.forEach((ch, i) => {
      if (ch && ch.id) {
        laidOut = laidOut.concat({
          id: 'subgraph:' + ch.id,
          position: { x: maxX + 80, y: 30 + i * (NODE_HEIGHT + 12) },
          data: {
            label: (ch.name || 'subgraph') + ' ▶',
            type: 'subgraph',
            graphId: ch.id,
            tooltip: 'Click to expand: ' + (ch.name || ch.id)
          },
          class: 'gir-node gir-node-subgraph'
        })
      }
    })
  }
  return { nodes: laidOut, edges }
}

async function loadGraphById(graphId) {
  graphStatus.value = 'Loading…'
  graphError.value = false
  let res
  try {
    res = await api.fetchGirJson(null, graphId, appStore.currentTaskId)
  } catch (e) {
    graphStatus.value = 'Request failed: ' + (e.message || e)
    graphError.value = true
    return
  }
  if (!res || res.ok === false) {
    graphStatus.value = (res && res.error) || 'Failed to load graph'
    graphError.value = true
    return
  }
  const graph = res.graph
  const { nodes, edges } = graphToFlow(graph)
  flowNodes.value = nodes
  flowEdges.value = edges
  currentGraphId.value = graphId
  graphStatus.value = ''
  await refreshBreakpointsAndPaused()
}

function applyBreakpointStyles() {
  const bp = breakpointNodeIds.value
  const paused = pausedNodeId.value
  flowNodes.value = flowNodes.value.map((n) => ({
    ...n,
    class: [n.class || '', bp.has(n.id) ? 'gir-breakpoint' : '', n.id === paused ? 'gir-paused' : ''].filter(Boolean).join(' ')
  }))
}

async function refreshBreakpointsAndPaused() {
  try {
    const res = await api.fetchGirBreakpoints(appStore.currentTaskId)
    if (res && Array.isArray(res.nodeIds)) breakpointNodeIds.value = new Set(res.nodeIds)
  } catch (_) {}
  try {
    const state = await api.fetchState()
    const task = state?.tasks?.find((t) => String(t.id) === String(appStore.currentTaskId) || t.port === Number(appStore.currentTaskId))
    if (task?.paused && (task?.lastAlloc?.phase === 'gir_node' || task?.pauseReason?.phase === 'gir_node')) {
      const reason = task.lastAlloc || task.pauseReason
      pausedNodeId.value = reason?.nodeId ?? null
    } else
      pausedNodeId.value = null
  } catch (_) {}
  applyBreakpointStyles()
}

function onNodeClick({ node }) {
  if (node.data && node.data.type === 'subgraph' && node.data.graphId) {
    graphStack.value.push(currentGraphId.value)
    loadGraphById(node.data.graphId)
  }
}

async function onNodeDoubleClick({ node }) {
  if (node.data?.type === 'subgraph' || node.id.startsWith('subgraph:')) return
  const next = new Set(breakpointNodeIds.value)
  if (next.has(node.id)) next.delete(node.id)
  else next.add(node.id)
  breakpointNodeIds.value = next
  try {
    await api.postGirBreakpoints([...next], appStore.currentTaskId)
  } catch (_) {}
  applyBreakpointStyles()
}

async function goBack() {
  if (graphStack.value.length === 0) return
  const prev = graphStack.value.pop()
  if (prev) {
    await loadGraphById(prev)
  } else {
    await fetchAndRender()
  }
}

async function fetchAndRender() {
  if (!graphContainerRef.value) return
  flowNodes.value = []
  flowEdges.value = []
  graphLoaded.value = false
  graphStatus.value = 'Loading GIR…'
  graphError.value = false
  let res
  try {
    res = await api.fetchGirJson(null, null, appStore.currentTaskId)
  } catch (e) {
    graphStatus.value = 'Request failed: ' + (e.message || e)
    graphError.value = true
    return
  }
  if (!res || res.ok === false) {
    const err = res && res.error ? res.error : 'Failed to get GIR'
    graphStatus.value = err === 'run first' ? 'Run the script (header ▶) first, then click Refresh.' : err
    graphError.value = true
    return
  }
  const rootSummary = res.graph
  if (!rootSummary || !rootSummary.id) {
    graphStatus.value = 'Empty graph returned'
    graphError.value = true
    return
  }
  graphStatus.value = 'Loading root graph…'
  let expanded
  try {
    expanded = await api.fetchGirJson(null, rootSummary.id, appStore.currentTaskId)
  } catch (e) {
    graphStatus.value = 'Request failed: ' + (e.message || e)
    graphError.value = true
    return
  }
  if (!expanded || expanded.ok === false) {
    graphStatus.value = (expanded && expanded.error) || 'Failed to expand root graph'
    graphError.value = true
    return
  }
  const graph = expanded.graph
  graphStack.value = []
  currentGraphId.value = rootSummary.id
  const { nodes, edges } = graphToFlow(graph)
  flowNodes.value = nodes
  flowEdges.value = edges
  graphLoaded.value = true
  graphStatus.value = ''
  await refreshBreakpointsAndPaused()
}

watch([breakpointNodeIds, pausedNodeId], () => applyBreakpointStyles(), { deep: true })

onMounted(() => {
  statePollTimer = setInterval(() => {
    if (graphLoaded.value && appStore.currentTaskId) refreshBreakpointsAndPaused()
  }, 1500)
})
onUnmounted(() => {
  if (statePollTimer) clearInterval(statePollTimer)
})
</script>

<style scoped>
.gir-view {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
  min-height: 0;
}
.gir-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
}
.gir-header-text {
  min-width: 0;
  flex: 1;
}
.gir-title {
  font-size: 1.05rem;
  font-weight: 600;
  color: #e4e6eb;
  margin: 0 0 4px;
  letter-spacing: 0.01em;
}
.gir-desc {
  font-size: 0.8125rem;
  color: #8b949e;
  margin: 0;
  line-height: 1.4;
  max-width: 52ch;
}
.gir-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}
.btn-secondary {
  background: var(--bg-panel);
  color: #8b949e;
  border: 1px solid #30363d;
}
.btn-secondary:hover:not(:disabled) {
  background: #2d333b;
  color: #e4e6eb;
  border-color: #484f58;
}
.btn-secondary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.gir-status {
  font-size: 0.8125rem;
  color: #8b949e;
  margin: 0;
  min-height: 1.25em;
}
.gir-status.error {
  color: #f85149;
}
.gir-canvas-wrap {
  flex: 1;
  min-height: 280px;
  background: var(--bg-panel-alt);
  border: var(--border-divider-subtle);
  border-radius: var(--radius-panel);
  overflow: hidden;
  position: relative;
}
.gir-empty {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0;
  font-size: 0.875rem;
  color: #6e7681;
  padding: 24px;
  text-align: center;
}
.gir-flow {
  width: 100%;
  height: 100%;
  --vf-node-bg: #21262d;
  --vf-node-border: #30363d;
  --vf-node-text: #e4e6eb;
  --vf-edge: #484f58;
  --vf-bg: #0d1117;
}
/* Canvas background: subtle dot grid */
.gir-flow :deep(.vue-flow__background) {
  background-color: var(--vf-bg);
  background-image: radial-gradient(circle at 1px 1px, #21262d 1px, transparent 0);
  background-size: 20px 20px;
}
.gir-flow :deep(.vue-flow__container) {
  font-family: 'Segoe UI', system-ui, sans-serif;
}
/* Default node: compact, readable */
.gir-flow :deep(.vue-flow__node) {
  padding: 0;
  border: none;
  border-radius: 6px;
  font-size: 0.75rem;
  font-weight: 500;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.2);
  transition: box-shadow 0.15s ease, border-color 0.15s ease;
}
/* Node inner content: support both default and custom structure */
.gir-flow :deep(.vue-flow__node .vue-flow__node-content),
.gir-flow :deep(.vue-flow__node > div) {
  padding: 6px 12px;
  border-radius: 6px;
  border: 1px solid var(--vf-node-border);
  background: var(--vf-node-bg);
  color: var(--vf-node-text);
  max-width: 140px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.gir-flow :deep(.vue-flow__node.selected .vue-flow__node-content),
.gir-flow :deep(.vue-flow__node.selected > div),
.gir-flow :deep(.vue-flow__node:focus-visible .vue-flow__node-content),
.gir-flow :deep(.vue-flow__node:focus-visible > div) {
  border-color: #58a6ff;
  box-shadow: 0 0 0 1px #58a6ff;
}
/* Subgraph node */
.gir-flow :deep(.gir-node-subgraph .vue-flow__node-content),
.gir-flow :deep(.gir-node-subgraph > div) {
  border-style: dashed;
  border-color: #388bfd;
  color: #58a6ff;
  background: rgba(56, 139, 253, 0.08);
}
.gir-flow :deep(.gir-node-subgraph:hover .vue-flow__node-content),
.gir-flow :deep(.gir-node-subgraph:hover > div) {
  background: rgba(56, 139, 253, 0.12);
}
/* Breakpoint: subtle red ring */
.gir-flow :deep(.gir-breakpoint .vue-flow__node-content),
.gir-flow :deep(.gir-breakpoint > div) {
  border-color: #da3633;
  box-shadow: 0 0 0 1px #da3633;
}
.gir-flow :deep(.gir-paused .vue-flow__node-content),
.gir-flow :deep(.gir-paused > div) {
  border-color: #d29922;
  background: rgba(210, 153, 34, 0.12);
  box-shadow: 0 0 0 1px #d29922;
}
/* Connection handles: small and subtle */
.gir-flow :deep(.vue-flow__handle) {
  width: 6px;
  height: 6px;
  background: #484f58;
  border: 1px solid #21262d;
}
.gir-flow :deep(.vue-flow__handle:hover) {
  background: #58a6ff;
}
/* Edges: softer line */
.gir-flow :deep(.vue-flow__edge-path) {
  stroke: #484f58;
  stroke-width: 1.5;
}
.gir-flow :deep(.vue-flow__edge.selected .vue-flow__edge-path),
.gir-flow :deep(.vue-flow__edge:focus-visible .vue-flow__edge-path) {
  stroke: #58a6ff;
}
</style>
