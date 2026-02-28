<template>
  <div class="app-root">
    <div class="app-body">
      <Sidebar />
      <div class="app-main">
        <AssertionOverlay />
        <NavHeader />
        <main class="main-content" id="main-content">
          <router-view v-slot="{ Component }">
            <component :is="Component" />
          </router-view>
        </main>
        <div
          :class="['log-resize-handle', { 'sidebar-collapsed': appStore.logPanelCollapsed }]"
          id="log-resize-handle"
          title="Drag to resize height"
          ref="resizeHandle"
        >
          <span class="log-toggle" aria-hidden="true">▲</span>
        </div>
        <aside
          :class="['app-log-panel', { collapsed: appStore.logPanelCollapsed }]"
          id="log-sidebar"
          role="complementary"
          aria-label="Log output"
          :style="logPanelStyle"
        >
          <LogPanel />
        </aside>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import Sidebar from './components/Sidebar.vue'
import NavHeader from './components/NavHeader.vue'
import LogPanel from './components/LogPanel.vue'
import AssertionOverlay from './components/AssertionOverlay.vue'
import { useAppStore } from './stores/app'
import { useApi } from './composables/useApi'

const appStore = useAppStore()
const api = useApi()
const resizeHandle = ref(null)

const logPanelStyle = computed(() => ({
  '--log-panel-height': appStore.logPanelHeight + 'px',
}))

function pollState() {
  api.fetchState().then((d) => {
    appStore.setStateFromApi(d)
  }).catch(() => {
    appStore.setStateFromApi({ serverRunning: false, tasks: [] })
  })
}

let stateTimer = null
let stepTimer = null

onMounted(() => {
  appStore.initFromStorage()
  pollState()
  stateTimer = setInterval(pollState, 2000)
  stepTimer = setInterval(() => {
    if (appStore.currentTaskId) pollState()
  }, 1000)

  const handle = resizeHandle.value
  if (handle) {
    const sidebar = handle.nextElementSibling
    const defaultH = 280
    const clamp = (v) => Math.max(120, Math.min(0.8 * window.innerHeight, v))
    let dragging = false
    handle.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return
      if (appStore.logPanelCollapsed) return
      e.preventDefault()
      dragging = true
      const startY = e.clientY
      const startH = appStore.logPanelHeight
      function onMove(ev) {
        if (!dragging) return
        const delta = startY - ev.clientY
        appStore.setLogPanelHeight(startH + delta)
      }
      function onUp() {
        dragging = false
        document.removeEventListener('mousemove', onMove)
        document.removeEventListener('mouseup', onUp)
        document.body.style.cursor = ''
        document.body.style.userSelect = ''
      }
      document.body.style.cursor = 'ns-resize'
      document.body.style.userSelect = 'none'
      document.addEventListener('mousemove', onMove)
      document.addEventListener('mouseup', onUp)
    })
    handle.addEventListener('click', () => {
      if (appStore.logPanelCollapsed) {
        appStore.setLogPanelCollapsed(false)
      }
    })
  }
})

onUnmounted(() => {
  if (stateTimer) clearInterval(stateTimer)
  if (stepTimer) clearInterval(stepTimer)
})
</script>
