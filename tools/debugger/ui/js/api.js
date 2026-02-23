/**
 * Debugger API client. All /api/* requests go through here.
 */

const base = '';

async function jsonFetch(url, opts = {}) {
  const res = await fetch(base + url, {
    ...opts,
    headers: { ...opts.headers, 'Content-Type': 'application/json' },
  });
  return res.json();
}

export async function fetchSnapshot() {
  return jsonFetch('/api/snapshot');
}

export async function fetchRegionMemory(regionName, offset, limit) {
  const url =
    '/api/region/' + encodeURIComponent(regionName) + '/memory?offset=' + offset + '&limit=' + limit;
  return jsonFetch(url);
}

export async function fetchRegionObjects(regionName, offset, limit) {
  const url =
    '/api/region/' + encodeURIComponent(regionName) + '/objects?offset=' + offset + '&limit=' + limit;
  return jsonFetch(url);
}

export async function fetchState() {
  return jsonFetch('/api/state');
}

export async function fetchStepPaused() {
  return jsonFetch('/api/step-paused');
}

export async function fetchLastAlloc() {
  return jsonFetch('/api/last-alloc');
}

/** GET /api/log?offset=N → { lines: string[], nextOffset: number } */
export async function fetchLog(offset = 0) {
  return jsonFetch('/api/log?offset=' + encodeURIComponent(String(offset)));
}

export async function fetchBreakpointTypes() {
  return jsonFetch('/api/breakpoint-types');
}

export async function postBreakpointTypes(enabled) {
  return jsonFetch('/api/breakpoint-types', {
    method: 'POST',
    body: JSON.stringify({ enabled }),
  });
}

export async function postFile(path) {
  return jsonFetch('/api/file', { method: 'POST', body: JSON.stringify({ path }) });
}

export async function postRun(opts) {
  return jsonFetch('/api/run', { method: 'POST', body: JSON.stringify(opts) });
}

export async function postContinue() {
  return jsonFetch('/api/continue', { method: 'POST' });
}

export async function postRestart() {
  return jsonFetch('/api/restart', { method: 'POST' });
}

export async function postSettings(opts) {
  return jsonFetch('/api/settings', { method: 'POST', body: JSON.stringify(opts) });
}
