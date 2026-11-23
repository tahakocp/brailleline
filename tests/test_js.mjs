import { readFileSync } from 'node:fs';
import { crc16, encode, plan, asciiDots, Parser } from '../web/blcore.js';

const v = JSON.parse(readFileSync(process.argv[2], 'utf8'));
const unhex = (s) => Uint8Array.from(s.match(/../g) || [], (h) => parseInt(h, 16));
const hex = (a) => Array.from(a, (b) => b.toString(16).padStart(2, '0')).join('');
let checks = 0, fails = 0;
const eq = (a, b, what) => { checks++; if (JSON.stringify(a) !== JSON.stringify(b)) { fails++; console.log('FAIL', what, JSON.stringify(a), JSON.stringify(b)); } };

for (const t of v.crc) eq(crc16(unhex(t.data)), t.crc, 'crc ' + t.data);
for (const t of v.frames) {
  eq(hex(encode(t.cmd, Array.from(unhex(t.payload)))), t.frame, 'frame');
  const p = new Parser(); let got = [];
  for (const b of unhex(t.frame)) got = got.concat(p.feed(b));
  eq(got.length, 1, 'parse count');
  eq(hex(got[0].payload), t.payload, 'parse payload');
}
const asc = []; for (let c = 32; c < 127; c++) asc.push(asciiDots(String.fromCharCode(c)));
eq(hex(asc), v.ascii, 'ascii table');
for (const t of v.plans) {
  const s = plan(Array.from(unhex(t.cur)), Array.from(unhex(t.tgt)), t.conc, t.mode);
  eq(s.length, t.slots, 'slot count');
  eq(s.flat().map((p) => [p.cell, p.dot, p.up]), t.pulses, 'pulses');
}
console.log(`js: ${checks} checks against C vectors, ${fails} failed`);
process.exit(fails ? 1 : 0);
