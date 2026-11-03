// Runs the website's own calculator script (ias-calculator.html with its data file) on many setups and writes what the
// page shows, for tests/ias_calc_test.cpp to compare with the C++ port.
//   node tests/gen_ias_cases.js <ias-calculator.html> <ias-calculator-data.js> <cases.tsv> [count]
const fs = require('fs');
const vm = require('vm');

const [pagePath, dataPath, outPath, countArg] = process.argv.slice(2);
if (!outPath) { console.error('usage: node gen_ias_cases.js page.html data.js cases.tsv [count]'); process.exit(2); }
const count = +countArg || 3000;
const html = fs.readFileSync(pagePath, 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
let script = scripts.find(s => s.includes('var D = window.__DATA__'));
if (!script) { console.error('the calculator script was not found'); process.exit(1); }
script = script.replace(/update\(\);\s*\}\)\(\);\s*$/, 'update(); window.__update = update; })();');
if (!script.includes('window.__update')) { console.error('the script does not end the way the test expects'); process.exit(1); }

const elements = {};
function makeEl(id) {
  const el = {
    id, value: '', textContent: '', className: '', style: {}, hidden: false, disabled: false,
    options: [], children: [], childNodes: [], dataset: {}, title: '', placeholder: '', label: '',
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    addEventListener() {}, setAttribute() {}, querySelector() { return null; }, focus() {}, blur() {}, select() {},
    add(o) { this.options.push(o); },
    appendChild(c) { this.children.push(c); this.childNodes.push(c); if (c && c.__opt) this.options.push(c); return c; },
    get selectedIndex() {
      const all = this.options.concat(...this.children.map(c => c.options || []));
      return all.findIndex(o => o.value === this.value);
    },
    set innerHTML(v) { this._inner = v; if (v === '') { this.options = []; this.children = []; this.childNodes = []; } },
    get innerHTML() { return this._inner || ''; },
  };
  return el;
}
function getEl(id) { return elements[id] || (elements[id] = makeEl(id)); }
const document = {
  getElementById: getEl,
  createElement() { return makeEl(''); },
  activeElement: null,
};
function Option(text, value) { return { text, value: String(value === undefined ? text : value), disabled: false, __opt: true }; }
const window = {};
const context = vm.createContext({ window, document, Option, Event: function () {}, console, Math, String, Object, Array, JSON });
vm.runInContext(fs.readFileSync(dataPath, 'utf8'), context);
getEl('th-aps').textContent = 'Attacks / sec';
getEl('t-nomatch').textContent = 'Nothing matches.';
getEl('cls').value = '0'; getEl('form').value = ''; getEl('skill').value = 'standard'; getEl('wep').value = ''; getEl('wep2').value = '';
vm.runInContext(script, context);
const D = window.__DATA__;
const update = window.__update;

// a small deterministic generator
let seed = +process.env.SEED || 12345;
function rnd() { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; }
function pick(list) { return list[Math.floor(rnd() * list.length)]; }
function number(lo, hi, specials) {
  const r = rnd();
  if (r < 0.25) return 0;
  if (r < 0.4 && specials && specials.length) return pick(specials);
  return lo + Math.floor(rnd() * (hi - lo + 1));
}
const skills = ['standard', 'ww', 'strafe', 'frenzy', 'chargeup', 'cleave', 'mirrored', 'kick', 'traps', 'smite'];

function plain(html) { return String(html).replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim(); }
function rowsOf(tbody) {
  return tbody.children.map(tr => {
    const cells = [...tr.innerHTML.matchAll(/<div class="cell[^"]*">([^<]*)<\/div>/g)].map(m => m[1]);
    const state = tr.className === 'reached here' ? 2 : tr.className === 'next' ? 3 : tr.className === 'reached' ? 1 : 0;
    return [parseInt(cells[0], 10), cells[1], cells[2], cells[3], state].join('|');
  }).join('~');
}
function clean(s) { return String(s).replace(/[\t\r\n]+/g, ' '); }

const lines = [];
for (let n = 0; n < count; n++) {
  const c = {
    cls: Math.floor(rnd() * D.c.length),
    form: rnd() < 0.65 ? -1 : Math.floor(rnd() * D.f.length),
    skill: pick(skills),
    wep: rnd() < 0.1 ? -1 : Math.floor(rnd() * D.w.length),
    wep2: rnd() < 0.4 ? -1 : Math.floor(rnd() * D.w.length),
    ias: number(-40, 400, [-120, -119, -60, 120, 480]),
    wias: number(-40, 120, [-120, 30]),
    oias: number(-40, 120, [-120, 30]),
    ar: number(-60, 90, [-30, 30]),
    mar: number(-30, 40, []),
    oar: number(-30, 40, []),
    fcr: number(-130, 620, [-120, -55, -54, 480, 500]),
  };
  // bias towards setups where both hands are used, so the dual paths get exercised
  if (rnd() < 0.3) { c.cls = pick([1, 2]); c.skill = pick(['ww', 'frenzy', 'chargeup']); }
  getEl('cls').value = String(c.cls);
  getEl('form').value = c.form < 0 ? '' : String(c.form);
  getEl('skill').value = c.skill;
  getEl('wep').value = c.wep < 0 ? '' : String(c.wep);
  getEl('wep2').value = c.wep2 < 0 ? '' : String(c.wep2);
  for (const id of ['ias', 'wias', 'oias', 'ar', 'mar', 'oar', 'fcr']) getEl(id).value = String(c[id]);
  update(); update(); update();
  const g = id => getEl(id);
  const hasAnim = g('ro-anim').textContent !== '—';
  const fields = [
    c.cls, c.form, c.wep, c.wep2, c.skill, c.ias, c.wias, c.oias, c.ar, c.mar, c.oar, c.fcr,
    g('skill').value, g('wep2').value === '' ? -1 : g('wep2').value, hasAnim ? 1 : 0,
    hasAnim ? g('ro-anim').textContent : '', hasAnim ? g('ro-fpd').textContent : '', hasAnim ? g('ro-wsm').textContent : '',
    g('ro-total').textContent, g('ro-flat').textContent, hasAnim ? g('ro-eias').textContent : '', hasAnim ? (/warn/.test(g('ro-eias').className) ? 1 : 0) : 0,
    g('atk-fpa').textContent, g('atk-aps').textContent, hasAnim ? g('atk-solo').textContent : '', hasAnim ? g('atk-lbl').textContent : '',
    hasAnim ? g('atk-lbl2').textContent : '', g('th-atk').textContent, hasAnim ? g('th-aps').textContent : '',
    rowsOf(g('atk-body')), plain(g('atk-note').innerHTML),
    g('cast-fpa').textContent, g('cast-cps').textContent, rowsOf(g('cast-body')), plain(g('cast-note').innerHTML),
    g('wrap-wias').style.display === 'none' ? 0 : 1, g('wep2').disabled ? 0 : 1,
    plain(g('hint-ias').innerHTML), g('hint-wias').innerHTML, g('wep2hint').textContent, g('formhint').textContent,
  ];
  lines.push(fields.map(clean).join('\t'));
}
fs.writeFileSync(outPath, lines.join('\n') + '\n');
console.log('wrote ' + lines.length + ' cases');
