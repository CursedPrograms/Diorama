// The web game's physics (assets/web/physics.js, Jolt as WASM) on physicstest's scene, printed as physicstest prints
// the C++ one:   node tools/physicstest/test.js <folder with scene.json> [seconds]
'use strict';
const fs = require('fs');
const path = require('path');
const web = path.join(__dirname, '..', '..', 'assets', 'web');
const { pathToFileURL } = require('url');
const Physics = require(path.join(web, 'physics.js'));

const folder = process.argv[2] || '.';
const seconds = parseFloat(process.argv[3] || '3');
const proj = JSON.parse(fs.readFileSync(path.join(folder, 'scene.json'), 'utf8'));
const f4 = (v) => v.toFixed(4), f2 = (v) => v.toFixed(2);

import(pathToFileURL(path.join(web, 'jolt', 'jolt.js')).href).then((m) => m.default()).then((Jolt) => {
  const w = Physics.create(Jolt, proj, {});
  const objs = proj.markers[0].objects;
  const lists = [objs.map((o) => ({ o: o, on: true }))];
  const frames = Math.round(seconds * 60);
  const out = [];
  for (let fr = 1; fr <= frames; ++fr) {
    const t = fr / 60;
    w.step(1 / 60, t, lists);
    w.takeContacts().forEach((c) => out.push(`{"t": ${f4(t)}, "contact": [${c.a}, ${c.b}], "enter": ${c.enter}, "trigger": ${c.trigger}}`));
    if (fr % 30 === 0) {
      const parts = objs.map((o) => `"${o.name}": [${f4(o.position[0])}, ${f4(o.position[1])}, ${f4(o.position[2])}, ${f2(o.rotation[0])}, ${f2(o.rotation[1])}, ${f2(o.rotation[2])}]`);
      out.push(`{"t": ${f4(t)}, "objects": {${parts.join(', ')}}}`);
    }
  }
  const h = w.raycast(0, [0.3, 2, 0], [0, -1, 0], 10);
  out.push(h ? `{"raycast": {"id": ${h.id}, "floor": ${h.floor}, "y": ${f4(h.point[1])}, "distance": ${f4(h.distance)}}}` : '{"raycast": null}');
  out.push(`{"bodies": ${w.bodies()}}`);
  console.log(out.join('\n'));
});
