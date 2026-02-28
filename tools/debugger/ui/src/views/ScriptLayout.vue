<template>
  <div class="main-tabbed">
    <div class="main-tabs" role="tablist">
      <button
        v-for="tab in tabs"
        :key="tab.id"
        type="button"
        :class="['main-tab', { active: appStore.currentTab === tab.id }]"
        :data-tab="tab.id"
        :aria-selected="appStore.currentTab === tab.id"
        role="tab"
        @click="setTab(tab.id)"
      >
        {{ tab.label }}
      </button>
    </div>
    <div class="main-panels">
      <div
        id="panel-run"
        class="main-panel"
        :class="{ active: appStore.currentTab === 'run' }"
        role="tabpanel"
        data-tab="run"
      >
        <RunView />
      </div>
      <div
        id="panel-memory"
        class="main-panel"
        :class="{ active: appStore.currentTab === 'memory' }"
        role="tabpanel"
        data-tab="memory"
      >
        <MemoryView />
      </div>
      <div
        id="panel-gir"
        class="main-panel graph-page"
        :class="{ active: appStore.currentTab === 'gir' }"
        role="tabpanel"
        data-tab="gir"
      >
        <GirView />
      </div>
    </div>
  </div>
</template>

<script setup>
import { onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'
import RunView from './Run.vue'
import MemoryView from './Memory.vue'
import GirView from './Gir.vue'

const route = useRoute()
const router = useRouter()
const appStore = useAppStore()
const api = useApi()

onMounted(async () => {
  const taskId = route.params.taskId
  if (!taskId) {
    router.replace('/')
    return
  }
  const state = await api.fetchState().catch(() => ({}))
  const tasks = (state && state.tasks) || []
  const exists = tasks.some((t) => String(t.id) === String(taskId))
  if (!exists) {
    router.replace('/')
    return
  }
  appStore.setCurrentTaskId(taskId)
})

const tabs = [
  { id: 'run', label: 'Run' },
  { id: 'memory', label: 'Memory' },
  { id: 'gir', label: 'GIR' },
]

function setTab(tab) {
  appStore.setCurrentTab(tab)
}
</script>
