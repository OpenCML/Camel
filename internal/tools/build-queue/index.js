// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Host half of the build-queue bundle.
 *
 * The panel runs in the browser and cannot read a local file, so this half owns
 * the file read and exposes the normalized state on Connection's authenticated
 * Fetch channel (`/api/build-queue.state`). The Client half polls that route.
 *
 * The Camel build script rewrites its state file atomically (temp file + move),
 * so a missing or momentarily unreadable file is a normal "no build" moment:
 * every failure normalizes to `state: null` instead of an HTTP error.
 */
import { readFile } from 'node:fs/promises';
import os from 'node:os';

/** State file published by camel-build.ps1. */
const DEFAULT_STATE_PATH = 'D:\\Projects\\Camel-wt\\build-state.json';
/** One retry covers a read that lands between the temp write and the move. */
const RETRY_DELAY_MS = 25;
/** Bound on the build output line carried to the browser. */
const MAX_LAST_LINE = 400;
/** Bounds on list lengths; the panel draws the timeline from the recent window. */
const MAX_WAITERS = 50;
const MAX_RECENT = 30;

/** Services required for the authenticated Fetch route. */
export const inject = ['connection'];

/**
 * @param {unknown} value - parsed state document.
 * @returns {string | null} the document's update timestamp, or null.
 */
function readTimestamp(value) {
  return typeof value === 'string' && value.length > 0 ? value : null;
}

/**
 * @param {unknown} value - candidate holder record.
 * @returns {number} a finite non-negative number, else 0.
 */
function readCount(value) {
  return typeof value === 'number' && Number.isFinite(value) && value > 0 ? value : 0;
}

/**
 * @param {unknown} value - candidate holder record.
 * @returns {string} the build phase, possibly empty.
 */
function readPhase(value) {
  return typeof value === 'string' ? value : '';
}

/**
 * @param {unknown} value - candidate build output line.
 * @returns {string} a bounded single line.
 */
function readLastLine(value) {
  if (typeof value !== 'string') return '';
  const flat = value.replace(/\r?\n/g, ' ').trim();
  return flat.length > MAX_LAST_LINE ? `${flat.slice(0, MAX_LAST_LINE)}…` : flat;
}

/**
 * Normalize one holder record so the Client renders a predictable shape.
 * @param {unknown} value - `holder` field of the state document.
 * @returns {object | null} normalized holder, or null when no build holds the lock.
 */
function normalizeHolder(value) {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return null;
  const worktree = typeof value.worktree === 'string' ? value.worktree.trim() : '';
  if (worktree.length === 0) return null;
  return {
    worktree,
    path: typeof value.path === 'string' ? value.path : '',
    pid: typeof value.pid === 'number' ? value.pid : null,
    queuedAt: readTimestamp(value.queuedAt),
    startedAt: readTimestamp(value.startedAt),
    done: readCount(value.done),
    total: readCount(value.total),
    phase: readPhase(value.phase),
    lastLine: readLastLine(value.lastLine),
  };
}

/**
 * Normalize the waiting list in publish order.
 * @param {unknown} value - `waiters` field of the state document.
 * @returns {Array<{ worktree: string, pid: number | null, since: string | null }>} waiters.
 */
function normalizeWaiters(value) {
  if (!Array.isArray(value)) return [];
  const waiters = [];
  for (const item of value) {
    if (typeof item !== 'object' || item === null) continue;
    const worktree = typeof item.worktree === 'string' ? item.worktree.trim() : '';
    if (worktree.length === 0) continue;
    waiters.push({
      worktree,
      pid: typeof item.pid === 'number' ? item.pid : null,
      since: readTimestamp(item.since),
    });
    if (waiters.length >= MAX_WAITERS) break;
  }
  return waiters;
}

/**
 * Normalize the finished builds, newest first.
 * @param {unknown} value - `recent` field of the state document.
 * @returns {Array<object>} recent builds.
 */
function normalizeRecent(value) {
  if (!Array.isArray(value)) return [];
  const recent = [];
  for (const item of value) {
    if (typeof item !== 'object' || item === null) continue;
    const worktree = typeof item.worktree === 'string' ? item.worktree.trim() : '';
    if (worktree.length === 0) continue;
    const seconds = typeof item.seconds === 'number' && Number.isFinite(item.seconds) ? item.seconds : null;
    recent.push({
      worktree,
      queuedAt: readTimestamp(item.queuedAt),
      startedAt: readTimestamp(item.startedAt),
      finishedAt: readTimestamp(item.finishedAt),
      exit: typeof item.exit === 'number' && Number.isFinite(item.exit) ? item.exit : null,
      seconds,
      at: Date.parse(item.finishedAt ?? '') || 0,
    });
  }
  recent.sort((left, right) => right.at - left.at);
  return recent.slice(0, MAX_RECENT).map(({ at, ...rest }) => rest);
}

/**
 * Shape one parsed document for the browser.
 * @param {unknown} parsed - JSON document from the state file.
 * @returns {object} normalized snapshot.
 */
export function normalizeState(parsed) {
  const document = typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed) ? parsed : {};
  return {
    updatedAt: readTimestamp(document.updatedAt),
    holder: normalizeHolder(document.holder),
    waiters: normalizeWaiters(document.waiters),
    recent: normalizeRecent(document.recent),
  };
}

/**
 * @param {number} ms - delay in milliseconds.
 * @returns {Promise<void>} resolves after the delay.
 */
function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

/**
 * Read the published build state. A missing, unreadable, or malformed file is
 * reported as `state: null` because the writer's atomic move can be observed
 * between the temp file and the target.
 * @param {string} statePath - absolute state file path.
 * @returns {Promise<object | null>} normalized snapshot, or null.
 */
export async function readBuildState(statePath) {
  for (let attempt = 0; attempt < 2; attempt += 1) {
    if (attempt > 0) await delay(RETRY_DELAY_MS);
    let text;
    try {
      text = await readFile(statePath, 'utf8');
    } catch {
      continue;
    }
    if (text.trim().length === 0) continue;
    let parsed;
    try {
      parsed = JSON.parse(text);
    } catch {
      continue;
    }
    return normalizeState(parsed);
  }
  return null;
}

/**
 * Compose one response payload; the Host clock is the reference the Client uses
 * for elapsed and waited durations.
 * @param {string} statePath - absolute state file path.
 * @returns {Promise<object>} response body.
 */
async function snapshot(statePath) {
  const state = await readBuildState(statePath);
  return { ok: true, now: Date.now(), state };
}

/** Shortest CPU sampling window; a faster repeat returns the previous value. */
const CPU_MIN_WINDOW_MS = 250;
/** Last cumulative CPU counters, used to derive a rate on the next call. */
let previousCpu = null;

/**
 * Sample whole-machine CPU usage from the cumulative per-core counters.
 * @returns {{ percent: number | null, cores: number, sampled: boolean }} CPU sample.
 */
export function sampleCpu() {
  const cpus = os.cpus();
  let busy = 0;
  let total = 0;
  for (const cpu of cpus) {
    const times = cpu.times;
    const sum = times.user + times.nice + times.sys + times.idle + times.irq;
    busy += sum - times.idle;
    total += sum;
  }
  const at = Date.now();
  const before = previousCpu;
  const percentOf = (deltaBusy, deltaTotal) => (deltaTotal > 0
    ? Math.max(0, Math.min(100, (deltaBusy / deltaTotal) * 100))
    : null);
  let percent = before === null ? null : percentOf(busy - before.busy, total - before.total);
  const sampled = before !== null && at - before.at >= CPU_MIN_WINDOW_MS;
  if (!sampled && before !== null) percent = before.percent;
  if (before === null || at - before.at >= CPU_MIN_WINDOW_MS) {
    previousCpu = { at, busy, total, percent };
  }
  return { percent, cores: cpus.length, sampled };
}

/**
 * Sample whole-machine memory, plus a load average only where it is meaningful
 * (Windows reports three zeros, which is reported as no load data).
 * @returns {object} resource sample.
 */
export function sampleResources() {
  const total = os.totalmem();
  const free = os.freemem();
  const used = Math.max(0, total - free);
  const load = os.loadavg();
  return {
    ok: true,
    now: Date.now(),
    cpu: sampleCpu(),
    memory: {
      total,
      used,
      free,
      percent: total > 0 ? Math.max(0, Math.min(100, (used / total) * 100)) : null,
    },
    load: load.some((value) => value > 0) ? load : null,
  };
}

/**
 * Register the authenticated exact Fetch routes the panel polls.
 * @param {object} ctx - Host context carrying the Connection service.
 * @param {{ statePath?: string } | undefined} config - bundle row configuration.
 */
export function apply(ctx, config) {
  const statePath = typeof config?.statePath === 'string' && config.statePath.length > 0
    ? config.statePath
    : DEFAULT_STATE_PATH;
  ctx.connection.fetch.register({
    path: '/api/build-queue.state',
    methods: ['GET'],
    requestBody: 'buffered',
    fetch: async () => Response.json(await snapshot(statePath), {
      headers: { 'cache-control': 'no-store' },
    }),
  });
  ctx.connection.fetch.register({
    path: '/api/build-queue.resources',
    methods: ['GET'],
    requestBody: 'buffered',
    fetch: async () => Response.json(sampleResources(), {
      headers: { 'cache-control': 'no-store' },
    }),
  });
}