/* Compare the firmware's LED choices (written by host_test) with the live
 * preview's engine on the same vehicles.
 *
 *   node crosscheck.js <live-preview dir> <ledmap.json> <out_dir>
 *
 * The board works in float32 and the preview in double, so a vehicle that sits
 * almost exactly between two LEDs (or between two equally near segments) can
 * legitimately go either way. Those near-ties are reported but not failures;
 * any other disagreement is. */
'use strict';
const fs = require('fs'), path = require('path');
const [prevDir, mapPath, outDir] = process.argv.slice(2);
const Geo = require(path.join(prevDir, 'js/geo.js'));
const { LedMap } = require(path.join(prevDir, 'js/model.js'));
const { Engine } = require(path.join(prevDir, 'js/engine.js'));
const Feed = require(path.join(prevDir, 'js/feed.js'));

const map = new LedMap(JSON.parse(fs.readFileSync(mapPath, 'utf8')));
const TIE_M = 2.0; // float32 vs double: well under this

// How close a vehicle is to a decision boundary in the preview's maths.
function margin(route, v, choice, eng) {
  const paths = map.paths(route), P = Geo.projector(map.doc.projection_origin);
  const p = P.fwd(v.lat, v.lon);
  let best = Infinity, second = Infinity, tEdge = Infinity, edge = Infinity;
  const maxD = map.doc.max_snap_m || 400, frac = map.doc.snap_fraction != null ? map.doc.snap_fraction : 0.3;
  const idx = choice.index != null ? [choice.index] : paths.map((_, k) => k);
  for (const k of idx) {
    const pts = eng.usable(route, paths[k]).map(e => P.fwd(e.lat, e.lon));
    for (let i = 0; i + 1 < pts.length; i++) {
      const a = pts[i], b = pts[i + 1], dx = b[0] - a[0], dy = b[1] - a[1], L2 = dx * dx + dy * dy;
      let t = L2 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L2 : 0;
      t = Math.max(0, Math.min(1, t));
      const d = Math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy));
      const radius = Math.max(maxD, frac * Math.sqrt(L2));
      edge = Math.min(edge, Math.abs(d - radius));  // the corridor's edge is a boundary
      if (d > radius) continue;
      if (d < best) { second = best; best = d; tEdge = Math.abs(t - 0.5) * Math.sqrt(L2); }
      else if (d < second) second = d;
    }
  }
  return Math.min(second - best, tEdge, edge);
}

function run(file, useHysteresis) {
  const frames = JSON.parse(fs.readFileSync(path.join(outDir, file), 'utf8'));
  const eng = new Engine(map, Geo, { hysteresis: useHysteresis });
  let total = 0, same = 0, ties = 0, bad = [];
  for (const fr of frames) {
    const before = new Map(eng.prev);
    const out = eng.update({ t: fr.t, vehicles: fr.vehicles.map(v => Object.assign({}, v)) }, fr.now);
    out.vehicles.forEach((v, k) => {
      const fw = fr.vehicles[k];
      total++;
      const jsRef = v.state === 'lit' || v.state === 'hidden' ? v.ref : null;
      if (v.state === fw.fw_state && jsRef === fw.fw_ref) { same++; return; }
      let m = margin(v.route, v, eng.choosePath(v.route, v), eng);
      // §4.3 hysteresis keeps the old LED until the projection is 0.6 of a
      // segment past it: that threshold is a boundary too.
      const p = before.get(fw.id), ch = eng.choosePath(v.route, v);
      if (useHysteresis && p && ch.index != null) {
        const pts = eng.usable(v.route, map.paths(v.route)[ch.index]);
        const hit = Geo.snapToPath(v.lat, v.lon, pts, { origin: map.doc.projection_origin, maxSnap: map.doc.max_snap_m, snapFrac: map.doc.snap_fraction });
        const j = pts.findIndex(e => e.ref === p.ref);
        if (hit && j >= 0) {
          const P = Geo.projector(map.doc.projection_origin), a = P.fwd(pts[hit.k].lat, pts[hit.k].lon);
          const b = pts[hit.k + 1] ? P.fwd(pts[hit.k + 1].lat, pts[hit.k + 1].lon) : a;
          m = Math.min(m, Math.abs(Math.abs(hit.k + hit.t - j) - 0.6) * Math.hypot(b[0] - a[0], b[1] - a[1]));
        }
      }
      if (m < TIE_M) { ties++; return; }
      bad.push(`${fw.id}: preview ${v.state} ${jsRef} (d=${v.d && v.d.toFixed(2)}), firmware ${fw.fw_state} ${fw.fw_ref} (d=${fw.fw_d}), margin ${m.toFixed(2)} m`);
    });
  }
  console.log(`${file}: ${total} vehicles, ${same} identical, ${ties} near-ties (<${TIE_M} m), ${bad.length} real disagreements`);
  bad.slice(0, 20).forEach(b => console.log('  ' + b));
  return bad.length;
}

let fails = run('frames.json', true) + run('random.json', false);

// The preview's own protobuf reader must read the firmware's encoder output.
const pb = Feed.decodePb(fs.readFileSync(path.join(outDir, 'sim_feed.pb')));
const okPb = pb.vehicles.length > 0 && pb.vehicles.every(v => v.id && v.route_id && v.lat != null && v.lon != null);
console.log(`sim_feed.pb: preview decoder read ${pb.vehicles.length} vehicles, header t=${pb.t}${okPb ? '' : ' (BAD)'}`);
if (!okPb) fails++;
process.exit(fails ? 1 : 0);
