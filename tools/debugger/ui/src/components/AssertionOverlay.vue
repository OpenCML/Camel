<template>
  <div
    v-if="appStore.assertionError"
    id="assertion-overlay"
    class="assertion-overlay"
    style="display: flex;"
    aria-live="assertive"
  >
    <div class="assertion-overlay-inner">
      <h3 class="assertion-overlay-title">Internal assertion failed</h3>
      <p class="assertion-overlay-label">Runtime assertion detail:</p>
      <pre class="assertion-overlay-message">{{ assertionMessage }}</pre>
      <p class="assertion-overlay-hint">You can click Restart to clear and run again, or fix the script and Run from the Run tab.</p>
      <button type="button" class="btn btn-continue" @click="onRestart">Restart</button>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { useAppStore } from '../stores/app'
import { useApi } from '../composables/useApi'

const appStore = useAppStore()
const api = useApi()

const assertionMessage = computed(() => {
  const err = appStore.assertionError
  if (!err) return ''
  return typeof err === 'string' ? err : (err.message || JSON.stringify(err))
})

function onRestart() {
  api.postRestart(appStore.currentTaskId).then(() => {}).catch(() => {})
}
</script>
