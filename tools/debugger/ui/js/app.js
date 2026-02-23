/**
 * App entry: routing, run page (file/run/settings), and monitor page init.
 */

import * as api from './api.js';
import * as monitor from './monitor.js';

const OPEN_SCRIPTS_KEY = 'camel-db-open-scripts';
const SIDEBAR_COLLAPSED_KEY = 'camel-db-sidebar-collapsed';

function getRoute() {
  const h = (location.hash || '#/').slice(1);
  return h === '' ? '/' : h[0] === '/' ? h : '/' + h;
}

/** Path from route /script/encodedPath, or null. */
function getScriptPathFromRoute() {
  const r = getRoute();
  if (r.indexOf('/script/') !== 0) return null;
  try {
    return decodeURIComponent(r.slice('/script/'.length));
  } catch (_) {
    return null;
  }
}

let openScripts = [];
function loadOpenScripts() {
  try {
    const raw = sessionStorage.getItem(OPEN_SCRIPTS_KEY);
    if (raw) openScripts = JSON.parse(raw);
    else openScripts = [];
  } catch (_) {
    openScripts = [];
  }
}
function saveOpenScripts() {
  try {
    sessionStorage.setItem(OPEN_SCRIPTS_KEY, JSON.stringify(openScripts));
  } catch (_) {}
}
function addOpenScript(path) {
  if (!path || typeof path !== 'string') return;
  const norm = path.trim();
  if (!norm) return;
  openScripts = openScripts.filter((p) => p !== norm);
  openScripts.unshift(norm);
  if (openScripts.length > 20) openScripts = openScripts.slice(0, 20);
  saveOpenScripts();
  renderSidebarScripts();
}

function renderSidebarScripts() {
  const el = document.getElementById('sidebar-scripts');
  if (!el) return;
  const currentPath = getScriptPathFromRoute();
  if (openScripts.length === 0) {
    el.innerHTML = '<div class="sidebar-scripts-empty">No scripts opened</div>';
    return;
  }
  el.innerHTML = openScripts
    .map((path) => {
      const label = path.split(/[/\\]/).pop() || path;
      const href = '#/script/' + encodeURIComponent(path);
      const active = currentPath === path ? ' active' : '';
      return '<a href="' + href + '" class="sidebar-script' + active + '" title="' + path + '">' + escapeHtml(label) + '</a>';
    })
    .join('');
}
function escapeHtml(s) {
  const div = document.createElement('div');
  div.textContent = s;
  return div.innerHTML;
}

function updateSidebarActive() {
  const r = getRoute();
  const scriptPath = getScriptPathFromRoute();
  document.querySelectorAll('.sidebar-item').forEach((a) => a.classList.remove('active'));
  document.querySelectorAll('.sidebar-script').forEach((a) => a.classList.remove('active'));
  if (r === '/') document.getElementById('sidebar-home')?.classList.add('active');
  else if (r === '/monitor' && !scriptPath) document.getElementById('sidebar-monitor')?.classList.add('active');
  else if (scriptPath) {
    const scriptEl = document.querySelector('.sidebar-script[href="#/script/' + encodeURIComponent(scriptPath) + '"]');
    if (scriptEl) scriptEl.classList.add('active');
  }
}

function applyRoute() {
  const r = getRoute();
  const scriptPath = getScriptPathFromRoute();
  if (scriptPath) addOpenScript(scriptPath);
  const isMonitor = r === '/monitor' || r.indexOf('/script/') === 0;
  document.getElementById('page-home').classList.toggle('active', !isMonitor);
  document.getElementById('page-monitor').classList.toggle('active', isMonitor);
  updateSidebarActive();
  if (isMonitor) monitor.fetchData();
}

let memoryMonitorRunning = false;
const breakpointTypeLabels = { alloc: 'Memory alloc' };

function loadBreakpointTypes() {
  const container = document.getElementById('breakpoint-types-container');
  if (!container) return;
  api.fetchBreakpointTypes().then((d) => {
    const known = d.known || [];
    const enabled = (d.enabled || []).reduce((o, k) => {
      o[k] = true;
      return o;
    }, {});
    container.innerHTML = '';
    if (known.length === 0) {
      container.innerHTML = '<span class="control-status">No breakpoint types (run once to register)</span>';
      return;
    }
    known.forEach((type) => {
      const label = document.createElement('label');
      label.style.marginRight = '16px';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.setAttribute('data-type', type);
      cb.checked = !!enabled[type];
      cb.onchange = function () {
        const list = [];
        container.querySelectorAll('input[data-type]').forEach((c) => {
          if (c.checked) list.push(c.getAttribute('data-type'));
        });
        api.postBreakpointTypes(list).catch(() => {});
      };
      label.appendChild(cb);
      label.appendChild(document.createTextNode(breakpointTypeLabels[type] || type));
      container.appendChild(label);
    });
  }).catch(() => {
    container.innerHTML = '<span class="control-status">Breakpoint types: API unreachable</span>';
  });
}

function updateAssertionOverlay(state) {
  const overlay = document.getElementById('assertion-overlay');
  const msgEl = document.getElementById('assertion-overlay-message');
  if (!overlay || !msgEl) return;
  const assertionError = state && state.assertionError;
  if (assertionError) {
    let text = assertionError;
    if (state.assertionExpression || state.assertionFile || state.assertionLine) {
      const parts = [];
      if (state.assertionExpression)
        parts.push('Expression: ' + state.assertionExpression);
      if (state.assertionFile || state.assertionLine)
        parts.push('Location: ' + (state.assertionFile || '') + (state.assertionLine ? ':' + state.assertionLine : ''));
      if (parts.length) text = parts.join('\n') + '\n\n' + text;
    }
    msgEl.textContent = text;
    overlay.style.display = 'flex';
  } else {
    overlay.style.display = 'none';
  }
}

function updateControlStatus() {
  api.fetchState().then((d) => {
    if (!d || typeof d !== 'object') return;
    memoryMonitorRunning = !!d.memoryMonitorRunning;
    updateAssertionOverlay(d.assertionError ? d : null);
    const st = document.getElementById('control-status');
    const verboseCb = document.getElementById('opt-verbose');
    const verboseMon = document.getElementById('opt-verbose-monitor');
    if (d.verbose !== undefined) {
      if (verboseCb) verboseCb.checked = !!d.verbose;
      if (verboseMon) verboseMon.checked = !!d.verbose;
    }
    if (!st) return;
    if (!d.serverRunning) {
      st.innerHTML = '<span class="err">API: not running</span> – start camel-db and run <code>serve</code>';
      return;
    }
    st.innerHTML = '<span class="ok">API: connected</span>';
    if (d.hasFile && d.targetFile) {
      st.innerHTML += ' · Loaded: <code>' + escapeHtml(d.targetFile) + '</code>';
      const inp = document.getElementById('input-file-path');
      if (inp && inp.value !== d.targetFile) inp.value = d.targetFile;
      addOpenScript(d.targetFile);
    }
    if (d.memoryMonitorRunning) st.innerHTML += ' · Memory scan on';
    loadBreakpointTypes();
  }).catch(() => {
    updateAssertionOverlay(null);
    const st = document.getElementById('control-status');
    if (st) st.innerHTML = '<span class="err">API: unreachable</span> – start camel-db and run <code>serve</code>, then start serve_ui.py';
  });
}

function initRunPage() {
  document.getElementById('btn-load-file').onclick = function () {
    const path = document.getElementById('input-file-path').value.trim();
    const statusEl = document.getElementById('control-status');
    if (!path) {
      statusEl.innerHTML = '<span class="err">Enter a file path</span>';
      return;
    }
    statusEl.innerHTML = '<span class="content-loading"><span class="spinner"></span>Loading file…</span>';
    api.postFile(path).then((d) => {
      updateControlStatus();
      if (!d.ok) statusEl.innerHTML = '<span class="err">' + (d.error || 'Load failed') + '</span>';
    }).catch((e) => {
      statusEl.innerHTML = '<span class="err">' + e + '</span>';
    });
  };

  document.getElementById('btn-run').onclick = function () {
    const path = document.getElementById('input-file-path').value.trim();
    const memoryMonitor = document.getElementById('opt-memory-monitor').checked;
    const allocCb = document.querySelector('#breakpoint-types-container input[data-type="alloc"]');
    const allocStep = allocCb ? allocCb.checked : false;
    const statusEl = document.getElementById('control-status');
    const btn = this;
    btn.disabled = true;
    location.hash = path ? '#/script/' + encodeURIComponent(path) : '#/monitor';
    function done(ok, err) {
      btn.disabled = false;
      updateControlStatus();
      if (err) statusEl.innerHTML = '<span class="err">' + err + '</span>';
    }
    if (path) {
      api
        .postFile(path)
        .then((d) => {
          if (!d.ok) {
            done(false, d.error || 'Load failed');
            return;
          }
          return api.postRun({ memoryMonitor, allocStep });
        })
        .then((d) => {
          if (d && !d.ok) done(false, d.error);
          else done(true);
        })
        .catch((e) => done(false, e.message || e));
    } else {
      api
        .postRun({ memoryMonitor, allocStep })
        .then((d) => done(d.ok, d.ok ? null : d.error || 'Run failed'))
        .catch((e) => done(false, e.message || e));
    }
  };

  function setVerboseFromCheckbox(checked) {
    api.postSettings({ verbose: checked }).then((d) => {
      if (!d.ok) updateControlStatus();
    }).catch(() => updateControlStatus());
  }
  document.getElementById('opt-verbose').onchange = function () {
    setVerboseFromCheckbox(this.checked);
    const m = document.getElementById('opt-verbose-monitor');
    if (m) m.checked = this.checked;
  };
  document.getElementById('opt-verbose-monitor').onchange = function () {
    setVerboseFromCheckbox(this.checked);
    const h = document.getElementById('opt-verbose');
    if (h) h.checked = this.checked;
  };
}

function initSidebar() {
  const sidebar = document.getElementById('app-sidebar');
  const btnCollapse = document.getElementById('sidebar-toggle');
  const btnExpand = document.getElementById('sidebar-toggle-expand');
  if (!sidebar) return;
  try {
    const raw = sessionStorage.getItem(SIDEBAR_COLLAPSED_KEY);
    if (raw === 'true') sidebar.classList.add('collapsed');
  } catch (_) {}
  function setCollapsed(collapsed) {
    sidebar.classList.toggle('collapsed', collapsed);
    try {
      sessionStorage.setItem(SIDEBAR_COLLAPSED_KEY, String(collapsed));
    } catch (_) {}
  }
  if (btnCollapse) btnCollapse.onclick = () => setCollapsed(true);
  if (btnExpand) btnExpand.onclick = () => setCollapsed(false);
}

function main() {
  // 状态轮询优先注册，避免后续 init 抛错导致不再轮询
  setInterval(updateControlStatus, 2000);
  updateControlStatus();

  loadOpenScripts();
  renderSidebarScripts();
  initSidebar();
  const assertionRestartBtn = document.getElementById('assertion-overlay-restart');
  if (assertionRestartBtn) {
    assertionRestartBtn.onclick = function () {
      api.postRestart().then(() => updateControlStatus()).catch(() => updateControlStatus());
    };
  }
  window.addEventListener('hashchange', applyRoute);
  monitor.initMonitor();
  initRunPage();
  applyRoute();
  setInterval(() => {
    if (memoryMonitorRunning && getRoute() !== '/') monitor.fetchData(false);
  }, 1500);
  setInterval(monitor.pollStepStatus, 1000);
  setInterval(() => {
    const r = getRoute();
    if (r === '/monitor' || r.indexOf('/script/') === 0) monitor.pollLog();
  }, 800);
}

main();
