<template>
  <div class="pipeline-view">
    <header class="pipeline-header">
      <div class="pipeline-header-text">
        <h2 class="pipeline-title">Pipeline</h2>
        <p class="pipeline-hint">Click a dot between stages to set a breakpoint (red = stop before next stage). Applied when you run.</p>
      </div>
    </header>
    <div class="pipeline-canvas-wrap">
      <p v-if="!pipelineStages.length" class="pipeline-empty">Select a task to load the pipeline.</p>
      <div v-else class="pipeline-strip" role="group" aria-label="Pipeline stages and breakpoints">
        <template v-for="(stage, i) in pipelineStages" :key="stage.id">
          <span
            v-if="i > 0"
            class="pipeline-connector"
            aria-hidden="true"
          />
          <button
            type="button"
            :class="['pipeline-bp-dot', { active: pipelineBreakpoints.includes(stage.id) }]"
            :title="'Break before ' + stage.label"
            :aria-pressed="pipelineBreakpoints.includes(stage.id)"
            :aria-label="'Toggle breakpoint before ' + stage.label"
            @click="toggleBp(stage.id)"
          >
            <span class="pipeline-bp-dot-inner" />
          </button>
          <span v-if="i < pipelineStages.length - 1" class="pipeline-connector" aria-hidden="true" />
          <span class="pipeline-stage-box">{{ stage.label }}</span>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, provide } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const DEFAULT_STAGES = [
  { id: 'CTS', label: 'CTS' },
  { id: 'CST', label: 'CST' },
  { id: 'AST', label: 'AST' },
  { id: 'GCT', label: 'GCT' },
  { id: 'std::default', label: 'fallback' },
  { id: 'GIR-Z', label: 'GIR-Z' },
]

const pipelineStages = ref([...DEFAULT_STAGES])
const pipelineBreakpoints = computed(() => appStore.pipelineBreakpoints)

function syncPipelineBreakpointsToBackend() {
  const pipelineIds = appStore.pipelineBreakpoints
  const taskId = appStore.currentTaskId
  if (!taskId) return
  const ids = pipelineStages.value.map((s) => s.id)
  api.fetchBreakpointTypes(taskId).then((data) => {
    const enabled = (data && data.enabled) || []
    const other = enabled.filter((t) => !ids.includes(t))
    api.postBreakpointTypes([...pipelineIds, ...other], taskId).catch(() => {})
  }).catch(() => {})
}

provide('syncPipelineBreakpoints', syncPipelineBreakpointsToBackend)

function toggleBp(stageId) {
  const current = appStore.pipelineBreakpoints
  const next = current.includes(stageId)
    ? current.filter((id) => id !== stageId)
    : [...current, stageId]
  appStore.setPipelineBreakpoints(next)
  syncPipelineBreakpointsToBackend()
}

async function loadPipeline(taskId) {
  if (!taskId) {
    pipelineStages.value = [...DEFAULT_STAGES]
    return
  }
  try {
    const data = await api.fetchPipeline(taskId)
    const stages = (data && data.stages) || []
    pipelineStages.value = stages.length ? stages : [...DEFAULT_STAGES]
  } catch (_) {
    pipelineStages.value = [...DEFAULT_STAGES]
  }
}

watch(
  () => appStore.currentTaskId,
  async (taskId) => {
    await loadPipeline(taskId)
    if (!taskId) return
    try {
      const data = await api.fetchBreakpointTypes(taskId)
      const enabled = (data && data.enabled) || []
      const ids = pipelineStages.value.map((s) => s.id)
      const pipelineIds = enabled.filter((t) => ids.includes(t))
      appStore.setPipelineBreakpoints(pipelineIds)
    } catch (_) {}
  },
  { immediate: true }
)
</script>

<style scoped>
.pipeline-view {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
  min-height: 0;
}
.pipeline-header {
  flex-shrink: 0;
}
.pipeline-title {
  font-size: 1.05rem;
  font-weight: 600;
  color: #e4e6eb;
  margin: 0 0 4px;
}
.pipeline-hint {
  font-size: 0.8125rem;
  color: #8b949e;
  margin: 0;
}
.pipeline-canvas-wrap {
  flex: 1;
  min-height: 120px;
  display: flex;
  align-items: center;
  padding: 16px;
  background: var(--bg-panel-alt);
  border: 1px solid var(--border-divider-subtle);
  border-radius: 6px;
  overflow-x: auto;
}
.pipeline-empty {
  width: 100%;
  text-align: center;
  font-size: 0.875rem;
  color: #6e7681;
  margin: 0;
}
.pipeline-strip {
  display: flex;
  align-items: center;
  gap: 0;
  flex-wrap: nowrap;
  min-width: min-content;
}
.pipeline-connector {
  width: 12px;
  height: 1px;
  background: #484f58;
  flex-shrink: 0;
}
.pipeline-bp-dot {
  width: 20px;
  height: 20px;
  padding: 0;
  border: none;
  border-radius: 50%;
  background: transparent;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.pipeline-bp-dot:hover {
  background: rgba(88, 166, 255, 0.12);
}
.pipeline-bp-dot-inner {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #484f58;
  transition: background 0.15s, box-shadow 0.15s;
}
.pipeline-bp-dot:hover .pipeline-bp-dot-inner {
  background: #6e7681;
}
.pipeline-bp-dot.active .pipeline-bp-dot-inner {
  background: #f85149;
  box-shadow: 0 0 0 2px rgba(248, 81, 73, 0.4);
}
.pipeline-stage-box {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 56px;
  padding: 6px 12px;
  border-radius: 6px;
  border: 1px solid #30363d;
  background: #21262d;
  color: #e4e6eb;
  font-size: 0.75rem;
  font-weight: 500;
  flex-shrink: 0;
}
</style>
