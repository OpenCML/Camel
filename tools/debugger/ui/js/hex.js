/**
 * Hex view: format raw bytes into lines (hex + ASCII) and render with optional diff highlight.
 * API returns { data: number[], offset, capacity, used }; this module does all formatting.
 */

const BYTES_PER_LINE = 16;

/**
 * Format one byte as 2-char hex.
 * @param {number} b 0..255
 * @returns {string}
 */
function byteToHex(b) {
  return ('0' + (Number(b) & 0xff).toString(16)).slice(-2);
}

/**
 * Format one byte as ASCII char or '.'.
 * @param {number} b 0..255
 * @returns {string}
 */
function byteToAscii(b) {
  const n = Number(b) & 0xff;
  return n >= 32 && n < 127 ? String.fromCharCode(n) : '.';
}

/**
 * Convert raw byte array to lines for hex view.
 * @param {number[]} data - Array of byte values 0..255
 * @param {number} baseOffset - Start offset of this chunk (for display)
 * @param {number} [bytesPerLine=16]
 * @returns {{ offset: number, hex: string, ascii: string }[]}
 */
export function bytesToHexLines(data, baseOffset, bytesPerLine = BYTES_PER_LINE) {
  const lines = [];
  if (!Array.isArray(data) || data.length === 0) return lines;
  for (let i = 0; i < data.length; i += bytesPerLine) {
    const lineOffset = baseOffset + i;
    const chunk = data.slice(i, i + bytesPerLine);
    let hex = '';
    let ascii = '';
    for (let j = 0; j < bytesPerLine; j++) {
      if (j < chunk.length) {
        hex += (j > 0 ? ' ' : '') + byteToHex(chunk[j]);
        ascii += byteToAscii(chunk[j]);
      } else {
        hex += (j > 0 ? ' ' : '') + '  ';
        ascii += ' ';
      }
    }
    lines.push({ offset: lineOffset, hex: hex.trim(), ascii });
  }
  return lines;
}

/**
 * Build prevLines map from current container DOM (offset -> hex string).
 * @param {Element} container
 * @returns {Object<number, string>}
 */
export function getPrevLinesFromContainer(container) {
  const prevLines = {};
  if (!container) return prevLines;
  container.querySelectorAll('.hex-line').forEach((div) => {
    const spans = div.querySelectorAll('.hex-offset, .hex-bytes');
    if (spans.length >= 2) {
      const offset = parseInt(spans[0].textContent.trim(), 16);
      const hex = (spans[1].textContent || '').trim();
      prevLines[offset] = hex;
    }
  });
  return prevLines;
}

/**
 * Render hex view into container from raw API response.
 * @param {Element} container
 * @param {{ error?: string, data?: number[], offset?: number, capacity?: number, used?: number }} raw - API response
 * @param {Object<number, string>} prevLines - offset -> hex string for diff
 * @param {{ linesPerPage?: number, diffFadeMs?: number }} [options]
 */
export function renderHexView(container, raw, prevLines = {}, options = {}) {
  const linesPerPage = options.linesPerPage || 32;
  const diffFadeMs = options.diffFadeMs != null ? options.diffFadeMs : 2000;
  const highlightRange = options.highlightRange || null;

  if (raw.error) {
    container.innerHTML = '<div class="error">' + raw.error + '</div>';
    return;
  }

  const data = raw.data || [];
  const baseOffset = raw.offset != null ? raw.offset : 0;
  const used = raw.used != null ? raw.used : 0;
  const capacity = raw.capacity != null ? raw.capacity : 0;

  const lines = bytesToHexLines(data, baseOffset, BYTES_PER_LINE);
  container.innerHTML = '';

  if (lines.length === 0) {
    container.innerHTML = '<div class="view-placeholder">No data for this region.</div>';
    return;
  }

  lines.forEach((line) => {
    const lineStart = line.offset;
    const lineEnd = lineStart + BYTES_PER_LINE;
    const isHighlight =
      highlightRange &&
      lineStart < highlightRange.end &&
      lineEnd > highlightRange.start;
    const isDiff = prevLines[lineStart] !== undefined && prevLines[lineStart] !== line.hex;
    let cls = 'hex-line ';
    if (lineEnd <= used) cls += 'hex-used';
    else if (lineStart >= used) cls += 'hex-free';
    else {
      cls += 'hex-partial';
      const pct = Math.round(((used - lineStart) / BYTES_PER_LINE) * 100);
      // applied below via style
    }
    if (isDiff) cls += ' hex-diff';
    if (isHighlight) cls += ' hex-line-highlight';

    const div = document.createElement('div');
    div.className = cls;
    if (cls.includes('hex-partial')) {
      const pct = Math.round(((used - lineStart) / BYTES_PER_LINE) * 100);
      div.style.background =
        'linear-gradient(to right, rgba(35,134,54,0.2) 0%, rgba(35,134,54,0.2) ' +
        pct +
        '%, rgba(48,54,61,0.4) ' +
        pct +
        '%, rgba(48,54,61,0.4) 100%)';
    }
    div.innerHTML =
      '<span class="hex-offset">' +
      lineStart.toString(16).padStart(8, '0') +
      '</span> <span class="hex-bytes">' +
      line.hex +
      '</span> <span class="hex-ascii">' +
      line.ascii +
      '</span>';
    container.appendChild(div);
  });

  if (diffFadeMs > 0) {
    setTimeout(() => {
      container.querySelectorAll('.hex-line.hex-diff').forEach((el) => el.classList.remove('hex-diff'));
    }, diffFadeMs);
  }
}
