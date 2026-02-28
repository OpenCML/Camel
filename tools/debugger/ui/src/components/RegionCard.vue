<template>
  <div class="region-card">
    <div
      class="region-header"
      :data-region-name="region.name"
      @click="$emit('select')"
    >
      <div class="region-header-left">
        <span class="region-name" :title="regionDisplayName(region.name)">{{ regionDisplayName(region.name) }}</span>
        <span class="region-stats" :title="statsTitle(region)">{{ fmtBytesShort(region.used) }}/{{ fmtBytesShort(region.capacity) }}</span>
      </div>
      <div class="region-header-right">
        <button
          v-if="spaceId(region.name)"
          type="button"
          :class="['region-bp-toggle', { 'bp-on': breakSpacesSet.has(spaceId(region.name)) }]"
          title="Pause on alloc in this space"
          aria-label="Breakpoint"
          @click.stop="$emit('toggle-bp')"
        >
          <span class="bp-dot"></span>
        </button>
        <select
          v-if="region.capacity > 0"
          class="region-scale"
          :value="scaleForRegion(region.name)"
          @change="onScaleChange(region.name, $event)"
        >
          <option v-for="lv in scaleLevels" :key="lv.value" :value="lv.value">{{ lv.label }}</option>
        </select>
      </div>
    </div>
    <div v-if="(region.capacity || 0) > 0" class="region-viz">
      <div class="used" :style="{ width: pct(region) + '%' }"></div>
      <div class="free" :style="{ width: (100 - pct(region)) + '%' }"></div>
    </div>
  </div>
</template>

<script setup>
import { getRegionDisplayName, REGION_TO_SPACE_ID, fmtBytes } from '../utils/memory'

const props = defineProps({
  region: { type: Object, required: true },
  breakSpacesSet: { type: Set, default: () => new Set() },
  scaleForRegion: { type: Function, default: () => 1024 },
})

const emit = defineEmits(['select', 'toggle-bp', 'scale-change'])

const scaleLevels = [
  { value: 1024, label: '1 KB' },
  { value: 4096, label: '4 KB' },
  { value: 16384, label: '16 KB' },
  { value: 65536, label: '64 KB' },
  { value: 262144, label: '256 KB' },
  { value: 1048576, label: '1 MB' },
  { value: 4194304, label: '4 MB' },
  { value: 16777216, label: '16 MB' },
  { value: 0, label: 'Actual' },
]

function fmtBytesShort(n) {
  if (n < 1024) return n + 'B'
  if (n < 1024 * 1024) {
    const kb = n / 1024
    return kb % 1 === 0 ? kb + 'KB' : kb.toFixed(1) + 'KB'
  }
  const mb = n / (1024 * 1024)
  return mb % 1 === 0 ? mb + 'MB' : mb.toFixed(1) + 'MB'
}

function regionDisplayName(name) {
  return getRegionDisplayName(name)
}

function spaceId(name) {
  return REGION_TO_SPACE_ID[name] || null
}

function statsTitle(r) {
  return fmtBytes(r.used || 0) + ' / ' + fmtBytes(r.capacity || 0) + ' (' + (r.objectCount || 0) + ' objects)'
}

function pct(r) {
  const cap = r.capacity || 1
  const used = r.used || 0
  const scaleVal = props.scaleForRegion(r.name)
  const scale = scaleVal === 0 ? cap : scaleVal
  return scale > 0 ? Math.min(100, (used / scale) * 100) : 0
}

function onScaleChange(regionName, ev) {
  const val = parseInt(ev.target.value, 10)
  emit('scale-change', regionName, val)
}
</script>
