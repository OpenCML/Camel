/**
 * App entry: routing, run page (file/run/settings), and monitor page init.
 */

import * as api from './api.js';
import * as monitor from './monitor.js';
import * as graph from './graph.js';

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
let lastTargetFile = '';
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

function renderSidebarTasks(tasks) {
  const el = document.getElementById('sidebar-tasks');
  if (!el) return;
  const currentId = api.getCurrentTaskId();
  const aliveTasks = tasks ? tasks.filter((t) => t.taskState !== 'exited') : [];
  if (!tasks || tasks.length === 0) {
    el.innerHTML = '<div class="sidebar-tasks-empty">No running tasks</div>';
    return;
  }
  el.innerHTML = tasks
    .map((t) => {
      const label = (t.scriptPath && t.scriptPath.split(/[/\\]/).pop()) || t.id;
      const active = t.id === currentId ? ' active' : '';
      const exited = t.taskState === 'exited' ? ' exited' : '';
      return '<a href="#" class="sidebar-task' + active + exited + '" data-task-id="' + escapeHtml(t.id) + '" title="' + escapeHtml(t.scriptPath || t.id) + '">' + escapeHtml(label) + ' <span class="task-state-tag">' + escapeHtml(t.taskState || '') + '</span></a>';
    })
    .join('');
  el.querySelectorAll('.sidebar-task').forEach((a) => {
    a.onclick = (e) => {
      e.preventDefault();
      const id = a.getAttribute('data-task-id');
      if (id) api.setCurrentTaskId(id);
      el.querySelectorAll('.sidebar-task').forEach((x) => x.classList.toggle('active', x.getAttribute('data-task-id') === id));
    };
  });
  // If current task exited, auto-select first alive task (or clear selection)
  if (currentId) {
    const currentTask = tasks.find((t) => t.id === currentId);
    if (currentTask && currentTask.taskState === 'exited' && aliveTasks.length > 0) {
      api.setCurrentTaskId(aliveTasks[0].id);
    }
  }
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
  else if (r === '/graph') document.getElementById('sidebar-graph')?.classList.add('active');
  else if (scriptPath) {
    const scriptEl = document.querySelector('.sidebar-script[href="#/script/' + encodeURIComponent(scriptPath) + '"]');
    if (scriptEl) scriptEl.classList.add('active');
  }
}

function getPageTitle() {
  const r = getRoute();
  const scriptPath = getScriptPathFromRoute();
  if (r === '/') return 'Home';
  if (r === '/monitor' && !scriptPath) return 'Memory monitor';
  if (r === '/graph') return 'Visual GIR';
  if (scriptPath) return scriptPath.split(/[/\\]/).pop() || scriptPath;
  return 'Camel Debugger';
}

function updateNavTitle() {
  const el = document.getElementById('nav-title');
  if (!el) return;
  const title = getPageTitle();
  const isHome = getRoute() === '/';
  el.innerHTML = isHome
    ? '<a href="#/" style="color:inherit;text-decoration:none;">' + escapeHtml(title) + '</a>'
    : escapeHtml(title);
}

function applyRoute() {
  const r = getRoute();
  const scriptPath = getScriptPathFromRoute();
  if (scriptPath) addOpenScript(scriptPath);
  const isMonitor = r === '/monitor' || r.indexOf('/script/') === 0;
  const isGraph = r === '/graph';
  document.getElementById('page-home').classList.toggle('active', !isMonitor && !isGraph);
  document.getElementById('page-monitor').classList.toggle('active', isMonitor);
  document.getElementById('page-graph').classList.toggle('active', isGraph);
  updateSidebarActive();
  updateNavTitle();
  if (isMonitor) monitor.fetchData();
  if (isGraph) graph.fetchAndRender();
}

let memoryMonitorRunning = false;
const breakpointTypeLabels = { alloc: 'Memory alloc' };

// Camel Runtime pipeline (from pipeline diagram): id used in API, label for display
const PIPELINE_STAGES = [
  { id: 'CTS', label: 'CTS' },
  { id: 'CST', label: 'CST' },
  { id: 'AST', label: 'AST' },
  { id: 'GCT', label: 'GCT' },
  { id: 'GIR-M', label: 'GIR-M' },
  { id: 'GIR-E', label: 'GIR-E' },
  { id: 'GIR-R1', label: 'GIR-R1' },
  { id: 'GIR-Rn', label: 'GIR-Rn' },
  { id: 'GIR-Z', label: 'GIR-Z' },
];
function initPipelineAndSpaceBreakpoints() {
  const pipelineWrap = document.getElementById('pipeline-wrap');
  if (!pipelineWrap) return;
  pipelineWrap.innerHTML = '';
  PIPELINE_STAGES.forEach((stage, i) => {
    if (i) pipelineWrap.appendChild(createArrow());
    const el = document.createElement('span');
    el.className = 'pipeline-stage';
    el.setAttribute('data-stage', stage.id);
    el.title = 'Click to set or clear breakpoint';
    el.innerHTML = '<span class="bp-dot"></span><span>' + escapeHtml(stage.label) + '</span>';
    el.onclick = function () { this.classList.toggle('bp-on'); };
    pipelineWrap.appendChild(el);
  });
}

function createArrow() {
  const span = document.createElement('span');
  span.className = 'pipeline-arrow';
  span.textContent = '→';
  return span;
}

function getSelectedBreakSpaces() {
  const sel = [];
  const wrap = document.getElementById('monitor-space-breakpoints');
  if (wrap) wrap.querySelectorAll('.space-bp-item.bp-on').forEach((el) => {
    const id = el.getAttribute('data-space');
    if (id) sel.push(id);
  });
  return sel;
}

function getSelectedBreakStages() {
  const sel = [];
  document.querySelectorAll('.pipeline-stage.bp-on').forEach((el) => {
    const id = el.getAttribute('data-stage');
    if (id) sel.push(id);
  });
  return sel;
}

function loadBreakpointTypes() {
  const container = document.getElementById('breakpoint-types-container');
  if (!container) return;
  api.fetchBreakpointTypes(api.getCurrentTaskId()).then((d) => {
    const known = d.known || [];
    const enabledList = d.enabled || [];
    const enabled = enabledList.reduce((o, k) => {
      o[k] = true;
      return o;
    }, {});
    const allocStepCb = document.getElementById('opt-alloc-step');
    if (allocStepCb) allocStepCb.checked = enabledList.includes('alloc');
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
        api.postBreakpointTypes(list, api.getCurrentTaskId()).catch(() => {});
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
    const st = document.getElementById('control-status');
    const verboseCb = document.getElementById('opt-verbose');
    const verboseMon = document.getElementById('opt-verbose-monitor');
    api.setCurrentTaskIdFromState(d);
    const taskId = api.getCurrentTaskId();
    const tasks = d.tasks || [];
    const selectedTask = taskId ? tasks.find((t) => t.id === taskId) : null;
    const taskStateFromTask = selectedTask ? selectedTask.taskState : null;
    const taskState = taskStateFromTask != null ? taskStateFromTask : (d.taskState || 'idle');

    function applyTaskSettings(settings) {
      if (!settings) return;
      if (settings.memoryMonitorRunning !== undefined) memoryMonitorRunning = !!settings.memoryMonitorRunning;
      if (settings.verbose !== undefined && (verboseCb || verboseMon)) {
        const v = !!settings.verbose;
        if (verboseCb) verboseCb.checked = v;
        if (verboseMon) verboseMon.checked = v;
      }
      const allocHint = document.getElementById('alloc-breakpoint-hint');
      if (allocHint && settings.allocBreakpointsAvailable !== undefined) {
        if (settings.allocBreakpointsAvailable === false) {
          allocHint.textContent = '(requires Debug build)';
          allocHint.style.display = 'inline';
        } else {
          allocHint.style.display = 'none';
        }
      }
    }

    function renderStatusBar() {
      if (!st) return;
      if (!d.serverRunning) {
        st.innerHTML = '<span class="err">API: not running</span> – start camel-db and run <code>serve</code>';
        return;
      }
      const taskStateLabels = { idle: 'No task', loaded: 'Loaded', running: 'Running', paused: 'Paused', completed: 'Completed', terminated: 'Terminated', exited: 'Exited' };
      st.innerHTML = '<span class="ok">API: connected</span>';
      st.innerHTML += ' · Task: <span class="task-state-inline">' + escapeHtml(taskStateLabels[taskState] || taskState) + '</span>';
      if (d.hasFile && d.targetFile) {
        lastTargetFile = d.targetFile;
        st.innerHTML += ' · <code>' + escapeHtml((d.targetFile.split(/[/\\]/).pop()) || d.targetFile) + '</code>';
        const inp = document.getElementById('input-file-path');
        if (inp && inp.value !== d.targetFile) inp.value = d.targetFile;
        addOpenScript(d.targetFile);
      } else {
        lastTargetFile = '';
      }
      if (memoryMonitorRunning) st.innerHTML += ' · Memory scan on';
    }

    if (taskId) {
      // 获取任务级设置（verbose、memory monitor 等）
      api.fetchSettings(taskId).then((settings) => {
        applyTaskSettings(settings);
        renderStatusBar();
      }).catch(() => {
        if (d.verbose !== undefined && (verboseCb || verboseMon)) {
          if (verboseCb) verboseCb.checked = !!d.verbose;
          if (verboseMon) verboseMon.checked = !!d.verbose;
        }
        renderStatusBar();
      });
      // 获取任务级 assertion error（从子进程的 /api/state）
      if (taskState !== 'exited') {
        api.fetchState(taskId).then((taskData) => {
          updateAssertionOverlay(taskData && taskData.assertionError ? taskData : null);
        }).catch(() => {
          updateAssertionOverlay(null);
        });
      } else {
        updateAssertionOverlay(null);
      }
    } else {
      // 无任务时，assertion error 直接从全局 state 获取
      updateAssertionOverlay(d.assertionError ? d : null);
      if (d.memoryMonitorRunning !== undefined) memoryMonitorRunning = !!d.memoryMonitorRunning;
      if (d.verbose !== undefined && (verboseCb || verboseMon)) {
        if (verboseCb) verboseCb.checked = !!d.verbose;
        if (verboseMon) verboseMon.checked = !!d.verbose;
      }
      const allocHint = document.getElementById('alloc-breakpoint-hint');
      if (allocHint && d.allocBreakpointsAvailable === false) {
        allocHint.textContent = '(requires Debug build)';
        allocHint.style.display = 'inline';
      } else if (allocHint) {
        allocHint.style.display = 'none';
      }
      renderStatusBar();
    }
    renderSidebarTasks(tasks);
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
    const allocStepEl = document.getElementById('opt-alloc-step');
    const allocStep = allocStepEl ? allocStepEl.checked : false;
    const breakSpaces = getSelectedBreakSpaces();
    const breakStages = getSelectedBreakStages();
    const statusEl = document.getElementById('control-status');
    const btn = this;
    btn.disabled = true;
    location.hash = path ? '#/script/' + encodeURIComponent(path) : '#/monitor';
    const runOpts = { memoryMonitor, allocStep, breakSpaces, breakStages };
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
          return api.postRun(runOpts);
        })
        .then((d) => {
          if (d && !d.ok) done(false, d.error);
          else {
            done(true);
            setTimeout(updateControlStatus, 400);
          }
        })
        .catch((e) => done(false, e.message || e));
    } else {
      api
        .postRun(runOpts)
        .then((d) => {
          done(d.ok, d.ok ? null : d.error || 'Run failed');
          if (d && d.ok) {
            setTimeout(updateControlStatus, 400);
          }
        })
        .catch((e) => done(false, e.message || e));
    }
  };

  function setVerboseFromCheckbox(checked) {
    api.postSettings({ verbose: checked }, api.getCurrentTaskId()).then((d) => {
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

  const allocStepEl = document.getElementById('opt-alloc-step');
  if (allocStepEl) {
    allocStepEl.onchange = function () {
      const wantAlloc = !!this.checked;
      api.fetchBreakpointTypes(api.getCurrentTaskId()).then((bt) => {
        let enabled = Array.isArray(bt.enabled) ? [...bt.enabled] : [];
        const hasAlloc = enabled.includes('alloc');
        if (wantAlloc && !hasAlloc) enabled.push('alloc');
        else if (!wantAlloc && hasAlloc) enabled = enabled.filter((x) => x !== 'alloc');
        return api.postBreakpointTypes(enabled, api.getCurrentTaskId());
      }).then(() => updateControlStatus()).catch(() => updateControlStatus());
    };
  }
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
  // Register status poll first so init errors do not stop polling
  setInterval(updateControlStatus, 2000);
  updateControlStatus();

  loadOpenScripts();
  renderSidebarScripts();
  initSidebar();
  initPipelineAndSpaceBreakpoints();
  const assertionRestartBtn = document.getElementById('assertion-overlay-restart');
  if (assertionRestartBtn) {
    assertionRestartBtn.onclick = function () {
      api.postRestart(api.getCurrentTaskId()).then(() => updateControlStatus()).catch(() => updateControlStatus());
    };
  }
  window.addEventListener('hashchange', applyRoute);
  monitor.initMonitor();
  graph.initGraph();
  initRunPage();
  applyRoute();
  setInterval(() => {
    if (memoryMonitorRunning && getRoute() !== '/') monitor.fetchData(false);
  }, 1500);
  setInterval(monitor.pollStepStatus, 1000);
  setInterval(monitor.pollLog, 800);
}

main();
