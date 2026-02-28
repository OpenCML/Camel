<template>
  <nav class="nav" aria-label="Main header">
    <div class="nav-left">
      <h1 class="nav-title">{{ pageTitle }}</h1>
      <span v-if="appStore.stepStatusText" id="step-status" class="nav-title-detail step-status">{{ appStore.stepStatusText }}</span>
      <span v-if="appStore.lastAllocText" id="last-alloc" class="nav-title-detail last-alloc">{{ appStore.lastAllocText }}</span>
    </div>
    <div class="nav-right">
      <div class="nav-debug" aria-label="Debug controls">
        <div class="nav-debug-toolbar">
          <button
            type="button"
            id="btn-continue"
            class="nav-debug-btn nav-debug-btn-continue"
            title="Continue (F5)"
            aria-label="Continue"
            :disabled="!appStore.paused"
            @click="onContinue"
          >
            <span class="nav-debug-icon">▶</span>
          </button>
          <button
            type="button"
            id="btn-restart"
            class="nav-debug-btn nav-debug-btn-restart"
            title="Restart"
            aria-label="Restart"
            :disabled="!appStore.canRestart"
            @click="onRestart"
          >
            <span class="nav-debug-icon">↻</span>
          </button>
          <button
            type="button"
            id="btn-terminate"
            class="nav-debug-btn nav-debug-btn-terminate"
            title="Terminate"
            aria-label="Terminate"
            :disabled="!appStore.canTerminate"
            @click="onTerminate"
          >
            <span class="nav-debug-icon">■</span>
          </button>
          <span class="nav-debug-sep" aria-hidden="true"></span>
          <label class="nav-debug-alloc" title="Verbose output">
            <input type="checkbox" v-model="verbose" @change="onVerboseChange" />
            Verbose
          </label>
        </div>
      </div>
      <span class="nav-status-sep" aria-hidden="true"></span>
      <div class="nav-status" id="nav-status">
        <div class="nav-api-status" id="control-status">{{ appStore.apiStatus || 'API: checking…' }}</div>
      </div>
    </div>
  </nav>
</template>

<script setup>
import { ref, computed, watch } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const verbose = ref(false)

const pageTitle = computed(() => {
  const t = appStore.currentTask
  if (!t || !t.scriptPath) return 'Camel Debugger'
  const parts = (t.scriptPath || '').split(/[/\\]/)
  return parts[parts.length - 1] || t.scriptPath || 'Camel Debugger'
})

function onContinue() {
  api.postContinue(appStore.currentTaskId).then(() => {}).catch(() => {})
}

function onRestart() {
  api.postRestart(appStore.currentTaskId).then(() => {}).catch(() => {})
}

function onTerminate() {
  api.postTerminate(appStore.currentTaskId).then(() => {}).catch(() => {})
}

function onVerboseChange() {
  api.postSettings({ verbose: verbose.value }, appStore.currentTaskId).then(() => {}).catch(() => {})
}

watch(() => appStore.currentTaskId, () => {
  if (!appStore.currentTaskId) return
  api.fetchSettings(appStore.currentTaskId).then((d) => {
    if (d && d.verbose !== undefined) verbose.value = !!d.verbose
  })
}, { immediate: true })
</script>
