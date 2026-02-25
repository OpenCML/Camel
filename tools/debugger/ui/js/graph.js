/**
 * GIR Graph page: fetch DOT from /api/gir-dot and render with Viz.js.
 */

import * as api from './api.js';

const VIZ_CDN = 'https://cdn.jsdelivr.net/npm/@viz-js/viz@3/+esm';

let vizInstance = null;

async function getViz() {
  if (vizInstance) return vizInstance;
  const mod = await import(VIZ_CDN);
  const instanceFn = mod.instance || mod.default?.instance;
  if (!instanceFn) throw new Error('Viz.instance not found');
  vizInstance = await instanceFn();
  return vizInstance;
}

function setStatus(msg, isError = false) {
  const el = document.getElementById('graph-status');
  if (!el) return;
  el.textContent = msg || '';
  el.className = 'graph-status' + (isError ? ' error' : '');
}

const GRAPH_PLACEHOLDER_HTML = '<p class="graph-container-placeholder">GIR graph will appear here after you click Refresh (requires a completed run).</p>';

function clearGraph() {
  const container = document.getElementById('graph-container');
  if (!container) return;
  container.innerHTML = GRAPH_PLACEHOLDER_HTML;
}

export function initGraph() {
  const refreshBtn = document.getElementById('graph-refresh-btn');
  if (!refreshBtn) return;
  refreshBtn.onclick = function () { fetchAndRender(); };
}

/** Fetch current run GIR and render. Optional path for by-path compile (kept for later). */
export async function fetchAndRender(path) {
  const container = document.getElementById('graph-container');
  if (!container) return;
  clearGraph();
  setStatus('Loading DOT…');
  let res;
  try {
    res = await api.fetchGirDot(path, api.getCurrentTaskId());
  } catch (e) {
    setStatus('Request failed: ' + (e.message || e), true);
    return;
  }
  if (!res || !res.ok) {
    const err = res && res.error ? res.error : 'Failed to get GIR DOT';
    const friendly = err === 'run first' ? 'Load and run a script from Home first, then click Refresh.' : err;
    setStatus(friendly, true);
    return;
  }
  const dot = res.dot;
  if (!dot || typeof dot !== 'string') {
    setStatus('Empty DOT returned', true);
    return;
  }
  setStatus('Rendering…');
  try {
    const viz = await getViz();
    const svg = viz.renderSVGElement(dot);
    container.innerHTML = '';
    container.appendChild(svg);
    setStatus('');
  } catch (e) {
    setStatus('Render failed: ' + (e.message || e), true);
  }
}
