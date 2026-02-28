<template>
  <div class="monitor-body">
    <div class="main-layout" id="memory-with-snapshot">
      <div class="main-left">
        <div class="content-wrap">
          <div ref="contentRef" id="content">
            <div class="regions">
              <RegionList
                v-if="regions.length"
                :regions="regions"
                :break-spaces-set="breakSpacesSet"
                :scale-for-region="getScaleForRegion"
                @select-region="selectRegion"
                @toggle-bp="toggleBp"
                @scale-change="(name, val) => { regionScales.value = { ...regionScales.value, [name]: val } }"
              />
            </div>
          </div>
        </div>
      </div>
      <div class="main-right memory-detail-panel">
        <div class="region-panel-toolbar">
          <div class="region-panel-toolbar-left">
            <span class="panel-title region-name">{{ selectedRegionDisplayName }}</span>
            <span v-if="selectedRegionName" class="hex-page-offset-inline" :title="hexPageTitle">{{ hexPageOffsetText }}</span>
          </div>
          <div class="region-panel-toolbar-right">
            <div class="view-tabs">
              <button type="button" :class="['view-tab', { active: viewMode === 'hex' }]" @click="viewMode = 'hex'">HEX</button>
              <button type="button" :class="['view-tab', { active: viewMode === 'objects' }]" @click="viewMode = 'objects'">OBJ</button>
            </div>
            <button v-if="selectedRegionName" type="button" class="btn-icon region-refresh-btn" title="Refresh" aria-label="Refresh" @click="refreshRegion">↻</button>
          </div>
        </div>
        <div class="region-panel-body" id="region-panel-body">
          <div v-if="hexLoading" class="loader-overlay visible"><span class="spinner"></span><span>Loading…</span></div>
          <div v-if="viewMode === 'hex'" id="view-hex" class="view-pane">
            <div class="hex-view" ref="hexViewRef">
              <div class="hex-view-inner" ref="hexContentRef"></div>
            </div>
            <div class="hex-pagination">
              <label class="lines-per-page">
                <select v-model.number="hexLinesPerPage">
                  <option :value="0">Fit</option>
                  <option :value="16">16</option>
                  <option :value="32">32</option>
                  <option :value="64">64</option>
                  <option :value="128">128</option>
                </select>
                <span class="lines-per-page-label">lines/page</span>
              </label>
              <div v-if="hexTotalPages > 1" class="hex-page-nav">
                <button type="button" class="hex-page-arrow" :disabled="hexCurrentPage <= 0" aria-label="Previous page" @click="hexCurrentPage--; loadHexPage()">‹</button>
                <span class="page-info">{{ hexCurrentPage + 1 }} / {{ hexTotalPages }}</span>
                <button type="button" class="hex-page-arrow" :disabled="hexCurrentPage >= hexTotalPages - 1" aria-label="Next page" @click="hexCurrentPage++; loadHexPage()">›</button>
              </div>
            </div>
          </div>
          <div v-if="viewMode === 'objects'" id="view-objects" class="view-pane">
            <table class="obj-table">
              <thead><tr><th>Address</th><th>Offset</th><th>Size</th><th>Age</th></tr></thead>
              <tbody>
                <tr v-for="o in objects" :key="o.addr" class="obj-row-clickable">
                  <td class="col-addr">0x{{ (o.addr || 0).toString(16) }}</td>
                  <td class="col-offset">0x{{ ((o.addr || 0) - objRegionStart).toString(16) }}</td>
                  <td class="col-size">{{ o.size }}</td>
                  <td class="col-age">{{ o.age ?? '' }}</td>
                </tr>
              </tbody>
            </table>
            <div v-if="objHasMore" class="obj-load-more">
              <button type="button" class="btn btn-continue" @click="loadMoreObjects">Load more</button>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.memory-detail-panel { display: flex; flex-direction: column; gap: 12px; }
.region-panel-toolbar {
  display: flex; align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap;
  padding: 8px 12px; background: var(--bg-panel-alt); border-radius: 6px; border: 1px solid var(--border-divider-subtle);
}
.region-panel-toolbar-left { display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1; }
.region-panel-toolbar-left .panel-title { font-weight: 600; font-size: 0.9rem; color: #e4e6eb; }
.region-panel-toolbar-left .hex-page-offset-inline { font-family: Consolas, monospace; font-size: 0.8rem; color: #8b949e; }
.region-panel-toolbar-right { display: flex; align-items: center; gap: 8px; flex-shrink: 0; }
.region-panel-toolbar-right .view-tabs { display: flex; gap: 2px; }
.region-refresh-btn { padding: 4px 8px; border-radius: 4px; }
.region-refresh-btn:hover { background: #30363d; color: #58a6ff; }
.region-panel-body {
  flex: 1; min-height: 0; display: flex; flex-direction: column; overflow: hidden;
  background: #0d1117; border-radius: 6px; border: 1px solid var(--border-divider-subtle); padding: 12px;
}
</style>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'
import RegionList from '../components/RegionList.vue'
import { renderHexView, getPrevLinesFromContainer } from '../utils/hex'
import { getRegionsToRender, getRegionDisplayName, REGION_TO_SPACE_ID } from '../utils/memory'

const appStore = useAppStore()
const api = useApi()

const contentRef = ref(null)
const hexContentRef = ref(null)
const hexViewRef = ref(null)

const snapshot = ref(null)
const regions = ref([])
const breakSpacesSet = ref(new Set())
const selectedRegionName = ref('')
const viewMode = ref('hex')
const hexLoading = ref(false)
const hexCurrentPage = ref(0)
const hexLinesPerPage = ref(32)
/** When "Fit" is selected, lines per page is derived from container height. */
const fitComputedLines = ref(32)
const HEX_LINE_HEIGHT_PX = 20
const hexTotalPages = ref(1)
const hexTotal = ref(0)
const hexPageOffsetText = ref('')
const hexPageTitle = ref('')

const effectiveLinesPerPage = computed(() =>
  hexLinesPerPage.value === 0 ? Math.max(1, fitComputedLines.value) : hexLinesPerPage.value
)
const objects = ref([])
const objNextOffset = ref(0)
const objRegionStart = ref(0)
const objHasMore = ref(false)
const regionScales = ref({})

const SCALE_LEVELS = [1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 0]

const hasSnapshot = computed(() => !!appStore.currentTaskId)

const totalUsed = computed(() => regions.value.reduce((s, r) => s + (r.used || 0), 0))
const totalCap = computed(() => regions.value.reduce((s, r) => s + (r.capacity || 0), 0))

const selectedRegionDisplayName = computed(() =>
  selectedRegionName.value ? getRegionDisplayName(selectedRegionName.value) : 'Click a region on the left'
)

function fmtBytes(n) {
  if (n < 1024) return n + ' B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(2) + ' KB'
  return (n / (1024 * 1024)).toFixed(2) + ' MB'
}

function getScaleForRegion(name) {
  return regionScales.value[name] !== undefined ? regionScales.value[name] : SCALE_LEVELS[0]
}

function fetchData() {
  const taskId = appStore.currentTaskId
  if (!taskId) {
    regions.value = getRegionsToRender(null)
    snapshot.value = null
    return
  }
  // 不在此处清空 regions，避免刷新时整区闪烁；等 snapshot 返回后再更新
  const hadLocalBp = breakSpacesSet.value.size > 0
  Promise.all([
    api.fetchBreakpointSpaces(taskId).then((d) => {
      const fromApi = d && Array.isArray(d.breakSpaces) ? d.breakSpaces : []
      if (fromApi.length > 0) {
        breakSpacesSet.value = new Set(fromApi)
      } else if (hadLocalBp) {
        api.postBreakpointSpaces([...breakSpacesSet.value], taskId)
      }
    }),
    api.fetchSnapshot(taskId),
  ]).then(([, snap]) => {
    const next = getRegionsToRender(snap)
    // 就地更新，避免整表替换导致列表闪烁
    next.forEach((r, i) => {
      if (regions.value[i]) Object.assign(regions.value[i], r)
      else regions.value[i] = r
    })
    regions.value.length = next.length
    snapshot.value = snap
  }).catch(() => {
    regions.value = getRegionsToRender(null)
    snapshot.value = null
  })
}

function selectRegion(name) {
  selectedRegionName.value = name
  hexCurrentPage.value = 0
  objNextOffset.value = 0
  objects.value = []
  loadHexPage()
}

function toggleBp(spaceId) {
  const set = new Set(breakSpacesSet.value)
  if (set.has(spaceId)) set.delete(spaceId)
  else set.add(spaceId)
  breakSpacesSet.value = set
  const taskId = appStore.currentTaskId
  if (taskId) {
    api.postBreakpointSpaces([...set], taskId).then(() => {
      api.fetchBreakpointSpaces(taskId).then((d) => {
        if (d && Array.isArray(d.breakSpaces)) breakSpacesSet.value = new Set(d.breakSpaces)
      })
    })
  }
}

function loadHexPage(silent = false) {
  if (!selectedRegionName.value) return
  const lines = effectiveLinesPerPage.value
  const offsetBytes = hexCurrentPage.value * lines * 16
  const limitBytes = lines * 16
  const prevLines = hexContentRef.value ? getPrevLinesFromContainer(hexContentRef.value) : {}
  if (!silent) hexLoading.value = true
  api
    .fetchRegionMemory(selectedRegionName.value, offsetBytes, limitBytes, appStore.currentTaskId)
    .then((d) => {
      if (!silent) hexLoading.value = false
      if (d.error) {
        hexContentRef.value && (hexContentRef.value.innerHTML = '<div class="error">' + d.error + '</div>')
        return
      }
      const region = snapshot.value?.regions?.find((r) => r.name === selectedRegionName.value)
      if (region && region.used != null) d.used = Number(region.used)
      hexTotal.value = d.capacity != null ? d.capacity : d.total || 0
      const totalLines = Math.ceil(hexTotal.value / 16) || 1
      hexTotalPages.value = Math.max(1, Math.ceil(totalLines / lines))
      const pageStartOffset = d.offset != null ? d.offset : offsetBytes
      const regionStart = region && region.start != null ? Number(region.start) : 0
      const pageStartAddress = regionStart + pageStartOffset
      hexPageOffsetText.value = '0x' + pageStartAddress.toString(16).toUpperCase().padStart(8, '0')
      hexPageTitle.value = 'Page start address: 0x' + pageStartAddress.toString(16)
      renderHexView(hexContentRef.value, d, prevLines, {
        linesPerPage: lines,
        diffFadeMs: 2200,
      })
    })
    .catch(() => {
      if (!silent) hexLoading.value = false
    })
}

function loadObjects() {
  if (!selectedRegionName.value) return
  api
    .fetchRegionObjects(selectedRegionName.value, objNextOffset.value, 50, appStore.currentTaskId)
    .then((d) => {
      objRegionStart.value = Number(d.regionStart) || 0
      const objs = d.objects || []
      objects.value = objects.value.concat(objs)
      objNextOffset.value += objs.length
      objHasMore.value = !!d.hasMore
    })
}

function loadMoreObjects() {
  loadObjects()
}

function refreshRegion(silent = false) {
  loadHexPage(silent)
  if (viewMode.value === 'objects') {
    objNextOffset.value = 0
    objects.value = []
    loadObjects()
  }
}

watch(() => appStore.currentTab, (tab) => {
  if (tab === 'memory') fetchData()
})
watch(() => appStore.currentTaskId, () => fetchData(), { immediate: true })

watch(viewMode, (mode) => {
  if (!selectedRegionName.value) return
  if (mode === 'hex') {
    loadHexPage()
  } else if (mode === 'objects') {
    objNextOffset.value = 0
    objects.value = []
    loadObjects()
  }
})

watch(hexLinesPerPage, (val) => {
  if (val === 0) updateFitLines()
  if (!selectedRegionName.value) return
  hexCurrentPage.value = 0
  loadHexPage()
})

watch(fitComputedLines, () => {
  if (hexLinesPerPage.value === 0 && selectedRegionName.value) loadHexPage(true)
})

// 内存监控区自动刷新：在 Memory 页且任务为 running/paused 时定期拉取 snapshot，更新各 space 容量
let refreshTimer = null
function startRefreshTimer() {
  if (refreshTimer) return
  refreshTimer = setInterval(() => {
    if (appStore.currentTab !== 'memory' || !appStore.currentTaskId) return
    const task = appStore.currentTask
    const state = task?.taskState
    if (state !== 'running' && state !== 'paused') return
    fetchData()
  }, 1500)
}
function stopRefreshTimer() {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
}
watch(
  () => [appStore.currentTab, appStore.currentTaskId, appStore.currentTask?.taskState],
  () => {
    const onMemory = appStore.currentTab === 'memory'
    const task = appStore.currentTask
    const state = task?.taskState
    const shouldPoll = onMemory && appStore.currentTaskId && (state === 'running' || state === 'paused')
    if (shouldPoll) startRefreshTimer()
    else stopRefreshTimer()
  },
  { immediate: true }
)

// 右侧内存视图（当前页）定期刷新：有选中 region 且任务在跑/暂停时，每隔一段时间刷新 hex/obj 当前页
const RIGHT_PANEL_REFRESH_INTERVAL_MS = 2500
let rightPanelTimer = null
function startRightPanelRefreshTimer() {
  if (rightPanelTimer) return
  rightPanelTimer = setInterval(() => {
    if (appStore.currentTab !== 'memory' || !selectedRegionName.value) return
    const task = appStore.currentTask
    const state = task?.taskState
    if (state !== 'running' && state !== 'paused') return
    refreshRegion(true)
  }, RIGHT_PANEL_REFRESH_INTERVAL_MS)
}
function stopRightPanelRefreshTimer() {
  if (rightPanelTimer) {
    clearInterval(rightPanelTimer)
    rightPanelTimer = null
  }
}
watch(
  () => [
    appStore.currentTab,
    appStore.currentTaskId,
    appStore.currentTask?.taskState,
    selectedRegionName.value,
  ],
  () => {
    const onMemory = appStore.currentTab === 'memory'
    const hasRegion = !!selectedRegionName.value
    const task = appStore.currentTask
    const state = task?.taskState
    const shouldPoll =
      onMemory && hasRegion && appStore.currentTaskId && (state === 'running' || state === 'paused')
    if (shouldPoll) startRightPanelRefreshTimer()
    else stopRightPanelRefreshTimer()
  },
  { immediate: true }
)

// 发生与内存相关的断点（alloc 暂停）时，立即刷新右侧当前页一次
function isAllocPauseReason(reason) {
  if (!reason || typeof reason !== 'object') return false
  return reason.phase === 'before' || reason.ptr !== undefined || reason.space != null
}
const wasPausedForAlloc = ref(false)
watch(
  () => [appStore.paused, appStore.pauseReason],
  ([paused, reason]) => {
    const allocPause = !!paused && isAllocPauseReason(reason)
    if (allocPause && !wasPausedForAlloc.value) {
      wasPausedForAlloc.value = true
      if (appStore.currentTab === 'memory' && selectedRegionName.value) refreshRegion()
    }
    if (!paused) wasPausedForAlloc.value = false
  },
  { immediate: true }
)

let fitResizeObserver = null
function updateFitLines() {
  const el = hexViewRef.value
  if (el && hexLinesPerPage.value === 0) {
    const h = el.clientHeight || 0
    const next = Math.max(1, Math.floor(h / HEX_LINE_HEIGHT_PX))
    if (next !== fitComputedLines.value) fitComputedLines.value = next
  }
}

onMounted(() => {
  if (appStore.currentTab === 'memory') fetchData()
  contentRef.value?.addEventListener('click', (e) => {
    const header = e.target.closest('.region-header[data-region-name]')
    if (header && !e.target.closest('.region-bp-toggle')) {
      selectRegion(header.getAttribute('data-region-name'))
    }
  })
  const el = hexViewRef.value
  if (el && typeof ResizeObserver !== 'undefined') {
    fitResizeObserver = new ResizeObserver(updateFitLines)
    fitResizeObserver.observe(el)
    updateFitLines()
  }
})
onUnmounted(() => {
  stopRefreshTimer()
  stopRightPanelRefreshTimer()
  if (fitResizeObserver && hexViewRef.value) {
    fitResizeObserver.disconnect()
    fitResizeObserver = null
  }
})
</script>
