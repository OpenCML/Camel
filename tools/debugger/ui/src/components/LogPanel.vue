<template>
  <div class="log-panel">
    <div class="log-panel-header">
        <div class="log-tabs" role="tablist">
          <button
            type="button"
            :class="['log-tab', { active: appStore.logTab === 'debugger' }]"
            role="tab"
            :aria-selected="appStore.logTab === 'debugger'"
            @click="appStore.setLogTab('debugger')"
          >
            Debugger
          </button>
          <button
            type="button"
            :class="['log-tab', { active: appStore.logTab === 'task' }]"
            role="tab"
            :aria-selected="appStore.logTab === 'task'"
            @click="appStore.setLogTab('task')"
          >
            Task
          </button>
        </div>
        <div class="log-panel-actions">
          <button type="button" class="btn-icon" title="Clear log" aria-label="Clear log" @click="clearLog">⌫</button>
          <button type="button" class="log-toggle-inner btn-icon" title="Collapse" @click="toggleCollapsed">▶</button>
        </div>
      </div>
      <div class="log-contents">
        <pre
          id="log-content-debugger"
          class="log-content"
          :data-log-tab="'debugger'"
          :aria-hidden="appStore.logTab !== 'debugger'"
          ref="debuggerPre"
        ></pre>
        <pre
          id="log-content-task"
          class="log-content"
          :data-log-tab="'task'"
          :aria-hidden="appStore.logTab !== 'task'"
          ref="taskPre"
        ></pre>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const debuggerPre = ref(null)
const taskPre = ref(null)

let logNextOffsetDebugger = 0
let logNextOffsetTask = 0
let lastLogTaskId = null
let pollTimer = null

function appendLogLines(el, lines, nextOffset, isDebugger) {
  if (!el || !lines.length) return
  lines.forEach((line) => {
    el.appendChild(document.createTextNode(line + '\n'))
  })
  if (isDebugger) logNextOffsetDebugger = nextOffset
  else logNextOffsetTask = nextOffset
  el.scrollTop = el.scrollHeight
}

function pollLog() {
  const taskId = appStore.currentTaskId
  if (taskId !== lastLogTaskId) {
    lastLogTaskId = taskId
    logNextOffsetTask = 0
    if (taskPre.value) {
      taskPre.value.textContent = ''
    }
  }
  api.fetchLog(logNextOffsetDebugger, null).then((d) => {
    const lines = d.lines || []
    const next = d.nextOffset != null ? d.nextOffset : logNextOffsetDebugger + lines.length
    appendLogLines(debuggerPre.value, lines, next, true)
  }).catch(() => {})

  if (taskId) {
    api.fetchLog(logNextOffsetTask, taskId).then((d) => {
      const lines = d.lines || []
      const next = d.nextOffset != null ? d.nextOffset : logNextOffsetTask + lines.length
      appendLogLines(taskPre.value, lines, next, false)
    }).catch(() => {})
  }
}

function clearLog() {
  logNextOffsetDebugger = 0
  logNextOffsetTask = 0
  if (debuggerPre.value) debuggerPre.value.textContent = ''
  if (taskPre.value) taskPre.value.textContent = ''
}

function toggleCollapsed() {
  appStore.setLogPanelCollapsed(!appStore.logPanelCollapsed)
}

onMounted(() => {
  pollTimer = setInterval(pollLog, 800)
})

onUnmounted(() => {
  if (pollTimer) clearInterval(pollTimer)
})
</script>
