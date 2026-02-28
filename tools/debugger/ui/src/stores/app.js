import { defineStore } from 'pinia'

const UI_STORAGE = {
  TAB: 'camel-db-main-tab',
  SIDEBAR_COLLAPSED: 'camel-db-sidebar-collapsed',
  LOG_PANEL: 'camel-db-log-panel',
}

export const useAppStore = defineStore('app', {
  state: () => ({
    openScripts: [],
    currentTab: 'pipeline',
    sidebarCollapsed: false,
    currentTaskId: null,
    tasks: [],
    apiStatus: '',
    serverRunning: false,
    logPanelCollapsed: false,
    logPanelHeight: 280,
    logTab: 'debugger',
    stepStatus: '',
    lastAlloc: '',
    paused: false,
    pauseReason: null,
    canRestart: false,
    canTerminate: false,
    assertionError: null,
    stepStatusText: '',
    lastAllocText: '',
    /** Pipeline breakpoint stage IDs per task: { [taskId]: string[] } */
    pipelineBreakpointsByTask: {},
  }),

  getters: {
    hasScript: (state) => state.tasks.length > 0,
    currentTask: (state) =>
      state.currentTaskId == null
        ? null
        : (state.tasks || []).find((t) => String(t.id) === String(state.currentTaskId)) || null,
    /** Pipeline breakpoint stage IDs for current task (for Pipeline tab). */
    pipelineBreakpoints: (state) => {
      const id = state.currentTaskId
      if (!id) return []
      const arr = state.pipelineBreakpointsByTask[String(id)]
      return Array.isArray(arr) ? arr : []
    },
  },

  actions: {
    setPipelineBreakpoints(stageIds) {
      const id = this.currentTaskId
      if (id == null) return
      const key = String(id)
      this.pipelineBreakpointsByTask = { ...this.pipelineBreakpointsByTask, [key]: stageIds }
    },

    initFromStorage() {
      try {
        const tab = sessionStorage.getItem(UI_STORAGE.TAB)
        if (tab && ['pipeline', 'memory', 'gir'].includes(tab)) this.currentTab = tab
        const collapsed = sessionStorage.getItem(UI_STORAGE.SIDEBAR_COLLAPSED)
        if (collapsed === 'true') this.sidebarCollapsed = true
        const logCollapsed = sessionStorage.getItem(UI_STORAGE.LOG_PANEL)
        if (logCollapsed === 'collapsed') this.logPanelCollapsed = true
      } catch (_) {}
    },

    setCurrentTab(tab) {
      this.currentTab = tab
      try {
        sessionStorage.setItem(UI_STORAGE.TAB, tab)
      } catch (_) {}
    },

    toggleSidebar() {
      this.sidebarCollapsed = !this.sidebarCollapsed
      try {
        sessionStorage.setItem(UI_STORAGE.SIDEBAR_COLLAPSED, this.sidebarCollapsed ? 'true' : '')
      } catch (_) {}
    },

    addOpenScript(path) {
      if (!path || typeof path !== 'string') return
      const norm = path.trim()
      if (!norm) return
      this.openScripts = this.openScripts.filter((p) => p !== norm)
      this.openScripts.unshift(norm)
      if (this.openScripts.length > 20) this.openScripts = this.openScripts.slice(0, 20)
    },

    setCurrentTaskId(id) {
      this.currentTaskId = id == null || id === '' ? null : id
    },

    setCurrentTaskIdFromState(state) {
      if (!state || !state.tasks || !state.tasks.length) {
        this.currentTaskId = null
        return
      }
      const alive = state.tasks.filter((t) => t.taskState !== 'exited')
      const ids = state.tasks.map((t) => t.id)
      const current = this.currentTaskId ? state.tasks.find((t) => String(t.id) === String(this.currentTaskId)) : null
      if (current && current.taskState === 'exited') {
        this.currentTaskId = alive.length ? alive[0].id : current.id
        return
      }
      if (!this.currentTaskId || !ids.some((t) => String(t.id) === String(this.currentTaskId))) {
        this.currentTaskId = alive.length ? alive[0].id : state.tasks[0].id
      }
    },

    setLogPanelCollapsed(collapsed) {
      this.logPanelCollapsed = !!collapsed
      try {
        sessionStorage.setItem(UI_STORAGE.LOG_PANEL, this.logPanelCollapsed ? 'collapsed' : '')
      } catch (_) {}
    },

    setLogTab(tab) {
      this.logTab = tab === 'task' ? 'task' : 'debugger'
    },

    setLogPanelHeight(h) {
      this.logPanelHeight = Math.max(120, Math.min(0.8 * window.innerHeight, h))
    },

    setStateFromApi(d) {
      if (!d || typeof d !== 'object') return
      this.serverRunning = !!d.serverRunning
      this.tasks = d.tasks || []
      this.setCurrentTaskIdFromState(d)
      const taskId = this.currentTaskId
      const tasks = this.tasks
      const task = taskId ? tasks.find((t) => t.id === taskId) : null
      const taskState = task ? task.taskState : (d.taskState || 'idle')
      if (task) {
        this.paused = task.paused === true
        this.pauseReason = task.pauseReason || null
        this.lastAlloc = task.lastAlloc && Object.keys(task.lastAlloc).length ? task.lastAlloc : null
        this.stepStatusText = this.paused ? formatPauseReason(task.pauseReason || task) : ''
        this.lastAllocText = this.lastAlloc ? formatLastAlloc(this.lastAlloc) : ''
      } else {
        this.paused = false
        this.pauseReason = null
        this.lastAlloc = null
        this.stepStatusText = ''
        this.lastAllocText = ''
      }
      this.canRestart = !!taskId && !!task
      this.canTerminate = !!taskId && task && task.taskState !== 'exited'
      const labels = { idle: 'No task', loaded: 'Loaded', running: 'Running', paused: 'Paused', completed: 'Completed', terminated: 'Terminated', exited: 'Exited' }
      this.apiStatus = this.serverRunning
        ? 'API: Connected · Task: ' + (labels[taskState] || taskState)
        : 'API: Not Running – Start camel-db and run serve'
      this.assertionError = (task && task.assertionError) || d.assertionError || null
    },
  },
})

function fmtBytes(n) {
  if (n < 1024) return n + ' B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(2) + ' KB'
  return (n / (1024 * 1024)).toFixed(2) + ' MB'
}

function formatPauseReason(d) {
  if (!d || !d.phase) return d && d.ptr !== undefined ? 'Paused after alloc – Continue or Restart' : 'Paused – Continue or Restart'
  if (d.phase === 'before') return 'Paused before alloc (size ' + fmtBytes(d.size || 0) + ', ' + (d.space || '?') + ') – Continue or Restart'
  if (d.phase === 'pipeline') return 'Paused before stage ' + (d.stageId || '?') + ' – Continue or Restart'
  if (d.phase === 'gir_node') return 'Paused at GIR node – Continue or Restart'
  return 'Paused after alloc (0x' + (d.ptr != null ? d.ptr.toString(16) : '0') + ', ' + fmtBytes(d.size || 0) + ') – Continue or Restart'
}

function formatLastAlloc(d) {
  if (!d || d.size === undefined) return ''
  const space = d.space || '?'
  const sizeStr = fmtBytes(d.size)
  if (d.phase === 'before') return 'About to alloc: ' + space + ' ' + sizeStr
  if (d.ptr !== undefined) return 'Last: ' + space + ' 0x' + d.ptr.toString(16) + ' ' + sizeStr
  return 'Last: ' + space + ' ' + sizeStr
}
