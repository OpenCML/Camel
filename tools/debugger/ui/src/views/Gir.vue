<template>
  <div class="gir-view">
    <header class="gir-header">
      <div class="gir-header-text">
        <h2 class="gir-title">Visual GIR</h2>
        <p class="gir-desc">Click a node to select &amp; highlight inputs. Double‑click a function node to enter its subgraph, or double‑click other nodes to toggle breakpoints.</p>
      </div>
      <div class="gir-toolbar">
        <button type="button" class="btn btn-continue" @click="fetchAndRender">Refresh</button>
        <button type="button" class="btn btn-secondary" :disabled="!canGoBack" @click="goBack">Back</button>
      </div>
    </header>
    <nav v-if="graphStack.length > 0" class="gir-breadcrumb" aria-label="Graph hierarchy">
      <span
        v-for="(crumb, idx) in graphStack"
        :key="crumb.id"
        class="gir-crumb"
      ><a class="gir-crumb-link" href="#" @click.prevent="navigateToCrumb(idx)">{{ crumb.name }}</a><span class="gir-crumb-sep"> / </span></span>
      <span class="gir-crumb gir-crumb-current">{{ currentGraphName }}</span>
    </nav>
    <p v-if="graphStatus" :class="['gir-status', { error: graphError }]" aria-live="polite">{{ graphStatus }}</p>
    <div class="gir-canvas-wrap" ref="graphContainerRef">
      <p v-if="!graphLoaded" class="gir-empty">Load a script and run it, then click Refresh to show the graph.</p>
      <VueFlow
        v-else
        v-model:nodes="flowNodes"
        v-model:edges="flowEdges"
        :node-types="nodeTypes"
        :default-viewport="{ zoom: 0.9 }"
        :fit-view-options="{ padding: 0.2, minZoom: 0.2, maxZoom: 1.5 }"
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
import { VueFlow, Position } from '@vue-flow/core'
import '@vue-flow/core/dist/style.css'
import '@vue-flow/core/dist/theme-default.css'
import dagre from 'dagre'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'
import GirNode from '../components/GirNode.vue'

const appStore = useAppStore()
const api = useApi()

const nodeTypes = { default: GirNode }

const graphContainerRef = ref(null)
const graphStatus = ref('Click Refresh to fetch the GIR graph for the current run; if not run yet, use the header Run/Continue first.')
const graphError = ref(false)
const graphLoaded = ref(false)
const flowNodes = ref([])
const flowEdges = ref([])
const graphStack = ref([])
const currentGraphId = ref(null)
const currentGraphName = ref('')
const breakpointNodeIds = ref(new Set())
const pausedNodeId = ref(null)
const selectedNodeId = ref(null)
let statePollTimer = null

const canGoBack = computed(() => graphStack.value.length > 0)

const NODE_WIDTH = 180
const NODE_HEIGHT = 48
const SUBGRAPH_NODE_WIDTH = 160
const SUBGRAPH_GAP = 24

function applyDagreLayout(nodes, edges) {
  const g = new dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}))
  g.setGraph({ rankdir: 'LR', nodesep: 80, ranksep: 100, marginx: 20, marginy: 20 })
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

function displayLabelFromNode(n) {
  if (n.dataRepr != null && n.dataRepr !== '') return n.dataRepr
  if (n.funcName != null && n.funcName !== '') return n.funcName
  if (n.funcGraphName != null && n.funcGraphName !== '') return n.funcGraphName
  if (n.portName != null && n.portName !== '') return n.portName
  if (n.operName != null && n.operName !== '') return n.operName
  if (n.indexRepr != null && n.indexRepr !== '') return '.' + n.indexRepr
  if (n.nodeRepr != null && n.nodeRepr !== '') return n.nodeRepr
  return n.label || n.type || n.id
}

function tooltipTextFromNode(n) {
  if (n.graphName != null && n.nodeRepr != null) return n.graphName + '::' + n.nodeRepr
  return n.tooltip || ''
}

const EDGE_STYLE = {
  Norm: { stroke: '#7ee787', strokeDasharray: null },
  With: { stroke: '#a371f7', strokeDasharray: '6 4' },
  Ctrl: { stroke: '#79c0ff', strokeDasharray: '4 2' }
}
const EDGE_PATH_OPTIONS = { offset: 24, borderRadius: 14 }

function computeReachable(nodes, edges) {
  const exitNode = nodes.find((n) => n.data?.type === 'EXIT')
  if (!exitNode) return new Set(nodes.map((n) => n.id))
  const reverseAdj = {}
  for (const n of nodes) reverseAdj[n.id] = []
  for (const e of edges) {
    if (reverseAdj[e.source]) reverseAdj[e.source].push(e.target)
    if (reverseAdj[e.target]) reverseAdj[e.target].push(e.source)
  }
  const visited = new Set()
  const queue = [exitNode.id]
  visited.add(exitNode.id)
  while (queue.length > 0) {
    const cur = queue.shift()
    for (const neighbor of (reverseAdj[cur] || [])) {
      if (!visited.has(neighbor)) {
        visited.add(neighbor)
        queue.push(neighbor)
      }
    }
  }
  return visited
}

function graphToFlow(graph) {
  if (!graph || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
    return { nodes: [], edges: [] }
  }
  const childNameToId = {}
  if (Array.isArray(graph.children)) {
    for (const ch of graph.children) {
      if (ch && ch.name && ch.id) childNameToId[ch.name] = ch.id
    }
  }
  const nodes = graph.nodes.map((n) => {
    const nodeData = {
      displayLabel: displayLabelFromNode(n),
      tooltipText: tooltipTextFromNode(n),
      type: n.type,
      shape: n.shape,
      style: n.style,
      dataRepr: n.dataRepr,
      kind: n.kind,
      hasBreakpoint: false,
      isPaused: false,
      unreachable: false
    }
    if (n.type === 'FUNC' && n.funcGraphName && childNameToId[n.funcGraphName]) {
      nodeData.graphId = childNameToId[n.funcGraphName]
    }
    return {
      id: n.id,
      position: { x: 0, y: 0 },
      sourcePosition: Position.Right,
      targetPosition: Position.Left,
      data: nodeData,
      class: 'gir-node node-type-' + (n.type || 'default')
    }
  })
  const edges = graph.edges.map((e) => {
    const linkType = e.linkType || 'Norm'
    const cfg = EDGE_STYLE[linkType] || EDGE_STYLE.Norm
    const portLabel = e.sourcePortIndex != null && e.targetPortIndex != null ? `${e.sourcePortIndex}|${e.targetPortIndex}` : ''
    const baseStyle = {
      stroke: cfg.stroke,
      strokeDasharray: cfg.strokeDasharray || undefined,
      strokeWidth: 2,
      strokeLinecap: 'round',
      strokeLinejoin: 'round'
    }
    return {
      id: e.id,
      source: e.sourceId,
      target: e.targetId,
      type: 'smoothstep',
      pathOptions: EDGE_PATH_OPTIONS,
      label: portLabel || undefined,
      labelStyle: portLabel ? { fontSize: '10px', fill: '#8b949e' } : undefined,
      labelBgStyle: portLabel ? { fill: '#21262d', stroke: '#30363d' } : undefined,
      labelBgPadding: portLabel ? [2, 4] : undefined,
      labelBgBorderRadius: portLabel ? 2 : undefined,
      style: { ...baseStyle },
      data: { originalStyle: { ...baseStyle } },
      sourceHandle: e.sourcePortIndex != null ? String(e.sourcePortIndex) : undefined,
      targetHandle: e.targetPortIndex != null ? String(e.targetPortIndex) : undefined
    }
  })
  let laidOut = applyDagreLayout(nodes, edges)

  const reachable = computeReachable(laidOut, edges)
  laidOut = laidOut.map((n) => {
    if (!reachable.has(n.id)) {
      return { ...n, data: { ...n.data, unreachable: true } }
    }
    return n
  })

  let minX = Infinity
  let maxX = -Infinity
  let maxY = -Infinity
  laidOut.forEach((n) => {
    minX = Math.min(minX, n.position.x)
    maxX = Math.max(maxX, n.position.x + NODE_WIDTH)
    maxY = Math.max(maxY, n.position.y + NODE_HEIGHT)
  })
  if (minX === Infinity) minX = 0
  if (maxX === -Infinity) maxX = 0
  if (maxY === -Infinity) maxY = 0
  const subgraphY = maxY + 70
  const subgraphRowWidth = (graph.children?.length || 0) * SUBGRAPH_NODE_WIDTH + (Math.max(0, (graph.children?.length || 0) - 1)) * SUBGRAPH_GAP
  const subgraphStartX = minX + Math.max(0, (maxX - minX - subgraphRowWidth) / 2)
  if (Array.isArray(graph.children) && graph.children.length > 0) {
    graph.children.forEach((ch, i) => {
      if (ch && ch.id) {
        laidOut = laidOut.concat({
          id: 'subgraph:' + ch.id,
          position: {
            x: subgraphStartX + i * (SUBGRAPH_NODE_WIDTH + SUBGRAPH_GAP),
            y: subgraphY
          },
          sourcePosition: Position.Right,
          targetPosition: Position.Left,
          data: {
            displayLabel: ch.name || 'subgraph',
            tooltipText: 'Double-click to enter: ' + (ch.name || ch.id),
            type: 'subgraph',
            graphId: ch.id,
            hasBreakpoint: false,
            isPaused: false,
            unreachable: false
          },
          class: 'gir-node gir-node-subgraph'
        })
      }
    })
  }
  return { nodes: laidOut, edges }
}

async function loadGraphById(graphId, graphName) {
  graphStatus.value = 'Loading…'
  graphError.value = false
  selectedNodeId.value = null
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
  currentGraphName.value = graphName || graph.name || graphId
  graphStatus.value = ''
  await refreshBreakpointsAndPaused()
}

function applyBreakpointStyles() {
  const bp = breakpointNodeIds.value
  const paused = pausedNodeId.value
  flowNodes.value = flowNodes.value.map((n) => ({
    ...n,
    data: {
      ...n.data,
      hasBreakpoint: bp.has(n.id),
      isPaused: n.id === paused
    },
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

function highlightInputEdges(nodeId) {
  flowEdges.value = flowEdges.value.map((e) => {
    const orig = e.data?.originalStyle || e.style
    if (e.target === nodeId) {
      return {
        ...e,
        style: { ...orig, stroke: '#f0f6fc', strokeWidth: 2.5 },
        animated: true,
        zIndex: 10
      }
    }
    return {
      ...e,
      style: { ...orig },
      animated: false,
      zIndex: undefined
    }
  })
}

function onNodeClick({ node }) {
  selectedNodeId.value = node.id
  highlightInputEdges(node.id)
}

function enterSubgraph(graphId, graphName) {
  graphStack.value.push({ id: currentGraphId.value, name: currentGraphName.value })
  loadGraphById(graphId, graphName)
}

async function onNodeDoubleClick({ node }) {
  const hasSubgraph = node.data?.graphId
  if (hasSubgraph) {
    enterSubgraph(node.data.graphId, node.data.displayLabel || node.data.graphId)
    return
  }
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

function navigateToCrumb(index) {
  const target = graphStack.value[index]
  graphStack.value = graphStack.value.slice(0, index)
  loadGraphById(target.id, target.name)
}

async function goBack() {
  if (graphStack.value.length === 0) return
  const prev = graphStack.value.pop()
  if (prev) {
    await loadGraphById(prev.id, prev.name)
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
  currentGraphName.value = graph.name || rootSummary.name || rootSummary.id
  selectedNodeId.value = null
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
/* Breadcrumb navigation */
.gir-breadcrumb {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0;
  font-size: 0.8125rem;
  color: #8b949e;
  padding: 4px 0;
}
.gir-crumb-link {
  color: #58a6ff;
  text-decoration: none;
  cursor: pointer;
}
.gir-crumb-link:hover {
  text-decoration: underline;
  color: #79c0ff;
}
.gir-crumb-sep {
  color: #484f58;
  margin: 0 2px;
}
.gir-crumb-current {
  color: #e4e6eb;
  font-weight: 600;
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
.gir-flow :deep(.vue-flow__background) {
  background-color: var(--vf-bg);
  background-image: radial-gradient(circle at 1px 1px, #21262d 1px, transparent 0);
  background-size: 20px 20px;
}
.gir-flow :deep(.vue-flow__container) {
  font-family: 'Segoe UI', system-ui, sans-serif;
}
/* Node shell: reset padding/border so GirNode.vue controls everything */
.gir-flow :deep(.vue-flow__node) {
  padding: 0;
  border: none;
  border-radius: 6px;
  font-size: 0.75rem;
  font-weight: 500;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.2);
  transition: box-shadow 0.15s ease, border-color 0.15s ease;
  background: transparent;
}
/* Breakpoint / Paused: target GirNode inner element */
.gir-flow :deep(.gir-breakpoint .gir-node-inner) {
  border-color: #da3633 !important;
  box-shadow: 0 0 0 1px #da3633;
}
.gir-flow :deep(.gir-paused .gir-node-inner) {
  border-color: #d29922 !important;
  background: rgba(210, 153, 34, 0.12);
  box-shadow: 0 0 0 1px #d29922;
}
/* Connection handles */
.gir-flow :deep(.vue-flow__handle) {
  width: 6px;
  height: 6px;
  background: #484f58;
  border: 1px solid #21262d;
}
.gir-flow :deep(.vue-flow__handle:hover) {
  background: #58a6ff;
}
/* Edges */
.gir-flow :deep(.vue-flow__edge-path) {
  stroke: #484f58;
  stroke-width: 1.5;
}
.gir-flow :deep(.vue-flow__edge.selected .vue-flow__edge-path),
.gir-flow :deep(.vue-flow__edge:focus-visible .vue-flow__edge-path) {
  stroke: #58a6ff;
}
</style>
