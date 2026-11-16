// FivePocket block lettering: our own capitals, drawn for the builder's logo. Each letter is a set of centre lines on
// a 10-unit-high grid, stroked heavy (2.8 units, rounded joins); the logo draws every letter's black outline first and
// then the white letters, so the outlines merge like a sticker. Only the letters the builder needs are drawn.
'use strict';

const LETTER_STROKE = 2.8;
const LETTER_OUTLINE = 1.1;          // the black rim on each side
const T = 1.4, B = 8.6, M = 5;       // top, bottom and middle centre lines (the strokes' outer edges land on 0 and 10)

// width (outer, units) and centre-line paths, x measured from the letter's left edge
const LETTERS = {
  A: [7.0, (L, R) => `M${L} ${B}V${T + 1.2}Q${L} ${T} ${L + 1.2} ${T}H${R - 1.2}Q${R} ${T} ${R} ${T + 1.2}V${B}M${L} ${M + 0.4}H${R}`],
  C: [6.4, (L, R) => `M${R} ${T}H${L + 1.2}Q${L} ${T} ${L} ${T + 1.2}V${B - 1.2}Q${L} ${B} ${L + 1.2} ${B}H${R}`],
  D: [7.0, (L, R) => `M${L} ${T}V${B}H${R - 1.6}Q${R} ${B} ${R} ${B - 1.6}V${T + 1.6}Q${R} ${T} ${R - 1.6} ${T}Z`],
  E: [6.0, (L, R) => `M${R} ${T}H${L}V${B}H${R}M${L} ${M}H${R - 0.6}`],
  F: [6.0, (L, R) => `M${L} ${B}V${T}H${R}M${L} ${M}H${R - 0.8}`],
  H: [7.0, (L, R) => `M${L} ${T}V${B}M${R} ${T}V${B}M${L} ${M}H${R}`],
  I: [2.8, (L) => `M${L} ${T}V${B}`],
  K: [6.9, (L, R) => `M${L} ${T}V${B}M${R} ${T}L${L + 0.4} ${M}L${R} ${B}`],
  N: [7.2, (L, R) => `M${L} ${B}V${T}L${R} ${B}V${T}`],
  O: [7.0, (L, R) => `M${L + 1.4} ${T}H${R - 1.4}Q${R} ${T} ${R} ${T + 1.4}V${B - 1.4}Q${R} ${B} ${R - 1.4} ${B}H${L + 1.4}Q${L} ${B} ${L} ${B - 1.4}V${T + 1.4}Q${L} ${T} ${L + 1.4} ${T}Z`],
  P: [6.5, (L, R) => `M${L} ${B}V${T}H${R - 1}Q${R} ${T} ${R} ${T + 1}V${M - 0.6}Q${R} ${M + 0.4} ${R - 1} ${M + 0.4}H${L}`],
  R: [6.9, (L, R) => `M${L} ${B}V${T}H${R - 1}Q${R} ${T} ${R} ${T + 1}V${M - 0.6}Q${R} ${M + 0.4} ${R - 1} ${M + 0.4}H${L}M${L + 2.2} ${M + 0.4}L${R} ${B}`],
  S: [6.4, (L, R) => `M${R} ${T}H${L + 1}Q${L} ${T} ${L} ${T + 1}V${M - 0.9}Q${L} ${M} ${L + 1} ${M}H${R - 1}Q${R} ${M} ${R} ${M + 1}V${B - 1}Q${R} ${B} ${R - 1} ${B}H${L}`],
  T: [7.0, (L, R) => `M${L} ${T}H${R}M${(L + R) / 2} ${T}V${B}`],
  U: [7.0, (L, R) => `M${L} ${T}V${B - 1.4}Q${L} ${B} ${L + 1.4} ${B}H${R - 1.4}Q${R} ${B} ${R} ${B - 1.4}V${T}`],
  V: [7.4, (L, R) => `M${L} ${T}L${(L + R) / 2} ${B}L${R} ${T}`],
  i: [2.8, (L) => `M${L} ${T}V${T + 0.05}M${L} ${M + 0.3}V${B}`],
  '+': [5.0, (L, R) => `M${L} ${M}H${R}M${(L + R) / 2} ${M - (R - L) / 2}V${M + (R - L) / 2}`],
  ' ': [1.6, () => ''],
};
const LETTER_GAP = 1.0;

// the width of a word in units
function wordWidth(word) {
  let w = 0;
  for (const ch of word) w += (LETTERS[ch] || LETTERS[ch.toUpperCase()] || LETTERS[' '])[0] + LETTER_GAP;
  return Math.max(0, w - LETTER_GAP);
}

// the centre-line path of a word, its left edge at x 0 (units)
function wordPath(word) {
  let x = 0, d = '';
  const h = LETTER_STROKE / 2;
  for (const ch of word) {
    const [w, draw] = LETTERS[ch] || LETTERS[ch.toUpperCase()] || LETTERS[' '];
    d += draw(+(x + h).toFixed(2), +(x + w - h).toFixed(2));
    x += w + LETTER_GAP;
  }
  return d;
}

// draws lines of words into an SVG group: every outline first, then every letter; returns the height used
function drawLettering(group, lines, { width, unit, gap }) {
  const NS = 'http://www.w3.org/2000/svg';
  group.replaceChildren();
  const fits = lines.filter((l) => l).map((l) => Math.min(unit, width / (wordWidth(l) + 2 * LETTER_OUTLINE)));
  const scale = Math.min(...fits.slice(0, 2));       // the name's two lines share one size; the edition line may shrink
  const layers = { rim: [], face: [] };
  let y = 0;
  lines.forEach((line, i) => {
    if (!line) return;
    const s = i < 2 ? scale : Math.min(scale, fits[i]);
    const d = wordPath(line);
    const at = `translate(${(LETTER_OUTLINE * s).toFixed(2)} ${(y + LETTER_OUTLINE * s).toFixed(2)}) scale(${s.toFixed(3)})`;
    layers.rim.push([d, at]);
    layers.face.push([d, at]);
    y += (10 + 2 * LETTER_OUTLINE) * s + gap;
  });
  for (const [layer, colour, extra] of [['rim', '#0e0e0e', 2 * LETTER_OUTLINE], ['face', '#ffffff', 0]]) {
    for (const [d, at] of layers[layer]) {
      const p = document.createElementNS(NS, 'path');
      p.setAttribute('d', d);
      p.setAttribute('transform', at);
      p.setAttribute('fill', 'none');
      p.setAttribute('stroke', colour);
      p.setAttribute('stroke-width', String(LETTER_STROKE + extra));
      p.setAttribute('stroke-linejoin', 'round');
      p.setAttribute('stroke-linecap', layer === 'rim' ? 'round' : 'square');
      group.append(p);
    }
  }
  return y - gap;
}
