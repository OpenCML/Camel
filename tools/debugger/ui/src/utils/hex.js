const BYTES_PER_LINE = 16

function byteToHex(b) {
  return ('0' + (Number(b) & 0xff).toString(16)).slice(-2)
}

function byteToAscii(b) {
  const n = Number(b) & 0xff
  return n >= 32 && n < 127 ? String.fromCharCode(n) : '.'
}

export function bytesToHexLines(data, baseOffset, bytesPerLine = BYTES_PER_LINE) {
  const lines = []
  if (!Array.isArray(data) || data.length === 0) return lines
  for (let i = 0; i < data.length; i += bytesPerLine) {
    const lineOffset = baseOffset + i
    const chunk = data.slice(i, i + bytesPerLine)
    let hex = ''
    let ascii = ''
    for (let j = 0; j < bytesPerLine; j++) {
      if (j < chunk.length) {
        hex += (j > 0 ? ' ' : '') + byteToHex(chunk[j])
        ascii += byteToAscii(chunk[j])
      } else {
        hex += (j > 0 ? ' ' : '') + '  '
        ascii += ' '
      }
    }
    lines.push({ offset: lineOffset, hex: hex.trim(), ascii })
  }
  return lines
}

export function getPrevLinesFromContainer(container) {
  const prevLines = {}
  if (!container) return prevLines
  container.querySelectorAll('.hex-line').forEach((div) => {
    const spans = div.querySelectorAll('.hex-offset, .hex-bytes')
    if (spans.length >= 2) {
      const offset = parseInt(spans[0].textContent.trim(), 16)
      const hex = (spans[1].textContent || '').trim()
      prevLines[offset] = hex
    }
  })
  return prevLines
}

export function renderHexView(container, raw, prevLines = {}, options = {}) {
  const linesPerPage = options.linesPerPage || 32
  const diffFadeMs = options.diffFadeMs != null ? options.diffFadeMs : 2000
  const highlightRange = options.highlightRange || null

  if (!container) return
  if (raw.error) {
    container.innerHTML = '<div class="error">' + raw.error + '</div>'
    return
  }

  const data = raw.data || []
  const baseOffset = raw.offset != null ? raw.offset : 0
  const used = raw.used != null ? raw.used : 0

  const lines = bytesToHexLines(data, baseOffset, BYTES_PER_LINE)

  if (lines.length === 0) {
    if (container.replaceChildren) {
      const placeholder = document.createElement('div')
      placeholder.className = 'view-placeholder'
      placeholder.textContent = 'No data for this region.'
      container.replaceChildren(placeholder)
    } else {
      container.innerHTML = '<div class="view-placeholder">No data for this region.</div>'
    }
    return
  }

  const fragment = document.createDocumentFragment()
  lines.forEach((line) => {
    const lineStart = line.offset
    const lineEnd = lineStart + BYTES_PER_LINE
    const isHighlight = highlightRange && lineStart < highlightRange.end && lineEnd > highlightRange.start
    const isDiff = prevLines[lineStart] !== undefined && prevLines[lineStart] !== line.hex
    let cls = 'hex-line '
    if (lineEnd <= used) cls += 'hex-used'
    else if (lineStart >= used) cls += 'hex-free'
    else cls += 'hex-partial'
    if (isDiff) cls += ' hex-diff'
    if (isHighlight) cls += ' hex-line-highlight'

    const div = document.createElement('div')
    div.className = cls
    if (cls.includes('hex-partial')) {
      const pct = Math.round(((used - lineStart) / BYTES_PER_LINE) * 100)
      div.style.background =
        'linear-gradient(to right, rgba(35,134,54,0.28) 0%, rgba(35,134,54,0.28) ' +
        pct +
        '%, rgba(48,54,61,0.4) ' +
        pct +
        '%, rgba(48,54,61,0.4) 100%)'
    }
    div.innerHTML =
      '<span class="hex-offset">' +
      lineStart.toString(16).padStart(8, '0') +
      '</span> <span class="hex-bytes">' +
      line.hex +
      '</span> <span class="hex-ascii">' +
      line.ascii +
      '</span>'
    fragment.appendChild(div)
  })

  if (container.replaceChildren) {
    container.replaceChildren(fragment)
  } else {
    container.innerHTML = ''
    container.appendChild(fragment)
  }

  if (diffFadeMs > 0) {
    setTimeout(() => {
      container.querySelectorAll('.hex-line.hex-diff').forEach((el) => el.classList.remove('hex-diff'))
    }, diffFadeMs)
  }
}
