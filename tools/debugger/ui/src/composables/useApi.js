import { useAppStore } from '../stores/app'

const base = ''

async function jsonFetch(url, opts = {}) {
  const res = await fetch(base + url, {
    ...opts,
    headers: { ...opts.headers, 'Content-Type': 'application/json' },
  })
  const text = await res.text()
  if (!res.ok) {
    try {
      return JSON.parse(text)
    } catch (_) {
      throw new Error(res.status === 502 ? 'API unreachable – is camel-db running with serve?' : text || res.statusText)
    }
  }
  try {
    return text ? JSON.parse(text) : {}
  } catch (_) {
    return {}
  }
}

export function useApi() {
  const appStore = useAppStore()

  function getCurrentTaskId() {
    return appStore.currentTaskId
  }

  function setCurrentTaskId(id) {
    appStore.setCurrentTaskId(id)
  }

  function setCurrentTaskIdFromState(state) {
    appStore.setCurrentTaskIdFromState(state)
  }

  async function fetchSnapshot(target = null) {
    const url = target != null && target !== '' ? '/api/snapshot?target=' + encodeURIComponent(target) : '/api/snapshot'
    return jsonFetch(url)
  }

  async function fetchRegionMemory(regionName, offset, limit, target = null) {
    let url = '/api/region/' + encodeURIComponent(regionName) + '/memory?offset=' + offset + '&limit=' + limit
    if (target != null && target !== '') url += '&target=' + encodeURIComponent(target)
    return jsonFetch(url)
  }

  async function fetchRegionObjects(regionName, offset, limit, target = null) {
    let url = '/api/region/' + encodeURIComponent(regionName) + '/objects?offset=' + offset + '&limit=' + limit
    if (target != null && target !== '') url += '&target=' + encodeURIComponent(target)
    return jsonFetch(url)
  }

  /** 全局状态（任务列表 + 聚合的 paused/pauseReason/lastAlloc/assertionError）。仅 GET /api/state，无 target。 */
  async function fetchState() {
    return jsonFetch('/api/state')
  }

  /** 单个任务状态（worker 的 state，含 assertionError 等）。GET /api/task-state?target=<taskId>。 */
  async function fetchTaskState(taskId) {
    if (taskId == null || taskId === '') return {}
    return jsonFetch('/api/task-state?target=' + encodeURIComponent(String(taskId)))
  }

  async function fetchSettings(target = null) {
    const url = target != null && target !== '' ? '/api/settings?target=' + encodeURIComponent(target) : '/api/settings'
    return jsonFetch(url)
  }

  async function fetchLog(offset = 0, target = null) {
    let url = '/api/log?offset=' + encodeURIComponent(String(offset))
    if (target != null && target !== '') url += '&target=' + encodeURIComponent(target)
    return jsonFetch(url)
  }

  async function fetchBreakpointTypes(target = null) {
    const url = target != null && target !== '' ? '/api/breakpoint-types?target=' + encodeURIComponent(target) : '/api/breakpoint-types'
    return jsonFetch(url)
  }

  async function fetchBreakpointSpaces(target = null) {
    const url = target != null && target !== '' ? '/api/breakpoint-spaces?target=' + encodeURIComponent(target) : '/api/breakpoint-spaces'
    return jsonFetch(url)
  }

  /** Pipeline stages (compile + passes + GIR-Z) for the task. GET /api/pipeline?target=<taskId>. */
  async function fetchPipeline(target = null) {
    if (target == null || target === '') return { stages: [] }
    return jsonFetch('/api/pipeline?target=' + encodeURIComponent(String(target)))
  }

  async function postBreakpointSpaces(breakSpaces, target = null) {
    const body = { breakSpaces }
    if (target != null && target !== '') body.target = target
    return jsonFetch('/api/breakpoint-spaces', { method: 'POST', body: JSON.stringify(body) })
  }

  async function postBreakpointTypes(enabled, target = null) {
    const body = { enabled }
    if (target != null && target !== '') body.target = target
    return jsonFetch('/api/breakpoint-types', { method: 'POST', body: JSON.stringify(body) })
  }

  async function fetchListDir(dir) {
    const url = dir != null && dir !== '' ? '/api/list-dir?dir=' + encodeURIComponent(dir) : '/api/list-dir'
    return jsonFetch(url)
  }

  async function postFile(path) {
    return jsonFetch('/api/file', { method: 'POST', body: JSON.stringify({ path }) })
  }

  async function postRun(opts) {
    return jsonFetch('/api/run', { method: 'POST', body: JSON.stringify(opts) })
  }

  async function postContinue(target = null) {
    const body = target != null && target !== '' ? { target } : {}
    return jsonFetch('/api/continue', { method: 'POST', body: JSON.stringify(body) })
  }

  async function postRestart(target = null) {
    const body = target != null && target !== '' ? { target } : {}
    return jsonFetch('/api/restart', { method: 'POST', body: JSON.stringify(body) })
  }

  async function postTerminate(target = null) {
    const body = target != null && target !== '' ? { target } : {}
    return jsonFetch('/api/terminate', { method: 'POST', body: JSON.stringify(body) })
  }

  async function postSettings(opts, target = null) {
    const body = { ...opts }
    if (target != null && target !== '') body.target = target
    return jsonFetch('/api/settings', { method: 'POST', body: JSON.stringify(body) })
  }

  async function fetchGirJson(path = null, graphId = null, target = null) {
    const params = new URLSearchParams()
    if (path) params.set('path', path)
    if (graphId != null && graphId !== '') params.set('graphId', graphId)
    if (target != null && target !== '') params.set('target', target)
    const q = params.toString()
    return jsonFetch('/api/gir-json' + (q ? '?' + q : ''))
  }

  async function fetchGirBreakpoints(target = null) {
    const params = new URLSearchParams()
    if (target != null && target !== '') params.set('target', target)
    const q = params.toString()
    return jsonFetch('/api/gir-breakpoints' + (q ? '?' + q : ''))
  }

  async function postGirBreakpoints(nodeIds, target = null) {
    const body = { nodeIds: Array.isArray(nodeIds) ? nodeIds : [] }
    if (target != null && target !== '') body.target = target
    return jsonFetch('/api/gir-breakpoints', { method: 'POST', body: JSON.stringify(body) })
  }

  return {
    getCurrentTaskId,
    setCurrentTaskId,
    setCurrentTaskIdFromState,
    fetchSnapshot,
    fetchRegionMemory,
    fetchRegionObjects,
    fetchState,
    fetchTaskState,
    fetchSettings,
    fetchLog,
    fetchListDir,
    fetchBreakpointTypes,
    fetchBreakpointSpaces,
    fetchPipeline,
    postBreakpointSpaces,
    postBreakpointTypes,
    postFile,
    postRun,
    postContinue,
    postRestart,
    postTerminate,
    postSettings,
    fetchGirJson,
    fetchGirBreakpoints,
    postGirBreakpoints,
  }
}
