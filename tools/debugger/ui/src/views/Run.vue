<template>
  <div class="control-panel">
    <h2>Run</h2>
    <div class="control-row">
      <span class="current-script-path">{{ scriptPath }}</span>
    </div>
    <div class="control-row">
      <label>
        <input type="checkbox" v-model="memoryMonitor" />
        Enable memory monitor
      </label>
    </div>
    <h2 style="margin-top: 16px">Camel Runtime pipeline</h2>
    <p class="control-hint">Click a stage to set a breakpoint (red dot = stop before this stage). Takes effect on Run.</p>
    <div class="pipeline-wrap" role="group" aria-label="Pipeline breakpoints">
      <template v-for="(stage, i) in pipelineStages" :key="stage.id">
        <span v-if="i" class="pipeline-arrow">→</span>
        <span
          :class="['pipeline-stage', { 'bp-on': pipelineBreakpoints.has(stage.id) }]"
          :data-stage="stage.id"
          title="Click to set or clear breakpoint"
          @click="togglePipelineBp(stage.id)"
        >
          <span class="bp-dot"></span><span>{{ stage.label }}</span>
        </span>
      </template>
    </div>
    <div class="control-row">
      <button type="button" class="btn btn-run" :disabled="running || !appStore.currentTaskId" @click="onRun">Run</button>
    </div>
    <h2 style="margin-top: 16px">Settings</h2>
    <div class="control-row">
      <label>
        <input type="checkbox" v-model="verbose" @change="onVerboseChange" />
        Verbose output
      </label>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const memoryMonitor = ref(true)
const verbose = ref(false)
const running = ref(false)
const pipelineBreakpoints = ref(new Set())

const pipelineStages = [
  { id: 'CTS', label: 'CTS' },
  { id: 'CST', label: 'CST' },
  { id: 'AST', label: 'AST' },
  { id: 'GCT', label: 'GCT' },
  { id: 'GIR-M', label: 'GIR-M' },
  { id: 'GIR-E', label: 'GIR-E' },
  { id: 'GIR-R1', label: 'GIR-R1' },
  { id: 'GIR-Rn', label: 'GIR-Rn' },
  { id: 'GIR-Z', label: 'GIR-Z' },
]

const scriptPath = computed(() => {
  const t = appStore.currentTask
  return (t && t.scriptPath) || ''
})

function togglePipelineBp(stageId) {
  const set = new Set(pipelineBreakpoints.value)
  if (set.has(stageId)) set.delete(stageId)
  else set.add(stageId)
  pipelineBreakpoints.value = set
}

function onVerboseChange() {
  api.postSettings({ verbose: verbose.value }, appStore.currentTaskId).catch(() => {})
}

function onRun() {
  const taskId = appStore.currentTaskId
  if (!taskId) return
  running.value = true
  api
    .postRun({
      target: taskId,
      memoryMonitor: memoryMonitor.value,
      allocStep: false,
    })
    .then((d) => {
      running.value = false
      if (d && !d.ok) throw new Error(d.error)
    })
    .catch(() => {
      running.value = false
    })
}

watch(() => appStore.currentTaskId, (id) => {
  if (!id) return
  api.fetchSettings(id).then((d) => {
    if (d && d.verbose !== undefined) verbose.value = !!d.verbose
  })
}, { immediate: true })
</script>
