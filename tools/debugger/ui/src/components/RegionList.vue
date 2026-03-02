<template>
  <div class="regions">
    <!-- Young generation: Birth full width, Haven | Cache half each -->
    <div v-if="youngGenRegions.length" class="region-group">
      <div class="region-group-title">Young Generation</div>
      <div class="region-rows">
        <div class="region-row">
          <div
            v-if="birthRegion"
            class="region region-full"
            :data-region-name="birthRegion.name"
          >
            <RegionCard
              :region="birthRegion"
              :break-spaces-set="breakSpacesSet"
              :scale-for-region="scaleForRegion"
              @select="$emit('select-region', birthRegion.name)"
              @toggle-bp="$emit('toggle-bp', spaceId(birthRegion.name))"
              @scale-change="(name, val) => emit('scale-change', name, val)"
            />
          </div>
        </div>
        <div class="region-row region-row-half">
          <div
            v-for="r in [havenRegion, cacheRegion].filter(Boolean)"
            :key="r.name"
            class="region region-half"
            :data-region-name="r.name"
          >
            <RegionCard
              :region="r"
              :break-spaces-set="breakSpacesSet"
              :scale-for-region="scaleForRegion"
              @select="$emit('select-region', r.name)"
              @toggle-bp="$emit('toggle-bp', spaceId(r.name))"
              @scale-change="(name, val) => emit('scale-change', name, val)"
            />
          </div>
        </div>
      </div>
    </div>
    <!-- 其余区域：各占一行 -->
    <div
      v-for="r in otherRegions"
      :key="r.name"
      class="region"
      :data-region-name="r.name"
    >
      <RegionCard
        :region="r"
        :break-spaces-set="breakSpacesSet"
        :scale-for-region="scaleForRegion"
        @select="$emit('select-region', r.name)"
        @toggle-bp="$emit('toggle-bp', spaceId(r.name))"
        @scale-change="(name, val) => emit('scale-change', name, val)"
      />
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { REGION_TO_SPACE_ID } from '../utils/memory'
import RegionCard from './RegionCard.vue'

const props = defineProps({
  regions: { type: Array, default: () => [] },
  breakSpacesSet: { type: Set, default: () => new Set() },
  scaleForRegion: { type: Function, default: () => 1024 },
})

const emit = defineEmits(['select-region', 'toggle-bp', 'scale-change'])

function byName(name) {
  return props.regions.find((r) => r.name === name) || null
}

const birthRegion = computed(() => byName('birthSpace'))
const havenRegion = computed(() => byName('havenSpace'))
const cacheRegion = computed(() => byName('cacheSpace'))

const youngGenRegions = computed(() => {
  return [birthRegion.value, havenRegion.value, cacheRegion.value].filter(Boolean)
})

const otherRegions = computed(() => {
  const names = ['elderGenSpace', 'metaSpace', 'permSpace', 'largeObj']
  return names.map(byName).filter(Boolean)
})

function spaceId(name) {
  return REGION_TO_SPACE_ID[name] || null
}
</script>
