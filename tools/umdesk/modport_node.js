// -*- coding: utf-8 -*-
/*
 * The web converter (other stuff/usermods/modport-web/src/modport.js) run by node on the desk
 * (04.10.2026, tools/umdesk/test_converters.py): the page's own file, unchanged, with what a browser
 * gives it put in by hand -
 *   DOMParser / XMLSerializer   a strict XML reader building a small DOM (attributes in their order,
 *                               text nodes; comments, PIs and the DOCTYPE dropped) that refuses what
 *                               expat refuses (a bare &, a < inside a value, a repeated attribute, an
 *                               undefined entity, junk after the root), as the browser's parser does;
 *   createImageBitmap, OffscreenCanvas, ImageData
 *                               PNG read to RGBA as PIL's convert('RGBA') reads it (the converter's
 *                               rules are what is tested, not a browser's decoder) and written back.
 *
 *   node tools/umdesk/modport_node.js <gameref.bin> <mod zip> <out zip>
 * writes <out zip> (none when there is nothing for the port) and <out zip>.report.txt.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = path.join(__dirname, '..', '..', 'other stuff', 'usermods', 'modport-web', 'src');
globalThis.window = globalThis;
globalThis.self = globalThis;
const fflate = require(path.join(SRC, 'fflate.js'));
globalThis.fflate = fflate;

/* ------------------------------------------------------------------ a small DOM */

class Node_ {
  constructor(type, doc) { this.nodeType = type; this.ownerDocument = doc; this.parentNode = null; this.nextSibling = null; this.previousSibling = null; }
}
class Text_ extends Node_ {
  constructor(v, doc) { super(3, doc); this.nodeValue = v; }
  get textContent() { return this.nodeValue; }
  cloneNode() { return new Text_(this.nodeValue, this.ownerDocument); }
}
class Element_ extends Node_ {
  constructor(tag, doc) { super(1, doc); this.tagName = tag; this._attrs = []; this.childNodes = []; }
  get attributes() { return this._attrs.map(a => ({ name: a[0], value: a[1] })); }
  getAttribute(k) { const a = this._attrs.find(x => x[0] === k); return a ? a[1] : null; }
  hasAttribute(k) { return this._attrs.some(x => x[0] === k); }
  setAttribute(k, v) { const a = this._attrs.find(x => x[0] === k); if (a) a[1] = String(v); else this._attrs.push([k, String(v)]); }
  get firstElementChild() { return this.childNodes.find(c => c.nodeType === 1) || null; }
  get firstChild() { return this.childNodes[0] || null; }
  get children() { return this.childNodes.filter(c => c.nodeType === 1); }
  get textContent() { return this.childNodes.map(c => c.textContent).join(''); }
  appendChild(c) {
    if (c.parentNode) c.parentNode.removeChild(c);
    const last = this.childNodes[this.childNodes.length - 1];
    if (last) { last.nextSibling = c; c.previousSibling = last; } else c.previousSibling = null;
    c.nextSibling = null;
    c.parentNode = this;
    this.childNodes.push(c);
    return c;
  }
  removeChild(c) {
    const i = this.childNodes.indexOf(c);
    if (i < 0) return c;
    this.childNodes.splice(i, 1);
    if (c.previousSibling) c.previousSibling.nextSibling = c.nextSibling;
    if (c.nextSibling) c.nextSibling.previousSibling = c.previousSibling;
    c.parentNode = c.nextSibling = c.previousSibling = null;
    return c;
  }
  cloneNode(deep) {
    const e = new Element_(this.tagName, this.ownerDocument);
    e._attrs = this._attrs.map(a => [a[0], a[1]]);
    if (deep) for (const c of this.childNodes) e.appendChild(c.cloneNode(true));
    return e;
  }
  getElementsByTagName(t) {
    const out = [];
    const walk = (e) => { for (const c of e.childNodes) if (c.nodeType === 1) { if (t === '*' || c.tagName === t) out.push(c); walk(c); } };
    walk(this);
    return out;
  }
}
class Document_ {
  constructor() { this.documentElement = null; this.nodeType = 9; }
  createElement(t) { return new Element_(t, this); }
  getElementsByTagName(t) {
    if (!this.documentElement) return [];
    const out = (t === '*' || this.documentElement.tagName === t) ? [this.documentElement] : [];
    return out.concat(this.documentElement.getElementsByTagName(t));
  }
}

function xmlChar(c) { return c === 9 || c === 10 || c === 13 || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF); }
function nameStart(c) { return c === '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c.charCodeAt(0) >= 0xC0 && c !== '×' && c !== '÷'); }
function nameChar(c) { return nameStart(c) || c === '-' || c === '.' || (c >= '0' && c <= '9') || c === '·'; }

function parseStrict(s) {
  const doc = new Document_();
  let p = 0;
  const bad = (why) => { throw new Error(why + ' at ' + p); };
  const at = (t) => s.startsWith(t, p);
  const ws = () => { while (p < s.length && ' \t\n\r'.indexOf(s[p]) >= 0) p++; };
  const name = () => {
    const a = p;
    if (p >= s.length || !nameStart(s[p])) bad('not well-formed');
    p++;
    while (p < s.length && (nameChar(s[p]) || s[p] === ':')) p++;
    const n = s.slice(a, p);
    if (n.indexOf(':') >= 0) bad('unbound prefix');
    return n;
  };
  const ref = () => {
    const semi = s.indexOf(';', p);
    if (semi < 0) bad('not well-formed');
    const body = s.slice(p + 1, semi);
    let v;
    if (body[0] === '#') {
      let cp;
      if (body[1] === 'x') { if (!/^#x[0-9A-Fa-f]+$/.test(body)) bad('not well-formed'); cp = parseInt(body.slice(2), 16); }
      else { if (!/^#[0-9]+$/.test(body)) bad('not well-formed'); cp = parseInt(body.slice(1), 10); }
      if (!xmlChar(cp)) bad('reference to invalid character number');
      v = String.fromCodePoint(cp);
    } else {
      if (!body || !nameStart(body[0]) || ![...body.slice(1)].every(nameChar)) bad('not well-formed');
      v = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" }[body];
      if (v === undefined) bad('undefined entity');
    }
    p = semi + 1;
    return v;
  };
  const misc = () => {
    for (;;) {
      ws();
      if (at('<!--')) { const e = s.indexOf('-->', p + 4); if (e < 0) bad('unclosed'); p = e + 3; }
      else if (at('<?')) { const e = s.indexOf('?>', p + 2); if (e < 0) bad('unclosed'); p = e + 2; }
      else if (at('<!DOCTYPE')) { let d = 0; while (p < s.length) { const c = s[p++]; if (c === '[') d++; else if (c === ']') d--; else if (c === '>' && d <= 0) break; } }
      else return;
    }
  };
  misc();
  if (p >= s.length || s[p] !== '<') bad('no element found');
  const stack = [];
  const text = (str) => { const top = stack[stack.length - 1]; const last = top.childNodes[top.childNodes.length - 1]; if (last && last.nodeType === 3) last.nodeValue += str; else top.appendChild(new Text_(str, doc)); };
  for (;;) {
    if (p >= s.length) bad('unclosed token');
    const c = s[p];
    if (c === '<') {
      if (at('</')) {
        p += 2;
        const n = name();
        ws();
        if (s[p] !== '>') bad('not well-formed');
        p++;
        if (stack[stack.length - 1].tagName !== n) bad('mismatched tag');
        stack.pop();
        if (!stack.length) break;
        continue;
      }
      if (at('<!--')) { const e = s.indexOf('-->', p + 4); if (e < 0) bad('unclosed'); p = e + 3; continue; }
      if (at('<![CDATA[')) {
        if (!stack.length) bad('not well-formed');
        const e = s.indexOf(']]>', p + 9);
        if (e < 0) bad('unclosed CDATA');
        text(s.slice(p + 9, e).replace(/\r\n/g, '\n').replace(/\r/g, '\n'));
        p = e + 3;
        continue;
      }
      if (at('<?')) { const e = s.indexOf('?>', p + 2); if (e < 0) bad('unclosed'); p = e + 2; continue; }
      if (at('<!')) bad('not well-formed');
      p++;
      const el = doc.createElement(name());
      for (;;) {
        const before = p;
        ws();
        if (p >= s.length) bad('unclosed token');
        if (s[p] === '>' || s[p] === '/') break;
        if (p === before) bad('not well-formed');
        const k = name();
        ws();
        if (s[p] !== '=') bad('not well-formed');
        p++;
        ws();
        const q = s[p];
        if (q !== '"' && q !== "'") bad('not well-formed');
        p++;
        let v = '';
        for (;;) {
          if (p >= s.length) bad('unclosed token');
          const x = s[p];
          if (x === q) { p++; break; }
          if (x === '<') bad('not well-formed');
          if (x === '&') { v += ref(); continue; }
          const cp = s.codePointAt(p);
          if (!xmlChar(cp)) bad('not well-formed');
          if (x === '\r') { v += ' '; p++; if (s[p] === '\n') p++; continue; }
          if (x === '\n' || x === '\t') { v += ' '; p++; continue; }
          v += String.fromCodePoint(cp);
          p += cp > 0xffff ? 2 : 1;
        }
        if (el.hasAttribute(k)) bad('duplicate attribute');
        el._attrs.push([k, v]);
      }
      const empty = s[p] === '/';
      if (empty) { p++; if (s[p] !== '>') bad('not well-formed'); }
      p++;
      if (!stack.length) doc.documentElement = el; else stack[stack.length - 1].appendChild(el);
      if (empty) { if (!stack.length) break; continue; }
      stack.push(el);
      continue;
    }
    if (!stack.length) bad('not well-formed');
    if (c === '&') { text(ref()); continue; }
    if (at(']]>')) bad('not well-formed');
    const cp = s.codePointAt(p);
    if (!xmlChar(cp)) bad('not well-formed');
    if (c === '\r') { text('\n'); p++; if (s[p] === '\n') p++; continue; }
    text(String.fromCodePoint(cp));
    p += cp > 0xffff ? 2 : 1;
  }
  misc();
  if (p < s.length) bad('junk after document element');
  return doc;
}

globalThis.DOMParser = class {
  parseFromString(text) {
    try { return parseStrict(text); } catch (e) {
      const d = new Document_();
      d.documentElement = d.createElement('parsererror');
      d.documentElement.appendChild(new Text_(String(e.message), d));
      return d;
    }
  }
};

function escText(t) { return t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
function escAttr(t) { return escText(t).replace(/"/g, '&quot;').replace(/\r/g, '&#13;').replace(/\n/g, '&#10;').replace(/\t/g, '&#9;'); }
function ser(n) {
  if (n.nodeType === 3) return escText(n.nodeValue);
  let s = '<' + n.tagName;
  for (const a of n._attrs) s += ' ' + a[0] + '="' + escAttr(a[1]) + '"';
  if (!n.childNodes.length) return s + '/>';
  return s + '>' + n.childNodes.map(ser).join('') + '</' + n.tagName + '>';
}
globalThis.XMLSerializer = class { serializeToString(n) { return ser(n); } };

/* ------------------------------------------------------------------ pictures */

function be32(d, o) { return ((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]) >>> 0; }

function decodePng(d) {
  if (d[0] !== 0x89 || d[1] !== 0x50) throw new Error('not a png');
  let p = 8, w = 0, h = 0, depth = 0, color = 0, inter = 0, plte = null, trns = null;
  const idat = [];
  while (p + 8 <= d.length) {
    const n = be32(d, p), t = String.fromCharCode(d[p + 4], d[p + 5], d[p + 6], d[p + 7]), at = p + 8;
    if (t === 'IHDR') { w = be32(d, at); h = be32(d, at + 4); depth = d[at + 8]; color = d[at + 9]; inter = d[at + 12]; }
    else if (t === 'PLTE') plte = d.subarray(at, at + n);
    else if (t === 'tRNS') trns = d.subarray(at, at + n);
    else if (t === 'IDAT') idat.push(d.subarray(at, at + n));
    p = at + n + 4;
    if (t === 'IEND') break;
  }
  let len = 0; for (const c of idat) len += c.length;
  const z = new Uint8Array(len); let o = 0; for (const c of idat) { z.set(c, o); o += c.length; }
  const raw = fflate.unzlibSync(z);
  const ch = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[color], bpp = ch * depth, bpx = Math.max(1, bpp >> 3);
  const out = new Uint8ClampedArray(w * h * 4);
  let rp = 0;
  const pass = (x0, y0, dx, dy) => {
    const pw = Math.ceil((w - x0) / dx), ph = Math.ceil((h - y0) / dy);
    if (pw <= 0 || ph <= 0) return;
    const stride = Math.ceil(pw * bpp / 8);
    let prev = new Uint8Array(stride);
    for (let y = 0; y < ph; y++) {
      const f = raw[rp++], cur = raw.slice(rp, rp + stride); rp += stride;
      for (let i = 0; i < stride; i++) {
        const a = i >= bpx ? cur[i - bpx] : 0, b = prev[i], c = i >= bpx ? prev[i - bpx] : 0;
        let v = cur[i];
        if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
        else if (f === 4) { const q = a + b - c, pa = Math.abs(q - a), pb = Math.abs(q - b), pc = Math.abs(q - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
        cur[i] = v & 255;
      }
      const yy = y0 + y * dy;
      for (let x = 0; x < pw; x++) {
        const xx = x0 + x * dx, i = (yy * w + xx) * 4;
        const smp = (k) => { // a sample of the k-th channel
          if (depth === 8) return cur[x * ch + k];
          if (depth === 16) return cur[(x * ch + k) * 2];
          const per = 8 / depth, by = cur[Math.floor(x / per)], sh = 8 - depth * (x % per + 1);
          return (by >> sh) & ((1 << depth) - 1);
        };
        let r, g, b, al = 255;
        if (color === 0) {
          const v = smp(0), l = depth === 1 ? v * 255 : depth === 2 ? v * 85 : depth === 4 ? v * 17 : v;
          r = g = b = l;
          if (trns && trns.length >= 2 && l === ((trns[0] << 8) | trns[1])) al = 0;
        } else if (color === 2) {
          r = smp(0); g = smp(1); b = smp(2);
          if (trns && trns.length >= 6 && r === ((trns[0] << 8) | trns[1]) && g === ((trns[2] << 8) | trns[3]) && b === ((trns[4] << 8) | trns[5])) al = 0;
        } else if (color === 3) {
          const k = smp(0); r = g = b = 0;
          if (plte && k * 3 + 2 < plte.length) { r = plte[k * 3]; g = plte[k * 3 + 1]; b = plte[k * 3 + 2]; }
          if (trns && k < trns.length) al = trns[k];
        } else if (color === 4) { r = g = b = smp(0); al = smp(1); }
        else { r = smp(0); g = smp(1); b = smp(2); al = smp(3); }
        out[i] = r; out[i + 1] = g; out[i + 2] = b; out[i + 3] = al;
      }
      prev = cur;
    }
  };
  if (inter === 1) for (const ps of [[0, 0, 8, 8], [4, 0, 8, 8], [0, 4, 4, 8], [2, 0, 4, 4], [0, 2, 2, 4], [1, 0, 2, 2], [0, 1, 1, 2]]) pass(...ps);
  else pass(0, 0, 1, 1);
  return { width: w, height: h, data: out };
}

const CRC = (() => { const t = new Int32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c; } return t; })();
function crc32(parts) { let c = -1; for (const d of parts) for (let i = 0; i < d.length; i++) c = CRC[(c ^ d[i]) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; }
function encodePng(w, h, data) {
  const raw = new Uint8Array((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; raw.set(data.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1); }
  const chunk = (t, d) => {
    const head = new Uint8Array(8), tb = new TextEncoder().encode(t);
    new DataView(head.buffer).setUint32(0, d.length); head.set(tb, 4);
    const tail = new Uint8Array(4); new DataView(tail.buffer).setUint32(0, crc32([tb, d]));
    return [head, d, tail];
  };
  const ihdr = new Uint8Array(13); const dv = new DataView(ihdr.buffer); dv.setUint32(0, w); dv.setUint32(4, h); ihdr[8] = 8; ihdr[9] = 6;
  const parts = [new Uint8Array([0x89, 0x50, 0x4e, 0x47, 13, 10, 26, 10])].concat(chunk('IHDR', ihdr), chunk('IDAT', fflate.zlibSync(raw)), chunk('IEND', new Uint8Array(0)));
  return new Blob(parts, { type: 'image/png' });
}

globalThis.ImageData = class { constructor(data, w, h) { this.data = data; this.width = w; this.height = h; } };
globalThis.createImageBitmap = async function (blob) {
  const d = decodePng(new Uint8Array(await blob.arrayBuffer()));
  return { width: d.width, height: d.height, _data: d.data, close() {} };
};
globalThis.OffscreenCanvas = class {
  constructor(w, h) { this.width = w; this.height = h; this._px = null; }
  getContext() {
    const cv = this;
    const px = () => (cv._px || (cv._px = new Uint8ClampedArray(cv.width * cv.height * 4)));
    return {
      imageSmoothingEnabled: true,
      drawImage(bmp, x, y, dw, dh) {
        dw = dw === undefined ? bmp.width : dw; dh = dh === undefined ? bmp.height : dh;
        if (this.imageSmoothingEnabled && (dw !== bmp.width || dh !== bmp.height)) throw new Error('a smoothed scaling the converter never asks for');
        const o = px();
        for (let yy = 0; yy < dh; yy++) for (let xx = 0; xx < dw; xx++) {
          const sx = Math.floor(xx * bmp.width / dw), sy = Math.floor(yy * bmp.height / dh), si = (sy * bmp.width + sx) * 4, di = ((y + yy) * cv.width + x + xx) * 4;
          o[di] = bmp._data[si]; o[di + 1] = bmp._data[si + 1]; o[di + 2] = bmp._data[si + 2]; o[di + 3] = bmp._data[si + 3];
        }
      },
      getImageData(x, y, w, h) {
        const o = px(), out = new Uint8ClampedArray(w * h * 4);
        for (let yy = 0; yy < h; yy++) out.set(o.subarray(((y + yy) * cv.width + x) * 4, ((y + yy) * cv.width + x + w) * 4), yy * w * 4);
        return new ImageData(out, w, h);
      },
      putImageData(img, x, y) {
        const o = px();
        for (let yy = 0; yy < img.height; yy++) o.set(img.data.subarray(yy * img.width * 4, (yy + 1) * img.width * 4), ((y + yy) * cv.width + x) * 4);
      }
    };
  }
  async convertToBlob() { return encodePng(this.width, this.height, this._px || new Uint8ClampedArray(this.width * this.height * 4)); }
};

/* ------------------------------------------------------------------ run */

vm.runInThisContext(fs.readFileSync(path.join(SRC, 'modport.js'), 'utf8'), { filename: 'modport.js' });

(async () => {
  const [refPath, zipPath, outPath] = process.argv.slice(2);
  const t0 = Date.now();
  const ref = window.ModPort.loadRef(new Uint8Array(fs.readFileSync(refPath)));
  const buf = fs.readFileSync(zipPath);
  const file = new File([buf], path.basename(zipPath));
  const out = await window.ModPort.convert(await window.ModPort.sourceFromZip(file), ref);
  fs.writeFileSync(outPath + '.report.txt', out.report.text());
  if (fs.existsSync(outPath)) fs.unlinkSync(outPath);
  if (out.blob) fs.writeFileSync(outPath, Buffer.from(await out.blob.arrayBuffer()));
  console.log('js: ' + (out.blob ? out.files + ' files' : 'no zip (nothing for the port)') + ', id ' + out.manifest.id + ', ' + (Date.now() - t0) + ' ms');
})().catch(e => { console.error(e && e.stack || e); process.exit(1); });
