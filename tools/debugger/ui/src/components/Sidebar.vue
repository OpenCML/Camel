<template>
  <aside :class="['app-sidebar', { collapsed: appStore.sidebarCollapsed }]" id="app-sidebar">
    <div class="sidebar-head">
      <span class="sidebar-brand">Camel Debugger</span>
      <button type="button" class="sidebar-toggle" title="Collapse menu" @click="appStore.toggleSidebar">◀</button>
      <button type="button" class="sidebar-toggle-expand" title="Expand menu" @click="expandSidebar">▶</button>
    </div>
    <div class="sidebar-section">
      <button type="button" class="sidebar-open-script" title="Open a script" @click="showFilePicker = true">Open script</button>
    </div>
    <FilePickerModal
      v-model="showFilePicker"
      :error="filePickerError"
      initial-path="."
      @select="onPickFile"
      @cancel="filePickerError = ''"
    />
    <div class="sidebar-section">
      <div class="sidebar-section-title">Tasks</div>
      <div class="sidebar-tasks">
        <template v-if="!tasks.length">
          <div class="sidebar-tasks-empty">No tasks — open a script to create one</div>
        </template>
        <a
          v-for="t in tasks"
          :key="t.id"
          :href="'#/script/' + String(t.id)"
          :class="['sidebar-task', { active: String(t.id) === String(appStore.currentTaskId), exited: t.taskState === 'exited' }]"
          :title="t.scriptPath || t.id"
          @click.prevent="selectTaskAndGo(t.id)"
        >
          {{ taskLabel(t) }} <span class="task-state-tag">{{ t.taskState }}</span>
        </a>
      </div>
    </div>
  </aside>
</template>

<script setup>
import { computed, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'
import FilePickerModal from './FilePickerModal.vue'

const router = useRouter()
const appStore = useAppStore()
const api = useApi()

const showFilePicker = ref(false)
const filePickerError = ref('')

const tasks = computed(() => appStore.tasks || [])

function taskLabel(t) {
  return (t.scriptPath && t.scriptPath.split(/[/\\]/).pop()) || t.id
}

function selectTaskAndGo(id) {
  appStore.setCurrentTaskId(id)
  router.push('/script/' + String(id))
}

function expandSidebar() {
  if (appStore.sidebarCollapsed) appStore.toggleSidebar()
}

function onPickFile(path) {
  filePickerError.value = ''
  api.postFile(path).then((result) => {
    if (result && result.ok === false && result.error) {
      filePickerError.value = result.error || result.message || 'File not found or failed to load. Please choose another file.'
      return
    }
    const taskId = result && result.taskId != null ? String(result.taskId) : null
    if (taskId) {
      showFilePicker.value = false
      appStore.setCurrentTaskId(taskId)
      router.push('/script/' + taskId)
    }
  }).catch((err) => {
    filePickerError.value = (err && err.message ? err.message : String(err)) || 'Failed to load script. Please try again.'
  })
}
</script>
