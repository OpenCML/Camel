const REGION_DISPLAY_NAMES = {
  birthSpace: 'Birth (Eden)',
  havenSpace: 'Haven (To)',
  cacheSpace: 'Cache (From)',
  elderGenSpace: 'Elder Generation',
  metaSpace: 'Meta',
  permSpace: 'Permanent',
  largeObj: 'Large Object',
}

export function getRegionDisplayName(internalName) {
  return REGION_DISPLAY_NAMES[internalName] ?? internalName
}

export const REGION_TO_SPACE_ID = {
  birthSpace: 'auto.birth',
  havenSpace: 'auto.haven',
  cacheSpace: 'bump',
  elderGenSpace: 'auto.elder',
  metaSpace: 'meta',
  permSpace: 'perm',
  largeObj: 'auto.large',
}

const FIXED_REGION_ORDER = [
  'birthSpace',
  'havenSpace',
  'cacheSpace',
  'elderGenSpace',
  'metaSpace',
  'permSpace',
  'largeObj',
]

function defaultRegion(name) {
  return { name, used: 0, capacity: 1, objectCount: 0 }
}

export function getRegionsToRender(snapshot) {
  const byName = {}
  if (snapshot && snapshot.regions && Array.isArray(snapshot.regions)) {
    snapshot.regions.forEach((r) => {
      if (r && r.name) byName[r.name] = r
    })
  }
  return FIXED_REGION_ORDER.map((name) => byName[name] || defaultRegion(name))
}

export function fmtBytes(n) {
  if (n < 1024) return n + ' B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(2) + ' KB'
  return (n / (1024 * 1024)).toFixed(2) + ' MB'
}
