/**
 * Memory monitor page: snapshot list, region hex view, object list, refresh/continue/restart.
 */

import * as api from './api.js';
import * as hex from './hex.js';

const PAGE_SIZE_OBJ = 50;
const HEX_LINE_HEIGHT_EM = 1.5;

const SCALE_LEVELS = [1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 0];
const SCALE_LABELS = {
  1024: '1 KB',
  4096: '4 KB',
  16384: '16 KB',
  65536: '64 KB',
  262144: '256 KB',
  1048576: '1 MB',
  4194304: '4 MB',
  16777216: '16 MB',
  0: 'Actual',
};

/** Internal region name -> human-readable display name */
const REGION_DISPLAY_NAMES = {
  birthSpace: 'Birth Space (Eden)',
  havenSpace: 'Haven Space (To)',
  cacheSpace: 'Cache Space (From)',
  elderGenSpace: 'Elder Generation Space',
  metaSpace: 'Meta Space',
  permSpace: 'Permanent Space',
  largeObj: 'Large Object Space',
};
function getRegionDisplayName(internalName) {
  return REGION_DISPLAY_NAMES[internalName] ?? internalName;
}

let lastSnapshot = null;
/** @type {Object<string, number>} per-region bar scale (bytes); 0 = actual capacity */
let regionScales = {};
let currentRegionName = '';
let hexTotal = 0;
let hexCurrentPage = 0;
let hexLinesPerPage = 32;
let hexTotalPages = 1;
let objNextOffset = 0;
let objTotal = 0;
let objRegionStart = 0;
let logNextOffset = 0;
/** When set, hex view will highlight this byte range (region-relative). Cleared after render. */
let hexHighlightRange = null;

export function getLastSnapshot() {
  return lastSnapshot;
}

export function fmtBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(2) + ' KB';
  return (n / (1024 * 1024)).toFixed(2) + ' MB';
}

/** Short form for tight space: 0B, 4MB, 32MB (no space, integer when possible). */
function fmtBytesShort(n) {
  if (n < 1024) return n + 'B';
  if (n < 1024 * 1024) {
    const kb = n / 1024;
    return kb % 1 === 0 ? kb + 'KB' : kb.toFixed(1) + 'KB';
  }
  const mb = n / (1024 * 1024);
  return mb % 1 === 0 ? mb + 'MB' : mb.toFixed(1) + 'MB';
}

function getScaleForRegion(name) {
  if (regionScales[name] !== undefined) return regionScales[name];
  return SCALE_LEVELS[0];
}

/** When used >= 80% of scale, advance to next level for next render. */
function advanceScaleIfNeeded(used, capacity, scale) {
  if (scale === 0) return 0;
  if (capacity === 0) return scale;
  if (used >= 0.8 * scale) {
    const i = SCALE_LEVELS.indexOf(scale);
    if (i >= 0 && i < SCALE_LEVELS.length - 1) return SCALE_LEVELS[i + 1];
  }
  return scale;
}

function buildScaleOptionsHtml(selectedVal) {
  return SCALE_LEVELS.map(
    (v) =>
      '<option value="' + v + '"' + (v === selectedVal ? ' selected' : '') + '>' + (SCALE_LABELS[v] || String(v)) + '</option>'
  ).join('');
}

function buildRegionCard(r, scaleVal) {
  const cap = r.capacity || 1;
  const used = r.used || 0;
  scaleVal = advanceScaleIfNeeded(used, cap, scaleVal);
  regionScales[r.name] = scaleVal;
  const scale = scaleVal === 0 ? cap : scaleVal;
  const pct = scale > 0 ? Math.min(100, (used / scale) * 100) : 0;
  let card = '<div class="region" data-region-name="' + r.name + '">';
  card += '<div class="region-header" data-region-name="' + r.name + '">';
  card += '<div class="region-header-left">';
  const displayName = getRegionDisplayName(r.name);
  card += '<div class="region-name" title="' + displayName.replace(/"/g, '&quot;') + '">' + displayName + '</div>';
  const statsFull = fmtBytes(used) + ' / ' + fmtBytes(cap) + ' (' + (r.objectCount || 0) + ' objects)';
  card += '<div class="region-stats" title="' + statsFull.replace(/"/g, '&quot;') + '">' + fmtBytesShort(used) + '/' + fmtBytesShort(cap) + '</div>';
  card += '</div>';
  card += '<select class="region-scale" data-region-name="' + r.name + '" title="Bar scale">' + buildScaleOptionsHtml(scaleVal) + '</select>';
  card += '</div>';
  if (cap > 0) {
    card += '<div class="region-viz">';
    card += '<div class="used" style="width:' + pct + '%"></div>';
    card += '<div class="free" style="width:' + (100 - pct) + '%"></div>';
    card += '</div>';
  }
  card += '</div>';
  return card;
}

function applyHexPageHeight() {
  const wrap = document.getElementById('hex-view-wrap');
  if (wrap) wrap.style.height = hexLinesPerPage * HEX_LINE_HEIGHT_EM + 'em';
}

export function render(data) {
  const div = document.getElementById('content');
  if (!div) return;
  if (!data.regions) {
    div.innerHTML = '<div class="error">No data</div>';
    return;
  }
  lastSnapshot = data;
  const byName = {};
  data.regions.forEach((r) => {
    byName[r.name] = r;
  });
  const birth = byName.birthSpace;
  const haven = byName.havenSpace;
  const cache = byName.cacheSpace;
  const elder = byName.elderGenSpace;
  const meta = byName.metaSpace;
  const perm = byName.permSpace;
  const largeObj = byName.largeObj;

  let totalUsed = 0,
    totalCap = 0;
  data.regions.forEach((r) => {
    totalUsed += r.used || 0;
    totalCap += r.capacity || 0;
  });

  let html = '<div class="summary">';
  html += '<div class="card"><h3>Regions</h3><div class="val">' + data.regions.length + '</div></div>';
  html += '<div class="card"><h3>Used / Total</h3><div class="val">' + fmtBytes(totalUsed) + ' / ' + fmtBytes(totalCap) + '</div></div>';
  html += '</div><div class="regions">';

  if (birth || haven || cache) {
    html += '<div class="region-group region-group-young">';
    html += '<div class="region-group-title">Young Gen</div>';
    html += '<div class="region-rows">';
    if (birth) {
      html += '<div class="region-row">' + buildRegionCard(birth, getScaleForRegion(birth.name)) + '</div>';
    }
    if (haven || cache) {
      html += '<div class="region-row region-row-half">';
      if (haven) html += buildRegionCard(haven, getScaleForRegion(haven.name));
      if (cache) html += buildRegionCard(cache, getScaleForRegion(cache.name));
      html += '</div>';
    }
    html += '</div></div>';
  }

  [elder, meta, perm].forEach((r) => {
    if (!r) return;
    html += '<div class="region-row">' + buildRegionCard(r, getScaleForRegion(r.name)) + '</div>';
  });

  if (largeObj) {
    html += '<div class="region-row">' + buildRegionCard(largeObj, getScaleForRegion(largeObj.name)) + '</div>';
  }

  html += '</div>';
  div.innerHTML = html;
}

function selectRegion(name) {
  currentRegionName = name;
  const panelName = document.getElementById('panel-region-name');
  const btnRefresh = document.getElementById('btn-refresh');
  const hexContent = document.getElementById('hex-content');
  const hexPagination = document.getElementById('hex-pagination');
  const objTbody = document.getElementById('obj-tbody');
  const objLoadMore = document.getElementById('obj-load-more');
  if (panelName) panelName.textContent = getRegionDisplayName(name);
  if (btnRefresh) btnRefresh.style.display = 'inline-block';
  document.querySelectorAll('.view-tab').forEach((t) => {
    t.classList.toggle('active', t.getAttribute('data-view') === 'hex');
  });
  document.getElementById('view-hex').style.display = 'flex';
  document.getElementById('view-objects').style.display = 'none';
  hexCurrentPage = 0;
  objNextOffset = 0;
  if (hexPagination) hexPagination.style.display = 'none';
  loadHexPage();
  if (objTbody) objTbody.innerHTML = '';
  if (objLoadMore) objLoadMore.style.display = 'none';
}

function loadHexPage() {
  if (!currentRegionName) return Promise.resolve();
  const offsetBytes = hexCurrentPage * hexLinesPerPage * 16;
  const limitBytes = hexLinesPerPage * 16;
  const container = document.getElementById('hex-content');
  const hexPagination = document.getElementById('hex-pagination');
  const panelLoader = document.getElementById('panel-loader');
  const prevLines = hex.getPrevLinesFromContainer(container);
  if (panelLoader) panelLoader.classList.add('visible');
  hexPagination.style.display = 'none';

  return api.fetchRegionMemory(currentRegionName, offsetBytes, limitBytes).then((d) => {
    if (panelLoader) panelLoader.classList.remove('visible');
    const offsetEl = document.getElementById('hex-page-offset');
    if (d.error) {
      if (offsetEl) offsetEl.classList.remove('visible');
      container.innerHTML = '<div class="error">' + d.error + '</div>';
      hexPagination.style.display = 'none';
      return;
    }
    hexTotal = d.capacity != null ? d.capacity : d.total || 0;
    const totalLines = Math.ceil(hexTotal / 16) || 1;
    hexTotalPages = Math.max(1, Math.ceil(totalLines / hexLinesPerPage));
    applyHexPageHeight();

    const pageStartOffset = d.offset != null ? d.offset : offsetBytes;
    const region = lastSnapshot && lastSnapshot.regions && lastSnapshot.regions.find((r) => r.name === currentRegionName);
    const regionStart = region && region.start != null ? Number(region.start) : 0;
    const pageStartAddress = regionStart + pageStartOffset;
    const hexStr = pageStartAddress.toString(16).toUpperCase().padStart(8, '0');
    const offsetHexStr = pageStartOffset.toString(16).toUpperCase().padStart(8, '0');
    if (offsetEl) {
      offsetEl.title = 'Page start address: 0x' + hexStr + ' (region offset: 0x' + offsetHexStr + ')';
      offsetEl.textContent = '0x' + hexStr;
      offsetEl.classList.add('visible');
    }
    hex.renderHexView(container, d, prevLines, {
      linesPerPage: hexLinesPerPage,
      diffFadeMs: 2000,
      highlightRange: hexHighlightRange,
    });
    if (hexHighlightRange) {
      hexHighlightRange = null;
      const wrap = document.getElementById('hex-view-wrap');
      if (wrap) {
        setTimeout(() => {
          wrap.querySelectorAll('.hex-line-highlight').forEach((el) => el.classList.remove('hex-line-highlight'));
        }, 4000);
      }
    }
    hexPagination.style.display = 'flex';
    document.getElementById('hex-page-info').textContent =
      'Page ' + (hexCurrentPage + 1) + ' / ' + hexTotalPages;
    document.getElementById('hex-prev-btn').disabled = hexCurrentPage <= 0;
    document.getElementById('hex-next-btn').disabled = hexCurrentPage >= hexTotalPages - 1;
  }).catch((e) => {
    if (panelLoader) panelLoader.classList.remove('visible');
    const offsetEl = document.getElementById('hex-page-offset');
    if (offsetEl) offsetEl.classList.remove('visible');
    container.innerHTML = '<div class="error">Failed to load: ' + e + '</div>';
    hexPagination.style.display = 'none';
  });
}

function loadObjPage() {
  if (!currentRegionName) return Promise.resolve();
  const tbody = document.getElementById('obj-tbody');
  const panelLoader = document.getElementById('panel-loader');
  const isFirst = objNextOffset === 0;
  if (isFirst && panelLoader) panelLoader.classList.add('visible');
  return api
    .fetchRegionObjects(currentRegionName, objNextOffset, PAGE_SIZE_OBJ)
    .then((d) => {
      if (isFirst && panelLoader) panelLoader.classList.remove('visible');
      if (isFirst) tbody.innerHTML = '';
      if (d.error) {
        tbody.innerHTML = '<tr><td colspan="4" class="error">' + d.error + '</td></tr>';
        return;
      }
      objRegionStart = Number(d.regionStart) || 0;
      objTotal = d.total || 0;
      const objs = d.objects || [];
      objs.forEach((o) => {
        const addr = Number(o.addr) || 0;
        const size = Number(o.size) || 0;
        const offsetInRegion = addr - objRegionStart;
        const tr = document.createElement('tr');
        tr.className = 'obj-row-clickable';
        tr.dataset.objAddr = String(addr);
        tr.dataset.objSize = String(size);
        tr.dataset.objOffset = String(offsetInRegion);
        tr.innerHTML =
          '<td class="col-addr">0x' + addr.toString(16) + '</td>' +
          '<td class="col-offset">0x' + (offsetInRegion >= 0 ? offsetInRegion : 0).toString(16) + '</td>' +
          '<td class="col-size">' + size + '</td>' +
          '<td class="col-age">' + (o.age ?? '') + '</td>';
        tbody.appendChild(tr);
      });
      objNextOffset += objs.length;
      const loadMoreEl = document.getElementById('obj-load-more');
      loadMoreEl.style.display = d.hasMore ? 'block' : 'none';
    })
    .catch((e) => {
      if (isFirst && panelLoader) panelLoader.classList.remove('visible');
      tbody.innerHTML = '<tr><td colspan="4" class="error">Failed to load: ' + e + '</td></tr>';
    });
}

function jumpHexToObject(offsetInRegion, size) {
  if (!currentRegionName) return;
  const bytesPerPage = hexLinesPerPage * 16;
  const start = Math.max(0, offsetInRegion);
  const pageIndex = Math.floor(start / bytesPerPage);
  hexCurrentPage = Math.max(0, pageIndex);
  hexHighlightRange = { start, end: start + size };
  document.querySelectorAll('.view-tab').forEach((t) => {
    t.classList.toggle('active', t.getAttribute('data-view') === 'hex');
  });
  document.getElementById('view-hex').style.display = 'flex';
  document.getElementById('view-objects').style.display = 'none';
  loadHexPage();
}

export function fetchData(showSpinner = true) {
  const content = document.getElementById('content');
  const contentLoader = document.getElementById('content-loader');
  if (showSpinner !== false && contentLoader) contentLoader.classList.add('visible');
  return api
    .fetchSnapshot()
    .then((d) => {
      if (contentLoader) contentLoader.classList.remove('visible');
      render(d);
      return d;
    })
    .catch((e) => {
      if (contentLoader) contentLoader.classList.remove('visible');
      if (content) content.innerHTML = '<div class="error">Failed to fetch data: ' + e + '</div>';
      throw e;
    });
}

export function refreshMonitorViews() {
  const promises = [fetchData()];
  if (currentRegionName) {
    promises.push(loadHexPage());
    const objPane = document.getElementById('view-objects');
    if (objPane && objPane.style.display !== 'none') {
      objNextOffset = 0;
      document.getElementById('obj-tbody').innerHTML = '';
      promises.push(loadObjPage());
    }
  }
  const lastAllocPromise = api.fetchLastAlloc().then((d) => {
    const la = document.getElementById('last-alloc');
    if (la) la.textContent = formatLastAlloc(d);
  }).catch(() => {});
  promises.push(lastAllocPromise);
  return Promise.all(promises).catch(() => {});
}

export function initMonitor() {
  const content = document.getElementById('content');
  if (content) {
    content.addEventListener('click', (e) => {
      const h = e.target.closest('.region-header[data-region-name]');
      if (h && !e.target.classList.contains('region-scale')) selectRegion(h.getAttribute('data-region-name'));
    });
    content.addEventListener('change', (e) => {
      if (e.target.classList.contains('region-scale')) {
        const name = e.target.getAttribute('data-region-name');
        const val = parseInt(e.target.value, 10);
        if (name != null && !isNaN(val)) {
          regionScales[name] = val;
          if (lastSnapshot) render(lastSnapshot);
        }
      }
    });
  }

  document.querySelectorAll('.view-tab').forEach((tab) => {
    tab.onclick = function () {
      document.querySelectorAll('.view-tab').forEach((t) => t.classList.remove('active'));
      this.classList.add('active');
      const offsetEl = document.getElementById('hex-page-offset');
      if (this.getAttribute('data-view') === 'hex') {
        document.getElementById('view-hex').style.display = 'flex';
        document.getElementById('view-objects').style.display = 'none';
        if (offsetEl && offsetEl.textContent) offsetEl.classList.add('visible');
      } else {
        document.getElementById('view-hex').style.display = 'none';
        document.getElementById('view-objects').style.display = 'block';
        if (offsetEl) offsetEl.classList.remove('visible');
        if (document.getElementById('obj-tbody').children.length === 0) loadObjPage();
      }
    };
  });

  const hexLinesEl = document.getElementById('hex-lines-per-page');
  if (hexLinesEl) {
    hexLinesEl.onchange = function () {
      hexLinesPerPage = parseInt(this.value, 10);
      hexCurrentPage = 0;
      loadHexPage();
    };
  }
  document.getElementById('hex-prev-btn').onclick = function () {
    if (hexCurrentPage > 0) {
      hexCurrentPage--;
      loadHexPage();
    }
  };
  document.getElementById('hex-next-btn').onclick = function () {
    if (hexCurrentPage < hexTotalPages - 1) {
      hexCurrentPage++;
      loadHexPage();
    }
  };
  document.getElementById('obj-load-more-btn').onclick = loadObjPage;

  const objTbody = document.getElementById('obj-tbody');
  if (objTbody) {
    objTbody.addEventListener('click', (e) => {
      const row = e.target.closest('tr.obj-row-clickable');
      if (!row) return;
      const offset = Number(row.dataset.objOffset);
      const size = Number(row.dataset.objSize);
      if (!Number.isNaN(offset) && !Number.isNaN(size)) jumpHexToObject(offset, size);
    });
  }

  document.getElementById('btn-refresh').onclick = function () {
    refreshMonitorViews().catch(() => {});
  };

  document.getElementById('btn-continue').onclick = function () {
    this.disabled = true;
    document.getElementById('btn-restart').disabled = true;
    document.getElementById('step-status').textContent = '';
    api.postContinue().then(() => refreshMonitorViews()).catch(() => {});
  };

  let restarting = false;
  document.getElementById('btn-restart').onclick = function () {
    this.disabled = true;
    document.getElementById('btn-continue').disabled = true;
    restarting = true;
    document.getElementById('step-status').textContent = 'Restarting…';
    document.getElementById('last-alloc').textContent = '';
    api.postRestart().then(() => {
      refreshMonitorViews();
      document.getElementById('step-status').textContent = '';
      restarting = false;
    }).catch(() => {
      restarting = false;
      document.getElementById('step-status').textContent = '';
    });
  };

  const btnLogClear = document.getElementById('btn-log-clear');
  if (btnLogClear) {
    btnLogClear.onclick = function () {
      const el = document.getElementById('log-content');
      if (el) el.textContent = '';
    };
  }

  initLogSidebar();
}

const LOG_SIDEBAR_STORAGE_KEY = 'camel-db-log-sidebar';
const LOG_SIDEBAR_MIN = 200;
const LOG_SIDEBAR_MAX = 800;
const LOG_SIDEBAR_DEFAULT = 360;

function initLogSidebar() {
  const sidebar = document.getElementById('log-sidebar');
  const handle = document.getElementById('log-resize-handle');
  const toggleInner = document.getElementById('log-toggle-inner');
  if (!sidebar || !handle) return;

  let saved = { width: LOG_SIDEBAR_DEFAULT, collapsed: false };
  try {
    const raw = sessionStorage.getItem(LOG_SIDEBAR_STORAGE_KEY);
    if (raw) saved = { ...saved, ...JSON.parse(raw) };
  } catch (_) {}
  const clamp = (v) => Math.max(LOG_SIDEBAR_MIN, Math.min(LOG_SIDEBAR_MAX, v));

  sidebar.style.setProperty('--log-sidebar-width', saved.width + 'px');
  if (saved.collapsed) {
    sidebar.classList.add('collapsed');
    handle.classList.add('sidebar-collapsed');
  }

  function save() {
    try {
      sessionStorage.setItem(
        LOG_SIDEBAR_STORAGE_KEY,
        JSON.stringify({
          width: parseInt(sidebar.style.getPropertyValue('--log-sidebar-width'), 10) || LOG_SIDEBAR_DEFAULT,
          collapsed: sidebar.classList.contains('collapsed'),
        })
      );
    } catch (_) {}
  }

  function collapse() {
    sidebar.classList.add('collapsed');
    handle.classList.add('sidebar-collapsed');
    handle.setAttribute('title', 'Click to expand log');
    save();
  }
  function expand() {
    sidebar.classList.remove('collapsed');
    handle.classList.remove('sidebar-collapsed');
    handle.setAttribute('title', 'Drag to resize');
    save();
  }

  if (toggleInner) toggleInner.onclick = collapse;

  handle.addEventListener('click', (e) => {
    if (sidebar.classList.contains('collapsed')) {
      e.preventDefault();
      e.stopPropagation();
      expand();
    }
  });

  if (sidebar.classList.contains('collapsed')) handle.setAttribute('title', 'Click to expand log');
  else handle.setAttribute('title', 'Drag to resize');

  let dragging = false;
  handle.addEventListener('mousedown', (e) => {
    if (e.button !== 0) return;
    if (sidebar.classList.contains('collapsed')) return;
    e.preventDefault();
    e.stopPropagation();
    dragging = true;
    const startX = e.clientX;
    const startW = parseInt(sidebar.style.getPropertyValue('--log-sidebar-width'), 10) || LOG_SIDEBAR_DEFAULT;
    function onMove(ev) {
      if (!dragging) return;
      const delta = startX - ev.clientX;
      const newW = clamp(startW + delta);
      sidebar.style.setProperty('--log-sidebar-width', newW + 'px');
    }
    function onUp() {
      dragging = false;
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
      save();
    }
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  });
}

/** Poll /api/log and append new lines to #log-content. Call when on monitor page. */
export function pollLog() {
  api.fetchLog(logNextOffset).then((d) => {
    const lines = d.lines || [];
    logNextOffset = d.nextOffset != null ? d.nextOffset : logNextOffset + lines.length;
    const el = document.getElementById('log-content');
    if (!el || lines.length === 0) return;
    lines.forEach((line) => {
      el.appendChild(document.createTextNode(line + '\n'));
    });
    el.scrollTop = el.scrollHeight;
  }).catch(() => {});
}

function formatPauseReason(d) {
  if (!d || !d.phase) return d && d.ptr !== undefined ? 'Paused after alloc – Continue or Restart' : 'Paused – Continue or Restart';
  if (d.phase === 'before')
    return 'Paused before alloc (size ' + fmtBytes(d.size || 0) + ', ' + (d.space || '?') + ') – Continue or Restart';
  return 'Paused after alloc (0x' + (d.ptr != null ? d.ptr.toString(16) : '0') + ', ' + fmtBytes(d.size || 0) + ') – Continue or Restart';
}

function formatLastAlloc(d) {
  if (!d || d.size === undefined) return '';
  const space = d.space || '?';
  const sizeStr = fmtBytes(d.size);
  if (d.phase === 'before') return 'About to alloc: ' + space + ' ' + sizeStr;
  if (d.ptr !== undefined) return 'Last: ' + space + ' 0x' + d.ptr.toString(16) + ' ' + sizeStr;
  return 'Last: ' + space + ' ' + sizeStr;
}

export function pollStepStatus() {
  api.fetchStepPaused().then((d) => {
    const btn = document.getElementById('btn-continue');
    const st = document.getElementById('step-status');
    const restartBtn = document.getElementById('btn-restart');
    if (d.paused) {
      btn.disabled = false;
      restartBtn.disabled = false;
      st.textContent = formatPauseReason(d);
    } else {
      btn.disabled = true;
      restartBtn.disabled = true;
      st.textContent = '';
    }
  }).catch(() => {});
  api.fetchLastAlloc().then((d) => {
    const la = document.getElementById('last-alloc');
    if (la) la.textContent = formatLastAlloc(d);
  }).catch(() => {});
}
