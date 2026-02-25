/**
 * Debugger API client. All /api/* requests go through here.
 * Task/target: per-target commands use current task id when present.
 */

const base = '';

let currentTaskId = null;
export function getCurrentTaskId() {
  return currentTaskId;
}
export function setCurrentTaskId(id) {
  currentTaskId = id == null || id === '' ? null : id;
}
export function setCurrentTaskIdFromState(state) {
  if (state && state.tasks && state.tasks.length) {
    const ids = state.tasks.map((t) => t.id);
    if (!currentTaskId || !ids.includes(currentTaskId)) currentTaskId = state.tasks[0].id;
  } else {
    currentTaskId = null;
  }
}

async function jsonFetch(url, opts = {}) {
  const res = await fetch(base + url, {
    ...opts,
    headers: { ...opts.headers, 'Content-Type': 'application/json' },
  });
  const text = await res.text();
  if (!res.ok) {
    try {
      const j = JSON.parse(text);
      return j;
    } catch (_) {
      throw new Error(res.status === 502 ? 'API unreachable – is camel-db running with serve?' : text || res.statusText);
    }
  }
  try {
    return text ? JSON.parse(text) : {};
  } catch (_) {
    return {};
  }
}

export async function fetchSnapshot(target = null) {
  const url = target != null && target !== '' ? '/api/snapshot?target=' + encodeURIComponent(target) : '/api/snapshot';
  return jsonFetch(url);
}

export async function fetchRegionMemory(regionName, offset, limit, target = null) {
  let url = '/api/region/' + encodeURIComponent(regionName) + '/memory?offset=' + offset + '&limit=' + limit;
  if (target != null && target !== '') url += '&target=' + encodeURIComponent(target);
  return jsonFetch(url);
}

export async function fetchRegionObjects(regionName, offset, limit, target = null) {
  let url = '/api/region/' + encodeURIComponent(regionName) + '/objects?offset=' + offset + '&limit=' + limit;
  if (target != null && target !== '') url += '&target=' + encodeURIComponent(target);
  return jsonFetch(url);
}

export async function fetchState(target = null) {
  const url = target != null && target !== '' ? '/api/state?target=' + encodeURIComponent(target) : '/api/state';
  return jsonFetch(url);
}

/** GET /api/settings[?target=id] → { verbose, logFile, allocStepEnabled, allocBreakpointsAvailable, memoryMonitorRunning }. With target, returns that task's settings. */
export async function fetchSettings(target = null) {
  const url = target != null && target !== '' ? '/api/settings?target=' + encodeURIComponent(target) : '/api/settings';
  return jsonFetch(url);
}

export async function fetchStepPaused(target = null) {
  const url = target != null && target !== '' ? '/api/step-paused?target=' + encodeURIComponent(target) : '/api/step-paused';
  return jsonFetch(url);
}

export async function fetchLastAlloc(target = null) {
  const url = target != null && target !== '' ? '/api/last-alloc?target=' + encodeURIComponent(target) : '/api/last-alloc';
  return jsonFetch(url);
}

/** GET /api/log?offset=N[&target=id] → { lines: string[], nextOffset: number }. With target, request is forwarded to that task's log. */
export async function fetchLog(offset = 0, target = null) {
  let url = '/api/log?offset=' + encodeURIComponent(String(offset));
  if (target != null && target !== '') url += '&target=' + encodeURIComponent(target);
  return jsonFetch(url);
}

export async function fetchBreakpointTypes(target = null) {
  const url = target != null && target !== '' ? '/api/breakpoint-types?target=' + encodeURIComponent(target) : '/api/breakpoint-types';
  return jsonFetch(url);
}

export async function fetchBreakpointSpaces(target = null) {
  const url = target != null && target !== '' ? '/api/breakpoint-spaces?target=' + encodeURIComponent(target) : '/api/breakpoint-spaces';
  return jsonFetch(url);
}

export async function postBreakpointSpaces(breakSpaces, target = null) {
  const body = { breakSpaces };
  if (target != null && target !== '') body.target = target;
  return jsonFetch('/api/breakpoint-spaces', { method: 'POST', body: JSON.stringify(body) });
}

export async function postBreakpointTypes(enabled, target = null) {
  const body = { enabled };
  if (target != null && target !== '') body.target = target;
  return jsonFetch('/api/breakpoint-types', { method: 'POST', body: JSON.stringify(body) });
}

export async function postFile(path) {
  return jsonFetch('/api/file', { method: 'POST', body: JSON.stringify({ path }) });
}

export async function postRun(opts) {
  return jsonFetch('/api/run', { method: 'POST', body: JSON.stringify(opts) });
}

export async function postContinue(target = null) {
  const body = target != null && target !== '' ? { target } : {};
  return jsonFetch('/api/continue', { method: 'POST', body: JSON.stringify(body) });
}

export async function postRestart(target = null) {
  const body = target != null && target !== '' ? { target } : {};
  return jsonFetch('/api/restart', { method: 'POST', body: JSON.stringify(body) });
}

export async function postTerminate(target = null) {
  const body = target != null && target !== '' ? { target } : {};
  return jsonFetch('/api/terminate', { method: 'POST', body: JSON.stringify(body) });
}

export async function postSettings(opts, target = null) {
  const body = { ...opts };
  if (target != null && target !== '') body.target = target;
  return jsonFetch('/api/settings', { method: 'POST', body: JSON.stringify(body) });
}

/** GET /api/gir-dot or /api/gir-dot?path=...&target=... → { ok: boolean, dot?: string, error?: string }. Omit path for current run. */
export async function fetchGirDot(path = null, target = null) {
  const params = new URLSearchParams();
  if (path) params.set('path', path);
  if (target != null && target !== '') params.set('target', target);
  const q = params.toString();
  return jsonFetch('/api/gir-dot' + (q ? '?' + q : ''));
}
