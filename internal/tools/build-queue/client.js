// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

window.__ModuleLoader__.load({
  id: '@local/dsh-build-queue',
  factory(require) {
    const React = require('react');
    const h = React.createElement;

    /** Client locale namespace. */
    const NS = 'build-queue';
    /** Authenticated Host route that serves the normalized state file. */
    const STATE_ROUTE = 'api/build-queue.state';
    /** Authenticated Host route that serves the machine resource sample. */
    const RESOURCES_ROUTE = 'api/build-queue.resources';
    /** Poll period while the panel is mounted and the page is visible. */
    const POLL_MS = 1000;
    /** Marker used to install the component styles exactly once per page. */
    const STYLE_ID = '@local/dsh-build-queue/styles';
    /** Persisted collapsed state; the panel starts expanded on a first visit. */
    const COLLAPSED_KEY = 'dsh.buildQueue.collapsed';
    /** Persisted position and size; one record owns x, y, width and height. */
    const GEOM_KEY = 'dsh.buildQueue.geom';
    /** Session-only hidden flag; deliberately never restored on load. */
    const HIDDEN_KEY = 'dsh.buildQueue.hidden';
    /** Shortest timeline window, in milliseconds. */
    const MIN_SPAN = 30 * 60 * 1000;
    /** Right padding past "now" so the now line stays inside the plot. */
    const NOW_PAD = 2 * 60 * 1000;
    /** Rows rendered before the timeline scrolls. */
    const MAX_ROWS = 12;
    /** CPU samples kept for the sparkline. */
    const HISTORY = 60;
    /** Resize limits and the margin kept inside the viewport. */
    const MIN_WIDTH = 320;
    const MIN_HEIGHT = 240;
    const MARGIN = 12;
    /** Chip rect: fixed so the collapsed panel can never measure itself away. */
    const CHIP_WIDTH = 220;
    const CHIP_HEIGHT = 32;
    const CHIP_MIN_WIDTH = 180;
    const CHIP_MAX_WIDTH = 320;
    /** Hidden-state handle. */
    const TAB_HEIGHT = 44;
    /** Gap kept between the panel/chip/handle and the viewport edge. */
    const EDGE = 8;
    /** Pixels of pointer travel before a click becomes a drag. */
    const DRAG_THRESHOLD = 4;

    const zh = {
      'panel.title': '构建队列',
      'panel.idle': '空闲',
      'panel.offline': '连接失败',
      'panel.collapse': '收起构建队列',
      'panel.expand': '展开构建队列',
      'panel.hide': '暂时隐藏（Alt+Shift+B 恢复）',
      'panel.show': '显示构建队列',
      'panel.resize': '拖动调整大小',
      'current.title': '当前构建',
      'current.none': '当前没有构建',
      'current.elapsed': '已用 {time}',
      'current.waited': '排队 {time}',
      'current.progress': '{done}/{total} · {percent}%',
      'waiters.line': '等待中 {count}',
      'waiters.none': '无人等待',
      'timeline.title': '队列时间线',
      'timeline.empty': '还没有构建记录',
      'res.title': '资源',
      'res.memory': '内存',
      'row.build': '构建',
      'row.wait': '排队',
      'detail.running': '运行中',
      'detail.line': '排队 {queued} · 开始 {started} · 用时 {build} · exit {exit}',
      'detail.waiter': '{worktree} 排队 {wait}（尚未开始）',
      'detail.none': '最近一次构建：{worktree} · exit {exit}',
      'phase.starting': '启动中',
      'phase.compiling': '编译中',
      'phase.step': '执行步骤',
      'phase.done': '已完成',
      'phase.fail': '失败',
      'phase.check': '检查中',
      'phase.other': '进行中',
    };

    const en = {
      'panel.title': 'Build Queue',
      'panel.idle': 'Idle',
      'panel.offline': 'Disconnected',
      'panel.collapse': 'Collapse the build queue',
      'panel.expand': 'Expand the build queue',
      'panel.hide': 'Hide for now (Alt+Shift+B to restore)',
      'panel.show': 'Show the build queue',
      'panel.resize': 'Drag to resize',
      'current.title': 'Current build',
      'current.none': 'No build running',
      'current.elapsed': '{time} elapsed',
      'current.waited': 'queued {time}',
      'current.progress': '{done}/{total} · {percent}%',
      'waiters.line': 'Waiting {count}',
      'waiters.none': 'Nothing waiting',
      'timeline.title': 'Queue timeline',
      'timeline.empty': 'No builds recorded yet',
      'res.title': 'Resources',
      'res.memory': 'Memory',
      'row.build': 'build',
      'row.wait': 'wait',
      'detail.running': 'running',
      'detail.line': 'queued {queued} · started {started} · {build} · exit {exit}',
      'detail.waiter': '{worktree} waiting {wait} (not started)',
      'detail.none': 'Last build: {worktree} · exit {exit}',
      'phase.starting': 'starting',
      'phase.compiling': 'compiling',
      'phase.step': 'running step',
      'phase.done': 'done',
      'phase.fail': 'failed',
      'phase.check': 'checking',
      'phase.other': 'running',
    };

    const CSS = [
      /* Floating panel: position and size come from the persisted geometry. */
      '.bq_panel{position:fixed;z-index:60;box-sizing:border-box;display:flex;flex-direction:column;',
      'max-height:calc(100vh - 8px);border:1px solid var(--dsw-alias-border-l2);border-radius:var(--dsw-radius-lg);',
      'background:var(--dsw-specific-menu);backdrop-filter:var(--dsw-menu-backdrop-filter);',
      'box-shadow:var(--dsw-elevation-panel);color:var(--dsw-alias-label-primary);overflow:hidden;',
      'font-size:12px;line-height:18px;min-width:320px}',
      '.bq_panel[data-collapsed=true]{min-width:0;overflow:visible}',
      '.bq_head{display:flex;align-items:center;gap:8px;padding:8px 10px 8px 12px;min-height:28px;',
      'cursor:grab;user-select:none;touch-action:none;outline-offset:-2px}',
      '.bq_head:hover{background:var(--dsw-alias-interactive-bg-hover)}',
      '.bq_panel[data-interacting=drag] .bq_head{cursor:grabbing}',
      '.bq_chip{padding:0 6px 0 10px;justify-content:space-between;box-sizing:border-box;',
      'border-radius:inherit;background:var(--dsw-specific-menu);box-shadow:var(--dsw-elevation-panel)}',
      '.bq_dot{flex:none;width:8px;height:8px;border-radius:50%;background:var(--dsw-alias-state-idle-primary)}',
      '.bq_dot[data-state=busy]{background:var(--dsw-alias-state-business-primary)}',
      '.bq_dot[data-state=warn]{background:var(--dsw-alias-state-warn-primary)}',
      '.bq_dot[data-state=fail]{background:var(--dsw-alias-state-error-primary)}',
      '.bq_dot[data-state=done]{background:var(--dsw-alias-state-success-primary)}',
      '.bq_title{flex:none;font-size:13px;font-weight:500;color:var(--dsw-alias-label-primary)}',
      '.bq_headStatus{flex:1;min-width:0;color:var(--dsw-alias-label-tertiary);white-space:nowrap;',
      'text-overflow:ellipsis;overflow:hidden}',
      '.bq_badge{flex:none;padding:0 6px;border:1px solid var(--dsw-alias-border-l2);border-radius:999px;',
      'font-size:11px;line-height:16px;color:var(--dsw-alias-label-secondary)}',
      '.bq_ctl{flex:none;display:grid;place-items:center;width:20px;height:20px;border-radius:4px;',
      'color:var(--dsw-alias-label-tertiary);cursor:pointer}',
      '.bq_ctl:hover{background:var(--dsw-alias-interactive-bg-hover);color:var(--dsw-alias-label-secondary)}',
      '.bq_toggle{flex:none;display:grid;place-items:center;width:20px;height:20px;',
      'color:var(--dsw-alias-label-tertiary)}',
      '.bq_body{display:flex;flex-direction:column;gap:8px;padding:8px 12px 12px;overflow-y:auto;',
      'flex:1;min-height:0;border-top:1px solid var(--dsw-alias-border-l1)}',
      '.bq_block{display:flex;flex-direction:column;gap:4px;flex:none}',
      '.bq_blockGrow{flex:1;min-height:0}',
      '.bq_label{font-size:11px;line-height:16px;font-weight:500;color:var(--dsw-alias-label-tertiary)}',
      '.bq_rowLine{display:flex;align-items:center;gap:8px;min-width:0}',
      '.bq_worktree{flex:1;min-width:0;font-size:12px;font-weight:500;color:var(--dsw-alias-label-primary);',
      'white-space:nowrap;text-overflow:ellipsis;overflow:hidden}',
      '.bq_phase{flex:none;padding:0 6px;border:1px solid var(--dsw-alias-border-l2);border-radius:999px;',
      'font-size:11px;line-height:16px;color:var(--dsw-alias-label-secondary)}',
      '.bq_phase[data-phase=compiling],.bq_phase[data-phase=starting],.bq_phase[data-phase=check]{',
      'color:var(--dsw-alias-state-business-primary);border-color:var(--dsw-alias-state-business-primary)}',
      '.bq_phase[data-phase=fail]{color:var(--dsw-alias-state-error-primary);border-color:var(--dsw-alias-state-error-primary)}',
      '.bq_phase[data-phase=done]{color:var(--dsw-alias-state-success-primary);border-color:var(--dsw-alias-state-success-primary)}',
      '.bq_time{flex:none;margin-left:auto;font-size:12px;font-variant-numeric:tabular-nums;',
      'color:var(--dsw-alias-label-tertiary)}',
      '.bq_trackBar{height:4px;border-radius:2px;background:var(--dsw-alias-border-l1);overflow:hidden}',
      '.bq_fill{height:100%;border-radius:2px;background:var(--dsw-alias-state-business-primary);transition:width .3s ease}',
      '.bq_count{font-size:11px;line-height:16px;font-variant-numeric:tabular-nums;color:var(--dsw-alias-label-tertiary)}',
      '.bq_last{font-size:11px;line-height:16px;color:var(--dsw-alias-label-secondary);white-space:nowrap;',
      'text-overflow:ellipsis;overflow:hidden;font-family:var(--dsw-font-mono,ui-monospace,monospace)}',
      '.bq_muted{font-size:12px;color:var(--dsw-alias-label-tertiary)}',
      /* Resource strip. */
      '.bq_res{display:flex;flex-direction:column;gap:3px}',
      '.bq_resRow{display:flex;align-items:center;gap:8px;min-width:0}',
      '.bq_resLabel{flex:none;width:30px;font-size:11px;color:var(--dsw-alias-label-tertiary)}',
      '.bq_resMid{flex:1;min-width:0;text-align:right;font-size:10px;font-variant-numeric:tabular-nums;',
      'color:var(--dsw-alias-label-tertiary);white-space:nowrap;text-overflow:ellipsis;overflow:hidden}',
      '.bq_resValue{flex:none;width:36px;text-align:right;font-size:10px;font-variant-numeric:tabular-nums;',
      'color:var(--dsw-alias-label-secondary)}',
      '.bq_spark{flex:1;min-width:0;height:14px;display:block;overflow:visible}',
      '.bq_resBar{height:3px;border-radius:2px;background:var(--dsw-alias-border-l1);overflow:hidden}',
      '.bq_resBar>i{display:block;height:100%;border-radius:2px;background:var(--dsw-alias-state-business-primary);',
      'transition:width .3s ease}',
      /* Timeline. */
      '.bq_gantt{display:flex;flex-direction:column;gap:4px;flex:1;min-height:70px}',
      '.bq_axis{display:flex;align-items:flex-end;height:16px;flex:none}',
      '.bq_gutter{flex:none;width:68px}',
      '.bq_axisTrack{position:relative;flex:1;height:16px;min-width:0}',
      '.bq_tick{position:absolute;top:0;bottom:0;width:1px;background:var(--dsw-alias-border-l1);opacity:.7}',
      '.bq_tickLabel{position:absolute;bottom:0;font-size:10px;line-height:14px;white-space:nowrap;',
      'color:var(--dsw-alias-label-tertiary);font-variant-numeric:tabular-nums}',
      '.bq_rows{display:flex;flex-direction:column;gap:2px;flex:1;min-height:0;overflow:auto}',
      '.bq_trow{display:flex;align-items:center;min-width:214px;border-radius:4px}',
      '.bq_trow:hover{background:var(--dsw-alias-interactive-bg-hover)}',
      '.bq_rowName{flex:none;width:68px;padding-right:6px;box-sizing:border-box;font-size:11px;',
      'color:var(--dsw-alias-label-secondary);white-space:nowrap;text-overflow:ellipsis;overflow:hidden}',
      '.bq_track{position:relative;flex:1;height:14px;min-width:80px}',
      '.bq_seg{position:absolute;top:3px;height:8px;min-width:2px;border-radius:2px}',
      '.bq_segWait{background:var(--dsw-alias-border-l2)}',
      '.bq_segOk{background:var(--dsw-alias-state-success-primary)}',
      '.bq_segFail{background:var(--dsw-alias-state-error-primary)}',
      '.bq_segLive{background:var(--dsw-alias-state-business-primary)}',
      '.bq_segUnknown{background:var(--dsw-alias-label-tertiary)}',
      '.bq_segPulse{animation:bq_pulse 2s ease-in-out infinite}',
      '@keyframes bq_pulse{0%,100%{opacity:.45}50%{opacity:1}}',
      '@media (prefers-reduced-motion:reduce){.bq_segPulse{animation:none}}',
      '.bq_nowLine{position:absolute;top:0;bottom:0;width:1px;background:var(--dsw-alias-state-business-primary);opacity:.55}',
      '.bq_rowDur{flex:none;width:66px;padding-left:6px;box-sizing:border-box;text-align:right;',
      'font-size:10px;line-height:14px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden}',
      '.bq_durWait{color:var(--dsw-alias-label-tertiary)}',
      '.bq_durBuild{color:var(--dsw-alias-label-secondary)}',
      '.bq_detail{font-size:10px;line-height:14px;color:var(--dsw-alias-label-tertiary);flex:none;',
      'white-space:nowrap;text-overflow:ellipsis;overflow:hidden;min-height:14px}',
      '.bq_waiterLine{font-size:11px;line-height:16px;color:var(--dsw-alias-label-secondary);',
      'white-space:nowrap;text-overflow:ellipsis;overflow:hidden}',
      /* Resize grip. */
      '.bq_grip{position:absolute;right:0;bottom:0;width:16px;height:16px;display:grid;place-items:center;',
      'color:var(--dsw-alias-label-tertiary);opacity:.7;cursor:nwse-resize;touch-action:none;z-index:2}',
      '.bq_grip:hover{opacity:1}',
      /* Hidden-state handle: a thin tab on the right edge, always reachable. */
      '.bq_tab{position:fixed;right:0;z-index:60;width:6px;height:44px;padding:0;border:0;',
      'border-radius:6px 0 0 6px;background:var(--dsw-alias-state-business-primary);opacity:.5;',
      'cursor:pointer;transition:opacity .15s ease,width .15s ease}',
      '.bq_tab:hover,.bq_tab:focus-visible{opacity:1;width:10px;outline:none}',
    ].join('');

    /**
     * Install the component stylesheet once and remove it with the plugin.
     * @returns {() => void} cleanup removing the style element this call created.
     */
    function installStyles() {
      const existing = document.querySelector('style[data-plugin-css=' + JSON.stringify(STYLE_ID) + ']');
      if (existing !== null) return () => {};
      const tag = document.createElement('style');
      tag.dataset.plugin = '@local/dsh-build-queue';
      tag.dataset.pluginCss = STYLE_ID;
      tag.textContent = CSS;
      document.head.appendChild(tag);
      return () => {
        tag.remove();
      };
    }

    /**
     * @param {string} value - ISO timestamp.
     * @returns {number | null} epoch milliseconds, or null when unparseable.
     */
    function parseTime(value) {
      if (typeof value !== 'string' || value.length === 0) return null;
      const time = Date.parse(value);
      return Number.isFinite(time) ? time : null;
    }

    /**
     * @param {number} ms - duration in milliseconds.
     * @returns {string} `m:ss` or `h:mm:ss`.
     */
    function formatDuration(ms) {
      const total = Math.max(0, Math.floor((Number.isFinite(ms) ? ms : 0) / 1000));
      const hours = Math.floor(total / 3600);
      const minutes = Math.floor((total % 3600) / 60);
      const seconds = String(total % 60).padStart(2, '0');
      return hours > 0 ? hours + ':' + String(minutes).padStart(2, '0') + ':' + seconds : minutes + ':' + seconds;
    }

    /**
     * @param {number} time - epoch milliseconds.
     * @param {boolean} withSeconds - include seconds.
     * @returns {string} local `HH:mm` or `HH:mm:ss`.
     */
    function formatClock(time, withSeconds) {
      const date = new Date(time);
      const base = String(date.getHours()).padStart(2, '0') + ':' + String(date.getMinutes()).padStart(2, '0');
      return withSeconds ? base + ':' + String(date.getSeconds()).padStart(2, '0') : base;
    }

    /**
     * @param {number} bytes - byte count.
     * @returns {string} human-readable size.
     */
    function formatBytes(bytes) {
      if (typeof bytes !== 'number' || !Number.isFinite(bytes) || bytes < 0) return '—';
      const gib = bytes / (1024 * 1024 * 1024);
      if (gib >= 1) return gib.toFixed(1) + ' GB';
      return Math.round(bytes / (1024 * 1024)) + ' MB';
    }

    /**
     * @param {object} t - bound translate function.
     * @param {string} phase - published build phase.
     * @returns {string} localized phase label.
     */
    function phaseLabel(t, phase) {
      const known = ['starting', 'compiling', 'step', 'done', 'fail', 'check'];
      return t(known.indexOf(phase) >= 0 ? 'phase.' + phase : 'phase.other');
    }

    /**
     * @param {string} phase - published build phase.
     * @returns {string} dot state keyword.
     */
    function phaseDot(phase) {
      if (phase === 'fail') return 'fail';
      if (phase === 'done') return 'done';
      if (phase === 'step') return 'warn';
      return 'busy';
    }

    /**
     * Read the persisted collapsed flag.
     * @returns {boolean} true when the panel was left collapsed.
     */
    function readCollapsed() {
      try {
        return window.localStorage.getItem(COLLAPSED_KEY) === '1';
      } catch {
        return false;
      }
    }

    /**
     * @param {boolean} collapsed - next collapsed state.
     */
    function writeCollapsed(collapsed) {
      try {
        window.localStorage.setItem(COLLAPSED_KEY, collapsed ? '1' : '0');
      } catch {
        /* private mode or blocked storage: the panel simply forgets the choice */
      }
    }

    /**
     * Record the session-only hidden flag. It is never read back on startup, so a
     * reload always brings the panel back; it only documents the current tab state.
     * @param {boolean} hidden - whether the panel is hidden.
     */
    function writeHidden(hidden) {
      try {
        if (hidden) window.sessionStorage.setItem(HIDDEN_KEY, '1');
        else window.sessionStorage.removeItem(HIDDEN_KEY);
      } catch {
        /* storage unavailable: hiding still works for this page */
      }
    }

    /**
     * @param {number} value - candidate.
     * @param {number} min - allowed minimum.
     * @param {number} max - allowed maximum.
     * @returns {number} clamped value.
     */
    function clampNumber(value, min, max) {
      return Math.min(Math.max(value, min), max);
    }

    /**
     * Keep a geometry record inside the viewport and above the minimum size.
     * @param {object} geom - candidate geometry.
     * @param {number} vw - viewport width.
     * @param {number} vh - viewport height.
     * @returns {{ x: number, y: number, w: number, h: number | null }} safe geometry.
     */
    function clampGeom(geom, vw, vh) {
      const maxWidth = Math.max(MIN_WIDTH, vw - 2 * MARGIN);
      const maxHeight = Math.max(MIN_HEIGHT, vh - 2 * MARGIN);
      const w = clampNumber(Number.isFinite(geom.w) ? geom.w : 400, MIN_WIDTH, maxWidth);
      const h = geom.h === null || !Number.isFinite(geom.h) ? null : clampNumber(geom.h, MIN_HEIGHT, maxHeight);
      const x = clampNumber(Number.isFinite(geom.x) ? geom.x : vw - w - MARGIN, 4, Math.max(4, vw - w - 4));
      const y = clampNumber(Number.isFinite(geom.y) ? geom.y : MARGIN, 4, Math.max(4, vh - 40));
      return { x, y, w, h };
    }

    /**
     * Default geometry: top-right, 400 px wide, content height.
     * @param {number} vw - viewport width.
     * @param {number} vh - viewport height.
     * @returns {{ x: number, y: number, w: number, h: null }} geometry.
     */
    function defaultGeom(vw, vh) {
      const w = Math.min(400, Math.max(MIN_WIDTH, vw - 2 * MARGIN));
      return clampGeom({ x: vw - w - MARGIN, y: MARGIN, w, h: null }, vw, vh);
    }

    /**
     * Restore the persisted geometry, falling back to the top-right corner.
     * @returns {{ x: number, y: number, w: number, h: number | null }} geometry.
     */
    function readGeom() {
      const vw = window.innerWidth;
      const vh = window.innerHeight;
      let parsed = null;
      try {
        parsed = JSON.parse(window.localStorage.getItem(GEOM_KEY) ?? 'null');
      } catch {
        parsed = null;
      }
      if (typeof parsed !== 'object' || parsed === null) return defaultGeom(vw, vh);
      return clampGeom({
        x: parsed.x,
        y: parsed.y,
        w: parsed.w,
        h: typeof parsed.h === 'number' ? parsed.h : null,
      }, vw, vh);
    }

    /**
     * @param {object} geom - geometry to persist.
     */
    function writeGeom(geom) {
      try {
        window.localStorage.setItem(GEOM_KEY, JSON.stringify(geom));
      } catch {
        /* storage unavailable: geometry lasts for this page only */
      }
    }

    /**
     * Rect of the collapsed chip. It is sized by itself (never by geom.w/h) and
     * anchored to the panel's top-right corner, the spot the collapse control
     * occupied, so collapsing never moves it somewhere new.
     * @param {object} geom - stored panel geometry.
     * @param {number} vw - viewport width.
     * @param {number} vh - viewport height.
     * @returns {{ x: number, y: number, w: number, h: number }} chip rect.
     */
    function chipRect(geom, vw, vh) {
      const w = clampNumber(Math.min(CHIP_WIDTH, vw - 2 * EDGE), CHIP_MIN_WIDTH, CHIP_MAX_WIDTH);
      const right = Number.isFinite(geom.x) && Number.isFinite(geom.w) ? geom.x + geom.w : vw - MARGIN;
      const x = clampNumber(right - w, EDGE, Math.max(EDGE, vw - w - EDGE));
      const y = clampNumber(Number.isFinite(geom.y) ? geom.y : MARGIN, EDGE, Math.max(EDGE, vh - CHIP_HEIGHT - EDGE));
      return { x, y, w, h: CHIP_HEIGHT };
    }

    /**
     * Vertical position of the hidden-state handle: the panel's own height, kept
     * fully inside the viewport.
     * @param {object} geom - stored panel geometry.
     * @param {number} vh - viewport height.
     * @returns {number} top offset in pixels.
     */
    function tabTop(geom, vh) {
      const y = Number.isFinite(geom.y) ? geom.y : MARGIN;
      return clampNumber(y + 10, EDGE, Math.max(EDGE, vh - TAB_HEIGHT - EDGE));
    }

    /**
     * Poll both Host routes while the page is visible and the panel is not hidden;
     * stop entirely on unmount, when the tab is hidden, or when the panel is hidden.
     * @param {boolean} active - whether the panel currently needs data.
     * @returns {{ body: object | null, failed: boolean, resources: object | null }} samples.
     */
    function useBuildQueue(active) {
      const [visible, setVisible] = React.useState(() => document.visibilityState !== 'hidden');
      const [body, setBody] = React.useState(null);
      const [failed, setFailed] = React.useState(false);
      const [resources, setResources] = React.useState(null);

      React.useEffect(() => {
        const onChange = () => {
          setVisible(document.visibilityState !== 'hidden');
        };
        document.addEventListener('visibilitychange', onChange);
        return () => {
          document.removeEventListener('visibilitychange', onChange);
        };
      }, []);

      React.useEffect(() => {
        if (!visible || !active) return undefined;
        let live = true;
        let timer = 0;
        let controller = null;
        const readJson = async (route, signal) => {
          const response = await fetch(route, { cache: 'no-store', credentials: 'same-origin', signal });
          if (!response.ok) throw new Error('HTTP ' + String(response.status));
          return response.json();
        };
        const tick = async () => {
          controller?.abort();
          controller = new AbortController();
          const signal = controller.signal;
          const [stateResult, resourceResult] = await Promise.allSettled([
            readJson(STATE_ROUTE, signal),
            readJson(RESOURCES_ROUTE, signal),
          ]);
          if (!live) return;
          if (stateResult.status === 'fulfilled') {
            setBody(stateResult.value);
            setFailed(false);
          } else if (stateResult.reason?.name !== 'AbortError') {
            setFailed(true);
          }
          if (resourceResult.status === 'fulfilled') setResources(resourceResult.value);
          timer = setTimeout(tick, POLL_MS);
        };
        tick();
        return () => {
          live = false;
          clearTimeout(timer);
          controller?.abort();
        };
      }, [visible, active]);

      return { body, failed, resources };
    }

    /**
     * Turn the published state into timeline events.
     * @param {object | null} state - normalized state from the Host.
     * @param {number} now - Host clock in epoch milliseconds.
     * @returns {Array<object>} one event per build or waiter, oldest first.
     */
    function buildEvents(state, now) {
      const events = [];
      const holder = state?.holder ?? null;
      if (holder !== null) {
        events.push({
          worktree: holder.worktree,
          queuedAt: parseTime(holder.queuedAt),
          startedAt: parseTime(holder.startedAt),
          finishedAt: null,
          exit: null,
          live: true,
          kind: 'holder',
        });
      }
      for (const item of state?.recent ?? []) {
        const startedAt = parseTime(item.startedAt);
        if (startedAt === null) continue;
        events.push({
          worktree: item.worktree,
          queuedAt: parseTime(item.queuedAt),
          startedAt,
          finishedAt: parseTime(item.finishedAt),
          exit: typeof item.exit === 'number' ? item.exit : null,
          live: false,
          kind: 'recent',
        });
      }
      for (const waiter of state?.waiters ?? []) {
        const since = parseTime(waiter.since);
        if (since === null) continue;
        events.push({
          worktree: waiter.worktree,
          queuedAt: since,
          startedAt: null,
          finishedAt: null,
          exit: null,
          live: true,
          kind: 'waiter',
          endAt: now,
        });
      }
      for (const event of events) {
        event.from = event.queuedAt ?? event.startedAt ?? now;
        event.to = event.finishedAt ?? event.endAt ?? now;
      }
      events.sort((left, right) => left.from - right.from);
      return events;
    }

    /**
     * Group events into one row per worktree, newest activity first.
     * @param {Array<object>} events - timeline events.
     * @param {number} now - Host clock.
     * @returns {Array<object>} rows with their events.
     */
    function buildRows(events, now) {
      const rows = new Map();
      for (const event of events) {
        let row = rows.get(event.worktree);
        if (row === undefined) {
          row = { worktree: event.worktree, events: [], lastAt: 0, holder: false, waiter: false };
          rows.set(event.worktree, row);
        }
        row.events.push(event);
        row.lastAt = Math.max(row.lastAt, event.to);
        if (event.kind === 'holder') row.holder = true;
        if (event.kind === 'waiter') row.waiter = true;
      }
      const list = [...rows.values()];
      for (const row of list) {
        row.events.sort((left, right) => left.from - right.from);
      }
      list.sort((left, right) => {
        if (left.holder !== right.holder) return left.holder ? -1 : 1;
        if (left.waiter !== right.waiter) return left.waiter ? -1 : 1;
        return right.lastAt - left.lastAt;
      });
      return list.slice(0, MAX_ROWS);
    }

    /**
     * Choose a tick step and the tick instants for one window.
     * @param {number} start - window start.
     * @param {number} end - window end.
     * @returns {{ ticks: Array<{ at: number, x: number, label: string }> }} axis ticks.
     */
    function buildTicks(start, end) {
      const span = Math.max(1, end - start);
      const candidates = [5, 10, 15, 30, 60].map((minutes) => minutes * 60 * 1000);
      const step = candidates.find((candidate) => span / candidate <= 14) ?? 2 * 60 * 60 * 1000;
      const labelEvery = span / step > 8 ? 2 : 1;
      const ticks = [];
      const first = Math.ceil(start / step) * step;
      for (let at = first, index = 0; at <= end && ticks.length < 40; at += step, index += 1) {
        ticks.push({
          at,
          x: ((at - start) / span) * 100,
          label: index % labelEvery === 0 ? formatClock(at) : '',
        });
      }
      return { ticks };
    }

    /**
     * @param {number} value - percentage.
     * @returns {number} percentage clamped to the plot.
     */
    function clampPercent(value) {
      return Math.max(0, Math.min(100, value));
    }

    /**
     * @param {object} event - timeline event.
     * @returns {string} class for the building segment.
     */
    function buildSegmentClass(event) {
      if (event.live) return 'bq_seg bq_segLive bq_segPulse';
      if (event.exit === 0) return 'bq_seg bq_segOk';
      if (typeof event.exit === 'number') return 'bq_seg bq_segFail';
      return 'bq_seg bq_segUnknown';
    }

    /**
     * @param {object} event - timeline event.
     * @param {number} now - Host clock.
     * @returns {{ wait: number, build: number }} durations in milliseconds.
     */
    function eventDurations(event, now) {
      const waitStart = event.queuedAt ?? event.startedAt ?? now;
      const waitEnd = event.startedAt ?? now;
      const wait = Math.max(0, waitEnd - waitStart);
      const build = event.startedAt === null ? 0 : Math.max(0, (event.finishedAt ?? now) - event.startedAt);
      return { wait, build };
    }

    /**
     * @param {object} event - timeline event.
     * @param {object} t - bound translate function.
     * @returns {string} native tooltip text.
     */
    function eventTooltip(event, t) {
      if (event.kind === 'waiter') {
        return t('detail.waiter', { worktree: event.worktree, wait: formatDuration(event.to - event.from) });
      }
      const build = event.startedAt === null ? t('detail.running') : formatDuration(event.to - event.startedAt);
      const exit = event.live ? t('detail.running') : String(event.exit ?? '—');
      return [
        event.worktree,
        t('detail.line', {
          queued: event.queuedAt === null ? '—' : formatClock(event.queuedAt, true),
          started: event.startedAt === null ? '—' : formatClock(event.startedAt, true),
          build,
          exit,
        }),
      ].join('\n');
    }

    /**
     * Thin CPU sparkline over the retained samples, newest at the right edge.
     * @param {{ values: number[] }} props - retained CPU percentages.
     * @returns {object} sparkline.
     */
    function Sparkline({ values }) {
      const count = values.length;
      const points = values.map((value, index) => {
        const x = 60 - (count - 1 - index);
        const y = 14.5 - (Math.max(0, Math.min(100, value)) / 100) * 13;
        return x.toFixed(1) + ',' + y.toFixed(1);
      }).join(' ');
      return h('svg', {
        className: 'bq_spark',
        viewBox: '0 0 60 16',
        preserveAspectRatio: 'none',
        'aria-hidden': true,
      },
      h('line', {
        x1: 0, y1: 8, x2: 60, y2: 8,
        stroke: 'var(--dsw-alias-border-l1)', strokeWidth: 0.5, vectorEffect: 'non-scaling-stroke',
      }),
      count > 1
        ? h('polyline', {
          points,
          fill: 'none',
          stroke: 'var(--dsw-alias-state-business-primary)',
          strokeWidth: 1,
          strokeLinejoin: 'round',
          strokeLinecap: 'round',
          vectorEffect: 'non-scaling-stroke',
        })
        : null);
    }

    /**
     * @param {object} props - injected translate function.
     * @returns {object} the floating, draggable, resizable build queue panel.
     */
    function BuildQueuePanel({ t }) {
      const [hidden, setHidden] = React.useState(false);
      const { body, failed, resources } = useBuildQueue(!hidden);
      const [collapsed, setCollapsed] = React.useState(readCollapsed);
      const [hovered, setHovered] = React.useState(null);
      const [geom, setGeom] = React.useState(readGeom);
      const [interacting, setInteracting] = React.useState(null);
      const [history, setHistory] = React.useState([]);
      const panelRef = React.useRef(null);
      const dragRef = React.useRef(null);
      const gripRef = React.useRef(null);
      const movedRef = React.useRef(false);

      React.useEffect(() => {
        writeCollapsed(collapsed);
      }, [collapsed]);

      React.useEffect(() => {
        writeHidden(hidden);
      }, [hidden]);

      React.useEffect(() => {
        const timer = setTimeout(() => {
          writeGeom(geom);
        }, 200);
        return () => {
          clearTimeout(timer);
        };
      }, [geom]);

      React.useEffect(() => {
        const onResize = () => {
          setGeom((current) => clampGeom(current, window.innerWidth, window.innerHeight));
        };
        window.addEventListener('resize', onResize);
        return () => {
          window.removeEventListener('resize', onResize);
        };
      }, []);

      /**
       * Alt+Shift+B: bring the panel back from anywhere. Hidden restores it;
       * otherwise the geometry is reset to the default and the panel expands.
       * Ignored while the user is typing in the composer or any editable field.
       */
      React.useEffect(() => {
        const onKeyDown = (event) => {
          if (!event.altKey || !event.shiftKey || event.ctrlKey || event.metaKey) return;
          if (event.isComposing) return;
          if (event.code !== 'KeyB' && String(event.key).toLowerCase() !== 'b') return;
          const target = event.target;
          const element = target instanceof Element ? target : null;
          if (element !== null && element.closest('input,textarea,select,[contenteditable=""],[contenteditable="true"],.composer') !== null) {
            return;
          }
          event.preventDefault();
          if (hidden) {
            setHidden(false);
            return;
          }
          try {
            window.localStorage.removeItem(GEOM_KEY);
          } catch {
            /* storage unavailable: the in-memory reset below still applies */
          }
          setGeom(defaultGeom(window.innerWidth, window.innerHeight));
          setCollapsed(false);
        };
        document.addEventListener('keydown', onKeyDown, true);
        return () => {
          document.removeEventListener('keydown', onKeyDown, true);
        };
      }, [hidden]);

      const cpuPercent = typeof resources?.cpu?.percent === 'number' ? resources.cpu.percent : null;

      React.useEffect(() => {
        if (cpuPercent === null) return;
        setHistory((current) => current.concat([cpuPercent]).slice(-HISTORY));
      }, [resources]);

      /**
       * Toggle without starting a drag; re-clamp the size when expanding.
       */
      function toggleCollapsed() {
        if (movedRef.current) {
          movedRef.current = false;
          return;
        }
        if (collapsed) setGeom((current) => clampGeom(current, window.innerWidth, window.innerHeight));
        setCollapsed(!collapsed);
      }

      /**
       * @param {object} event - pointer event on the header.
       */
      function onHeadPointerDown(event) {
        if (event.pointerType === 'mouse' && event.button !== 0) return;
        dragRef.current = {
          id: event.pointerId,
          x: event.clientX,
          y: event.clientY,
          originX: geom.x,
          originY: geom.y,
          moved: false,
        };
        event.currentTarget.setPointerCapture?.(event.pointerId);
        setInteracting('drag');
      }

      /**
       * @param {object} event - pointer event on the header.
       */
      function onHeadPointerMove(event) {
        const drag = dragRef.current;
        if (drag === null || drag.id !== event.pointerId) return;
        const dx = event.clientX - drag.x;
        const dy = event.clientY - drag.y;
        if (!drag.moved && Math.hypot(dx, dy) < DRAG_THRESHOLD) return;
        drag.moved = true;
        movedRef.current = true;
        event.preventDefault();
        setGeom((current) => clampGeom({
          ...current,
          x: drag.originX + dx,
          y: drag.originY + dy,
        }, window.innerWidth, window.innerHeight));
      }

      /**
       * @param {object} event - pointer event on the header.
       */
      function onHeadPointerEnd(event) {
        const drag = dragRef.current;
        if (drag === null) return;
        dragRef.current = null;
        setInteracting(null);
        if (event.currentTarget.hasPointerCapture?.(event.pointerId)) {
          event.currentTarget.releasePointerCapture?.(event.pointerId);
        }
        if (drag.moved) {
          setTimeout(() => {
            movedRef.current = false;
          }, 0);
        }
      }

      /**
       * @param {object} event - pointer event on the resize grip.
       */
      function onGripPointerDown(event) {
        if (event.pointerType === 'mouse' && event.button !== 0) return;
        event.preventDefault();
        event.stopPropagation();
        const height = geom.h ?? panelRef.current?.offsetHeight ?? MIN_HEIGHT;
        gripRef.current = {
          id: event.pointerId,
          x: event.clientX,
          y: event.clientY,
          width: geom.w,
          height,
        };
        event.currentTarget.setPointerCapture?.(event.pointerId);
        setInteracting('resize');
      }

      /**
       * @param {object} event - pointer event on the resize grip.
       */
      function onGripPointerMove(event) {
        const grip = gripRef.current;
        if (grip === null || grip.id !== event.pointerId) return;
        event.preventDefault();
        const width = clampNumber(grip.width + (event.clientX - grip.x), MIN_WIDTH, window.innerWidth - 2 * MARGIN);
        const height = clampNumber(grip.height + (event.clientY - grip.y), MIN_HEIGHT, window.innerHeight - 2 * MARGIN);
        setGeom((current) => clampGeom({ ...current, w: width, h: height }, window.innerWidth, window.innerHeight));
      }

      /**
       * @param {object} event - pointer event on the resize grip.
       */
      function onGripPointerEnd(event) {
        if (gripRef.current === null) return;
        gripRef.current = null;
        setInteracting(null);
        if (event.currentTarget.hasPointerCapture?.(event.pointerId)) {
          event.currentTarget.releasePointerCapture?.(event.pointerId);
        }
      }

      const state = body?.state ?? null;
      const now = typeof body?.now === 'number' ? body.now : Date.now();
      const holder = state?.holder ?? null;
      const waiters = Array.isArray(state?.waiters) ? state.waiters : [];
      const recent = Array.isArray(state?.recent) ? state.recent : [];

      const phase = holder?.phase ?? '';
      const dot = holder === null ? (failed ? 'fail' : 'idle') : phaseDot(phase);
      const startedAt = holder === null ? null : parseTime(holder.startedAt);
      const queuedAt = holder === null ? null : parseTime(holder.queuedAt);
      const elapsed = startedAt === null ? null : now - startedAt;
      const waited = queuedAt === null || startedAt === null ? null : Math.max(0, startedAt - queuedAt);
      const total = holder?.total ?? 0;
      const done = holder?.done ?? 0;
      const percent = total > 0 ? Math.max(0, Math.min(100, Math.round((done / total) * 100))) : null;

      const statusText = failed && body === null
        ? t('panel.offline')
        : holder === null
          ? t('panel.idle')
          : holder.worktree + ' · ' + phaseLabel(t, phase);
      const chipText = holder === null
        ? (failed ? t('panel.offline') : t('panel.idle'))
        : holder.worktree + ' · ' + phaseLabel(t, phase);

      const memory = resources?.memory ?? null;
      const memPercent = typeof memory?.percent === 'number' ? memory.percent : null;

      /** Hide the entire panel for this session; geometry and collapsed stay put. */
      function hidePanel() {
        setHidden(true);
      }

      if (hidden) {
        return h('button', {
          type: 'button',
          className: 'bq_tab',
          style: { top: tabTop(geom, window.innerHeight) + 'px' },
          title: t('panel.show') + ' · ' + chipText,
          'aria-label': t('panel.show'),
          onClick: () => {
            setHidden(false);
          },
        });
      }

      const style = {
        left: geom.x + 'px',
        top: geom.y + 'px',
        width: collapsed ? undefined : geom.w + 'px',
        height: !collapsed && geom.h !== null ? geom.h + 'px' : undefined,
      };
      const headProps = {
        className: 'bq_head' + (collapsed ? ' bq_chip' : ''),
        role: 'button',
        tabIndex: 0,
        'aria-expanded': !collapsed,
        'aria-label': collapsed ? t('panel.expand') : t('panel.collapse'),
        title: collapsed ? t('panel.expand') + ' · ' + chipText : undefined,
        onClick: toggleCollapsed,
        onKeyDown: (event) => {
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            toggleCollapsed();
          }
        },
        onPointerDown: onHeadPointerDown,
        onPointerMove: onHeadPointerMove,
        onPointerUp: onHeadPointerEnd,
        onPointerCancel: onHeadPointerEnd,
      };
      const hideControl = h('span', {
        className: 'bq_ctl',
        role: 'button',
        tabIndex: 0,
        title: t('panel.hide'),
        'aria-label': t('panel.hide'),
        onPointerDown: (event) => {
          event.stopPropagation();
        },
        onClick: (event) => {
          event.stopPropagation();
          hidePanel();
        },
        onKeyDown: (event) => {
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            event.stopPropagation();
            hidePanel();
          }
        },
      },
      h('svg', { viewBox: '0 0 16 16', width: 14, height: 14, 'aria-hidden': true },
        h('path', {
          d: 'M4 8h8', fill: 'none', stroke: 'currentColor',
          strokeWidth: 1.5, strokeLinecap: 'round',
        })));

      if (collapsed) {
        const chip = chipRect(geom, window.innerWidth, window.innerHeight);
        return h('section', {
          className: 'bq_panel',
          'data-collapsed': 'true',
          'data-interacting': interacting ?? undefined,
          'data-testid': 'build-queue-panel',
          style: { left: chip.x + 'px', top: chip.y + 'px', width: chip.w + 'px', height: chip.h + 'px' },
        },
        h('div', headProps,
          h('span', { className: 'bq_dot', 'data-state': dot, 'aria-hidden': true }),
          h('span', { className: 'bq_headStatus' }, chipText),
          waiters.length > 0 ? h('span', { className: 'bq_badge' }, '+' + String(waiters.length)) : null,
          hideControl,
          h('span', { className: 'bq_toggle', 'aria-hidden': true },
            h('svg', { viewBox: '0 0 16 16', width: 14, height: 14 },
              h('path', {
                d: 'M4 10l4-4 4 4', fill: 'none', stroke: 'currentColor',
                strokeWidth: 1.5, strokeLinecap: 'round', strokeLinejoin: 'round',
              }))),
        ));
      }

      const events = buildEvents(state, now);
      const rows = buildRows(events, now);
      const oldest = events.length === 0 ? now - MIN_SPAN : Math.min(...events.map((event) => event.from));
      const start = Math.min(now - MIN_SPAN, oldest);
      const end = now + NOW_PAD;
      const span = Math.max(1, end - start);
      const toX = (time) => clampPercent(((time - start) / span) * 100);
      const nowX = toX(now);
      const { ticks } = buildTicks(start, end);

      const holderRow = rows.find((row) => row.holder) ?? null;
      const hoveredRow = hovered === null ? null : rows.find((row) => row.worktree === hovered) ?? null;
      const detailRow = hoveredRow ?? holderRow;
      const detailEvent = detailRow === null ? null : detailRow.events[detailRow.events.length - 1];
      const lastRecent = recent.length > 0 ? recent[0] : null;
      const detailText = detailEvent === null
        ? (lastRecent === null
          ? ''
          : t('detail.none', { worktree: lastRecent.worktree, exit: String(lastRecent.exit ?? '—') }))
        : detailEvent.kind === 'waiter'
          ? t('detail.waiter', { worktree: detailEvent.worktree, wait: formatDuration(detailEvent.to - detailEvent.from) })
          : t('detail.line', {
            queued: detailEvent.queuedAt === null ? '—' : formatClock(detailEvent.queuedAt, true),
            started: detailEvent.startedAt === null ? '—' : formatClock(detailEvent.startedAt, true),
            build: detailEvent.startedAt === null ? t('detail.running') : formatDuration((detailEvent.finishedAt ?? now) - detailEvent.startedAt),
            exit: detailEvent.live ? t('detail.running') : String(detailEvent.exit ?? '—'),
          });

      return h('section', {
        className: 'bq_panel',
        'data-interacting': interacting ?? undefined,
        'data-testid': 'build-queue-panel',
        ref: panelRef,
        style,
      },
      h('div', headProps,
        h('span', { className: 'bq_dot', 'data-state': dot, 'aria-hidden': true }),
        h('span', { className: 'bq_title' }, t('panel.title')),
        h('span', { className: 'bq_headStatus', title: statusText }, statusText),
        hideControl,
        h('span', { className: 'bq_toggle', 'aria-hidden': true },
          h('svg', { viewBox: '0 0 16 16', width: 14, height: 14 },
            h('path', {
              d: 'M4 6l4 4 4-4', fill: 'none', stroke: 'currentColor',
              strokeWidth: 1.5, strokeLinecap: 'round', strokeLinejoin: 'round',
            }))),
      ),
      h('div', { className: 'bq_body' },
        h('div', { className: 'bq_block' },
          h('div', { className: 'bq_label' }, t('current.title')),
          holder === null
            ? h('div', { className: 'bq_muted' }, t('current.none'))
            : h('div', { className: 'bq_rowLine' },
              h('span', { className: 'bq_worktree', title: holder.path || holder.worktree }, holder.worktree),
              h('span', { className: 'bq_phase', 'data-phase': phase }, phaseLabel(t, phase)),
              elapsed === null ? null : h('span', { className: 'bq_time' },
                waited !== null && waited > 0
                  ? t('current.waited', { time: formatDuration(waited) }) + ' · '
                  : null,
                t('current.elapsed', { time: formatDuration(elapsed) })),
            ),
          holder !== null && percent !== null
            ? h('div', { className: 'bq_trackBar', role: 'progressbar', 'aria-label': t('current.title'),
              'aria-valuemin': 0, 'aria-valuemax': total, 'aria-valuenow': done },
              h('div', { className: 'bq_fill', style: { width: percent + '%' } }))
            : null,
          holder !== null && percent !== null
            ? h('div', { className: 'bq_count' },
              t('current.progress', { done: String(done), total: String(total), percent: String(percent) }))
            : null,
          holder !== null && holder.lastLine
            ? h('div', { className: 'bq_last', title: holder.lastLine }, holder.lastLine)
            : null,
        ),
        h('div', { className: 'bq_block' },
          h('div', { className: 'bq_label' },
            waiters.length === 0 ? t('waiters.none') : t('waiters.line', { count: String(waiters.length) })),
          waiters.length === 0
            ? null
            : h('div', {
              className: 'bq_waiterLine',
              title: waiters.map((waiter) => {
                const since = parseTime(waiter.since);
                return waiter.worktree + (since === null ? '' : ' ' + formatDuration(now - since));
              }).join(' · '),
            }, waiters.map((waiter) => {
              const since = parseTime(waiter.since);
              return waiter.worktree + (since === null ? '' : ' ' + formatDuration(now - since));
            }).join(' · ')),
        ),
        h('div', { className: 'bq_block' },
          h('div', { className: 'bq_label' }, t('res.title')),
          h('div', { className: 'bq_res' },
            h('div', { className: 'bq_resRow' },
              h('span', { className: 'bq_resLabel' }, 'CPU'),
              h(Sparkline, { values: history }),
              h('span', { className: 'bq_resValue' },
                cpuPercent === null ? '—' : Math.round(cpuPercent) + '%'),
            ),
            h('div', { className: 'bq_resBar' },
              h('i', { style: { width: (cpuPercent ?? 0) + '%' } })),
            h('div', { className: 'bq_resRow' },
              h('span', { className: 'bq_resLabel' }, t('res.memory')),
              h('span', { className: 'bq_resMid' },
                memory === null ? '—' : formatBytes(memory.used) + ' / ' + formatBytes(memory.total)),
              h('span', { className: 'bq_resValue' },
                memPercent === null ? '—' : Math.round(memPercent) + '%'),
            ),
            h('div', { className: 'bq_resBar' },
              h('i', { style: { width: (memPercent ?? 0) + '%' } })),
          ),
        ),
        h('div', { className: 'bq_block bq_blockGrow' },
          h('div', { className: 'bq_label' }, t('timeline.title')),
          rows.length === 0
            ? h('div', { className: 'bq_muted' }, t('timeline.empty'))
            : h('div', { className: 'bq_gantt' },
              h('div', { className: 'bq_axis' },
                h('span', { className: 'bq_gutter' }),
                h('div', { className: 'bq_axisTrack' }, ticks.map((tick) => h('span', {
                  key: 'tick-' + String(tick.at),
                  className: 'bq_tick',
                  style: { left: clampPercent(tick.x) + '%' },
                })),
                ticks.map((tick) => tick.label === '' ? null : h('span', {
                  key: 'label-' + String(tick.at),
                  className: 'bq_tickLabel',
                  style: {
                    left: clampPercent(tick.x) + '%',
                    transform: tick.x < 8 ? 'translateX(0)' : tick.x > 92 ? 'translateX(-100%)' : 'translateX(-50%)',
                  },
                }, tick.label))),
              ),
              h('div', { className: 'bq_rows' }, rows.map((row) => {
                const latest = row.events[row.events.length - 1];
                const durations = eventDurations(latest, now);
                return h('div', {
                  key: row.worktree,
                  className: 'bq_trow',
                  title: row.events.slice(-3).map((event) => eventTooltip(event, t)).join('\n\n'),
                  onMouseEnter: () => {
                    setHovered(row.worktree);
                  },
                  onMouseLeave: () => {
                    setHovered(null);
                  },
                },
                h('span', { className: 'bq_rowName', title: row.worktree }, row.worktree),
                h('div', { className: 'bq_track' },
                  h('span', { className: 'bq_nowLine', style: { left: nowX + '%' } }),
                  row.events.flatMap((event, index) => {
                    const pieces = [];
                    if (event.queuedAt !== null && event.startedAt !== null && event.startedAt > event.queuedAt) {
                      const left = toX(event.queuedAt);
                      const right = toX(event.startedAt);
                      pieces.push(h('span', {
                        key: 'w' + String(index),
                        className: 'bq_seg bq_segWait',
                        style: { left: left + '%', width: Math.max(0, right - left) + '%' },
                      }));
                    }
                    if (event.startedAt !== null) {
                      const left = toX(event.startedAt);
                      const right = toX(event.finishedAt ?? now);
                      pieces.push(h('span', {
                        key: 'b' + String(index),
                        className: buildSegmentClass(event),
                        style: { left: left + '%', width: Math.max(0, right - left) + '%' },
                      }));
                    } else {
                      const left = toX(event.from);
                      pieces.push(h('span', {
                        key: 'q' + String(index),
                        className: 'bq_seg bq_segWait bq_segPulse',
                        style: { left: left + '%', width: Math.max(0, nowX - left) + '%' },
                      }));
                    }
                    return pieces;
                  }),
                ),
                h('span', { className: 'bq_rowDur' },
                  durations.wait > 0 ? h('span', { className: 'bq_durWait' }, formatDuration(durations.wait)) : null,
                  durations.wait > 0 && durations.build > 0 ? '+' : null,
                  durations.build > 0 ? h('span', { className: 'bq_durBuild' }, formatDuration(durations.build)) : null,
                ),
                );
              })),
              h('div', { className: 'bq_detail', title: detailText }, detailText),
            ),
        ),
      ),
      h('span', {
        className: 'bq_grip',
        role: 'separator',
        'aria-label': t('panel.resize'),
        title: t('panel.resize'),
        onPointerDown: onGripPointerDown,
        onPointerMove: onGripPointerMove,
        onPointerUp: onGripPointerEnd,
        onPointerCancel: onGripPointerEnd,
      },
      h('svg', { viewBox: '0 0 10 10', width: 10, height: 10, 'aria-hidden': true },
        h('path', {
          d: 'M9 3L3 9M9 6.5L6.5 9',
          fill: 'none', stroke: 'currentColor', strokeWidth: 1, strokeLinecap: 'round',
        }))),
      );
    }

    return {
      inject: ['slots', 'locale'],
      apply(ctx) {
        ctx.effect(() => ctx.locale.register(NS, { zh, en }), 'build-queue: dictionaries');
        ctx.effect(installStyles, 'build-queue: styles');
        const t = ctx.locale.bind(NS);
        ctx.slots.inject('shell.overlay', () => ctx.slots.register({
          name: 'shell.overlay',
          id: 'build-queue',
          order: 20,
          locale: NS,
          inject: () => ({ t }),
        }, BuildQueuePanel));
      },
      /* Internal helpers exported for the offline layout harness. */
      __layout: { clampGeom, defaultGeom, readGeom, chipRect, tabTop, CHIP_HEIGHT, CHIP_WIDTH },
      __state: { readCollapsed, writeCollapsed, writeHidden, writeGeom, readGeom },
    };
  },
});