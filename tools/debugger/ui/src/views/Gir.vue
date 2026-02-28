<template>
  <div>
    <div class="graph-placeholder-card">
      <p class="graph-placeholder-title">Visual GIR</p>
      <p class="graph-placeholder-desc">View the GIR (Graph IR) for the script. Run the script first, then click Refresh to render the graph.</p>
      <div class="graph-toolbar">
        <button type="button" class="btn btn-continue" @click="fetchAndRender">Refresh</button>
        <span class="graph-hint">Fetches DOT from the current run and renders below.</span>
      </div>
    </div>
    <div :class="['graph-status', { error: graphError }]" aria-live="polite">{{ graphStatus }}</div>
    <div class="graph-container" ref="graphContainerRef">
      <p v-if="!graphSvg" class="graph-container-placeholder">GIR graph will appear here after you click Refresh (requires a completed run).</p>
      <component :is="'div'" v-else v-html="graphSvg" style="width:100%;"></component>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const graphContainerRef = ref(null)
const graphStatus = ref('Click Refresh to fetch the GIR graph for the current run; if not run yet, Run from the Run tab first.')
const graphError = ref(false)
const graphSvg = ref('')

let vizInstance = null

async function getViz() {
  if (vizInstance) return vizInstance
  const mod = await import('@viz-js/viz')
  const instanceFn = mod.instance || mod.default
  if (!instanceFn) throw new Error('Viz.instance not found')
  vizInstance = await (typeof instanceFn === 'function' ? instanceFn() : instanceFn)
  return vizInstance
}

async function fetchAndRender() {
  if (!graphContainerRef.value) return
  graphSvg.value = ''
  graphStatus.value = 'Loading DOT…'
  graphError.value = false
  let res
  try {
    res = await api.fetchGirDot(null, appStore.currentTaskId)
  } catch (e) {
    graphStatus.value = 'Request failed: ' + (e.message || e)
    graphError.value = true
    return
  }
  if (!res || !res.ok) {
    const err = res && res.error ? res.error : 'Failed to get GIR DOT'
    graphStatus.value = err === 'run first' ? 'Run the script from the Run tab first, then click Refresh.' : err
    graphError.value = true
    return
  }
  const dot = res.dot
  if (!dot || typeof dot !== 'string') {
    graphStatus.value = 'Empty DOT returned'
    graphError.value = true
    return
  }
  graphStatus.value = 'Rendering…'
  try {
    const viz = await getViz()
    const svgEl = viz.renderSVGElement(dot)
    graphSvg.value = svgEl.outerHTML
    graphStatus.value = ''
  } catch (e) {
    graphStatus.value = 'Render failed: ' + (e.message || e)
    graphError.value = true
  }
}
</script>
