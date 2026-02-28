<template>
  <Teleport to="body">
    <div v-if="modelValue" class="file-picker-overlay" @click.self="onCancel">
      <div class="file-picker-modal" role="dialog" aria-label="Open script" aria-modal="true">
        <div class="file-picker-header">
          <h2 class="file-picker-title">Open .cml script</h2>
          <button type="button" class="file-picker-close" aria-label="Close" @click="onCancel">×</button>
        </div>
        <div v-if="error" class="file-picker-error" role="alert">
          {{ error }}
          <span class="file-picker-error-hint">Please choose another file or path.</span>
        </div>
        <div class="file-picker-path-bar">
          <label for="file-picker-path-input" class="file-picker-path-label">Path</label>
          <input
            id="file-picker-path-input"
            v-model="pathInput"
            type="text"
            class="file-picker-path-input"
            placeholder="e.g. C:\Projects\my\folder or ."
            @keydown.enter="goToPath"
          />
          <button type="button" class="file-picker-go btn" @click="goToPath">Go</button>
        </div>
        <div class="file-picker-body">
          <div v-if="loading" class="file-picker-loading">Loading…</div>
          <div v-else-if="listError" class="file-picker-list-error">{{ listError }}</div>
          <div v-else class="file-picker-list">
            <div v-if="canGoUp" class="file-picker-row file-picker-dir" @click="goUp">
              <span class="file-picker-icon">📁</span> ..
            </div>
            <div
              v-for="d in dirs"
              :key="'d-' + d.path"
              class="file-picker-row file-picker-dir"
              @click="enterDir(d.path)"
            >
              <span class="file-picker-icon">📁</span> {{ d.name }}
            </div>
            <div
              v-for="f in files"
              :key="'f-' + f.path"
              class="file-picker-row file-picker-file"
              @click="selectFile(f.path)"
            >
              <span class="file-picker-icon">📄</span> {{ f.name }}
            </div>
            <div v-if="!loading && !listError && dirs.length === 0 && files.length === 0" class="file-picker-empty">
              No .cml files in this folder.
            </div>
          </div>
        </div>
        <div class="file-picker-footer">
          <button type="button" class="btn file-picker-cancel" @click="onCancel">Cancel</button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, watch } from 'vue'
import { useApi } from '../composables/useApi'

const props = defineProps({
  modelValue: { type: Boolean, default: false },
  error: { type: String, default: '' },
  initialPath: { type: String, default: '.' },
})

const emit = defineEmits(['update:modelValue', 'select', 'cancel'])

const api = useApi()
const pathInput = ref(props.initialPath)
const currentPath = ref(props.initialPath)
const dirs = ref([])
const files = ref([])
const loading = ref(false)
const listError = ref('')

const canGoUp = ref(false)

watch(
  () => props.modelValue,
  (visible) => {
    if (visible) {
      pathInput.value = props.initialPath
      currentPath.value = props.initialPath
      listError.value = ''
      loadDir(props.initialPath)
    }
  }
)

watch(
  () => props.error,
  (err) => {
    if (err) listError.value = ''
  }
)

async function loadDir(dir) {
  if (!dir || dir === '') dir = '.'
  loading.value = true
  listError.value = ''
  try {
    const data = await api.fetchListDir(dir)
    if (data && data.error) {
      listError.value = data.error
      dirs.value = []
      files.value = []
      canGoUp.value = false
    } else {
      dirs.value = (data && data.dirs) || []
      files.value = (data && data.files) || []
      currentPath.value = (data && data.path) || dir
      pathInput.value = currentPath.value
      const p = currentPath.value.replace(/\\/g, '/')
      canGoUp.value = p !== '.' && p !== '' && p !== '/'
    }
  } catch (e) {
    listError.value = (e && e.message) || String(e)
    dirs.value = []
    files.value = []
    canGoUp.value = false
  } finally {
    loading.value = false
  }
}

function goToPath() {
  const p = (pathInput.value || '').trim() || '.'
  loadDir(p)
}

function enterDir(path) {
  loadDir(path)
}

function goUp() {
  const p = currentPath.value.replace(/\\/g, '/')
  if (p === '.' || p === '') {
    loadDir('..')
    return
  }
  const parts = p.split('/').filter(Boolean)
  if (parts.length <= 1) {
    loadDir(currentPath.value.startsWith('/') ? '/' : '..')
    return
  }
  parts.pop()
  const parent = p.startsWith('/') ? '/' + parts.join('/') : parts.join('/')
  loadDir(parent)
}

function selectFile(path) {
  emit('select', path)
}

function onCancel() {
  emit('update:modelValue', false)
  emit('cancel')
}
</script>
