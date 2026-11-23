// JavaScript port of src/brailleline.c (same scheduler, same frames).
// tests/test_js.mjs checks it against vectors produced by the C code.

export function crc16(bytes) {
  let c = 0xffff;
  for (const b of bytes) {
    c ^= b << 8;
    for (let i = 0; i < 8; i++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xffff : (c << 1) & 0xffff;
  }
  return c;
}

export function encode(cmd, payload = []) {
  const body = [cmd, payload.length, ...payload];
  const c = crc16(body);
  return Uint8Array.from([0xa5, ...body, c >> 8, c & 255]);
}

export function popcount8(v) { let n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
export function concurrent(budgetMa, pulseMa) { return pulseMa <= 0 ? 1 : Math.max(1, Math.floor(budgetMa / pulseMa)); }

// mode 0: fewest slots (largest backlog first), mode 1: reading order (leftmost first)
export function plan(cur, tgt, conc, mode = 0) {
  const n = cur.length;
  const diff = Array.from(cur, (c, i) => c ^ tgt[i]);
  const rem = diff.map(popcount8);
  let total = rem.reduce((a, b) => a + b, 0);
  const slots = [];
  while (total > 0) {
    const taken = new Array(n).fill(false);
    const slot = [];
    for (let k = 0; k < conc; k++) {
      let best = -1;
      for (let i = 0; i < n; i++) {
        if (taken[i] || !rem[i]) continue;
        if (best < 0) best = i;
        else if (mode === 0 && rem[i] > rem[best]) best = i;
        if (mode === 1) break;
      }
      if (best < 0) break;
      taken[best] = true;
      let d = 0; while (!((diff[best] >> d) & 1)) d++;
      diff[best] &= ~(1 << d); rem[best]--; total--;
      slot.push({ cell: best, dot: d, up: (tgt[best] >> d) & 1 });
    }
    slots.push(slot);
  }
  return slots;
}

const TABLE = " A1B'K2L@CIF/MSP\"E3H9O6R^DJG>NTQ,*5<-U8V.%[$+X!&;:4\\0Z7(_?W]#Y)=";
export function asciiDots(ch) {
  const c = ch.toUpperCase();
  const i = TABLE.indexOf(c);
  return i < 0 ? 0 : i;
}
export function textToCells(text, n = 40) {
  const out = new Array(n).fill(0);
  for (let i = 0; i < Math.min(n, text.length); i++) out[i] = asciiDots(text[i]);
  return out;
}
export const unicodeCell = (v) => String.fromCodePoint(0x2800 + v);

export class Parser {
  constructor() { this.buf = []; this.crcErrors = 0; }
  feed(b) {
    const out = [];
    this._step(b, out);
    return out;
  }
  idle() { const out = []; this._rescan(out); return out; }
  _rescan(out) {
    const tmp = this.buf.slice(1); this.buf = [];
    for (const x of tmp) this._step(x, out);
  }
  _step(b, out) {
    if (!this.buf.length) { if (b === 0xa5) this.buf.push(b); return; }
    this.buf.push(b);
    if (this.buf.length === 3 && this.buf[2] > 96) { this.crcErrors++; this._rescan(out); return; }
    if (this.buf.length >= 3 && this.buf.length === this.buf[2] + 5) {
      const len = this.buf[2];
      const want = crc16(this.buf.slice(1, 3 + len));
      const got = (this.buf[3 + len] << 8) | this.buf[4 + len];
      if (want === got) { out.push({ cmd: this.buf[1], payload: this.buf.slice(3, 3 + len) }); this.buf = []; }
      else { this.crcErrors++; this._rescan(out); }
    }
  }
}
