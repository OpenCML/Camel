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
      <div class="main-right">
        <div class="region-panel-toolbar">
          <span class="panel-title region-name">{{ selectedRegionDisplayName }}</span>
          <span v-if="selectedRegionName" class="hex-page-offset-inline visible" :title="hexPageTitle">{{ hexPageOffsetText }}</span>
          <button v-if="selectedRegionName" type="button" class="btn-icon" title="Refresh" aria-label="Refresh" @click="refreshRegion">↻</button>
          <div class="view-tabs">
            <button type="button" :class="['view-tab', { active: viewMode === 'hex' }]" @click="viewMode = 'hex'">HEX</button>
            <button type="button" :class="['view-tab', { active: viewMode === 'objects' }]" @click="viewMode = 'objects'">OBJ</button>
          </div>
        </div>
        <div class="panel-body" id="region-panel-body">
          <div v-if="hexLoading" class="loader-overlay visible"><span class="spinner"></span><span>Loading…</span></div>
          <div v-if="viewMode === 'hex'" id="view-hex" class="view-pane">
            <div class="hex-view" ref="hexViewRef">
              <div class="hex-view-inner" ref="hexContentRef"></div>
            </div>
            <div v-if="hexTotalPages > 1" class="hex-pagination">
              <label class="lines-per-page">Lines: <select v-model.number="hexLinesPerPage"><option :value="16">16</option><option :value="32">32</option><option :value="64">64</option><option :value="128">128</option></select></label>
              <button type="button" class="btn btn-page btn-continue" :disabled="hexCurrentPage <= 0" @click="hexCurrentPage--; loadHexPage()">Prev</button>
              <span class="page-info">Page {{ hexCurrentPage + 1 }} / {{ hexTotalPages }}</span>
              <button type="button" class="btn btn-page btn-continue" :disabled="hexCurrentPage >= hexTotalPages - 1" @click="hexCurrentPage++; loadHexPage()">Next</button>
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

<script setup>
import { ref, computed, watch, onMounted } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'
import RegionList from '../components/RegionList.vue'
import { renderHexView } from '../utils/hex'
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
const hexTotalPages = ref(1)
const hexTotal = ref(0)
const hexPageOffsetText = ref('')
const hexPageTitle = ref('')
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
  regions.value = getRegionsToRender(null)
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
    regions.value = getRegionsToRender(snap)
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

function loadHexPage() {
  if (!selectedRegionName.value) return
  hexLoading.value = true
  const offsetBytes = hexCurrentPage.value * hexLinesPerPage.value * 16
  const limitBytes = hexLinesPerPage.value * 16
  api
    .fetchRegionMemory(selectedRegionName.value, offsetBytes, limitBytes, appStore.currentTaskId)
    .then((d) => {
      hexLoading.value = false
      if (d.error) {
        hexContentRef.value && (hexContentRef.value.innerHTML = '<div class="error">' + d.error + '</div>')
        return
      }
      hexTotal.value = d.capacity != null ? d.capacity : d.total || 0
      const totalLines = Math.ceil(hexTotal.value / 16) || 1
      hexTotalPages.value = Math.max(1, Math.ceil(totalLines / hexLinesPerPage.value))
      const pageStartOffset = d.offset != null ? d.offset : offsetBytes
      const region = snapshot.value?.regions?.find((r) => r.name === selectedRegionName.value)
      const regionStart = region && region.start != null ? Number(region.start) : 0
      const pageStartAddress = regionStart + pageStartOffset
      hexPageOffsetText.value = '0x' + pageStartAddress.toString(16).toUpperCase().padStart(8, '0')
      hexPageTitle.value = 'Page start address: 0x' + pageStartAddress.toString(16)
      renderHexView(hexContentRef.value, d, {}, { linesPerPage: hexLinesPerPage.value })
    })
    .catch(() => {
      hexLoading.value = false
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

function refreshRegion() {
  loadHexPage()
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
watch([viewMode, selectedRegionName], () => {
  if (viewMode.value === 'objects' && selectedRegionName.value) {
    objNextOffset.value = 0
    objects.value = []
    loadObjects()
  }
})
onMounted(() => {
  if (appStore.currentTab === 'memory') fetchData()
  contentRef.value?.addEventListener('click', (e) => {
    const header = e.target.closest('.region-header[data-region-name]')
    if (header && !e.target.closest('.region-bp-toggle')) {
      selectRegion(header.getAttribute('data-region-name'))
    }
  })
})
</script>
