/* Static GTFS loader. Reads a google_transit.zip and keeps only what the tool
 * draws for the board's routes: their shapes, the stops they serve, and the
 * headsign of each direction. */
(function (root) {
  'use strict';

  // RFC 4180 CSV -> array of objects keyed by the header row.
  function parseCsv(text) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    const rows = [];
    let row = [], field = '', i = 0, q = false;
    const n = text.length;
    while (i < n) {
      const c = text[i];
      if (q) {
        if (c === '"') {
          if (text[i + 1] === '"') { field += '"'; i += 2; continue; }
          q = false; i++; continue;
        }
        field += c; i++; continue;
      }
      if (c === '"') { q = true; i++; continue; }
      if (c === ',') { row.push(field); field = ''; i++; continue; }
      if (c === '\r') { i++; continue; }
      if (c === '\n') { row.push(field); rows.push(row); row = []; field = ''; i++; continue; }
      field += c; i++;
    }
    if (field !== '' || row.length) { row.push(field); rows.push(row); }
    const head = (rows.shift() || []).map(h => h.trim());
    return rows.filter(r => r.length > 1 || r[0]).map(r => {
      const o = {};
      head.forEach((h, k) => { o[h] = r[k] !== undefined ? r[k] : ''; });
      return o;
    });
  }

  function splitQuoted(line) {
    const out = [];
    let f = '', q = false;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) {
        if (c === '"' && line[i + 1] === '"') { f += '"'; i++; }
        else if (c === '"') q = false;
        else f += c;
      } else if (c === '"') q = true;
      else if (c === ',') { out.push(f); f = ''; }
      else f += c;
    }
    out.push(f);
    return out;
  }

  // stop_times.txt is large; scan it line by line for just two columns.
  function scanStopTimes(text, tripRoute, onRow) {
    let start = 0;
    const firstNl = text.indexOf('\n');
    const head = text.slice(text.charCodeAt(0) === 0xFEFF ? 1 : 0, firstNl).replace('\r', '').split(',').map(s => s.trim().replace(/"/g, ''));
    const iTrip = head.indexOf('trip_id'), iStop = head.indexOf('stop_id');
    start = firstNl + 1;
    while (start < text.length) {
      let end = text.indexOf('\n', start);
      if (end < 0) end = text.length;
      const line = text.slice(start, end);
      start = end + 1;
      if (!line) continue;
      const cols = line.indexOf('"') >= 0 ? splitQuoted(line.replace('\r', '')) : line.replace('\r', '').split(',');
      const route = tripRoute.get(cols[iTrip]);
      if (route) onRow(route, cols[iStop]);
    }
  }

  async function readFile(zip, name, required) {
    const f = zip.file(name) || zip.file(new RegExp('(^|/)' + name.replace('.', '\\.') + '$'))[0];
    if (!f) { if (required) throw new Error(`The zip has no ${name}`); return null; }
    return f.async('string');
  }

  const r5 = v => Math.round(v * 1e5) / 1e5;

  /* wanted: route ids or short names, e.g. ['550','801'].
   * Returns {routes: {id: {...}}, stops: {id: {...}}, loadedAt, source}. */
  async function loadGtfsZip(data, wanted, onProgress) {
    const say = onProgress || (() => {});
    say('Opening zip…');
    const zip = await JSZip.loadAsync(data);

    say('Reading routes…');
    const routesTxt = parseCsv(await readFile(zip, 'routes.txt', true));
    const routes = {};
    const idFor = new Map();
    for (const r of routesTxt) {
      const key = wanted.find(w => w === r.route_id || w === r.route_short_name);
      if (!key) continue;
      idFor.set(r.route_id, key);
      routes[key] = {
        gtfs_route_id: r.route_id, short: r.route_short_name, long: r.route_long_name,
        type: +r.route_type, color: r.route_color ? '#' + r.route_color : null,
        dirs: {}, shapes: [], stops: []
      };
    }
    if (!Object.keys(routes).length) throw new Error('None of the board routes (' + wanted.join(', ') + ') are in this GTFS feed.');

    say('Reading trips…');
    const trips = parseCsv(await readFile(zip, 'trips.txt', true));
    const tripRoute = new Map();
    const shapeUse = {};
    for (const t of trips) {
      const key = idFor.get(t.route_id);
      if (!key) continue;
      const dir = t.direction_id === '' ? '0' : t.direction_id;
      tripRoute.set(t.trip_id, { key, dir });
      const d = routes[key].dirs[dir] || (routes[key].dirs[dir] = { headsigns: {}, trips: 0 });
      d.trips++;
      if (t.trip_headsign) d.headsigns[t.trip_headsign] = (d.headsigns[t.trip_headsign] || 0) + 1;
      if (t.shape_id) (shapeUse[t.shape_id] || (shapeUse[t.shape_id] = { key, dir, n: 0 })).n++;
    }
    for (const r of Object.values(routes)) {
      for (const d of Object.values(r.dirs)) {
        d.headsign = Object.entries(d.headsigns).sort((a, b) => b[1] - a[1]).map(e => e[0])[0] || '';
        delete d.headsigns;
      }
    }

    say('Reading shapes…');
    const shapesText = await readFile(zip, 'shapes.txt', false);
    if (shapesText) {
      const pts = {};
      for (const s of parseCsv(shapesText)) {
        if (!shapeUse[s.shape_id]) continue;
        (pts[s.shape_id] || (pts[s.shape_id] = [])).push([+s.shape_pt_sequence, r5(+s.shape_pt_lat), r5(+s.shape_pt_lon)]);
      }
      for (const [id, list] of Object.entries(pts)) {
        list.sort((a, b) => a[0] - b[0]);
        const u = shapeUse[id];
        routes[u.key].shapes.push({ id, dir: u.dir, trips: u.n, pts: list.map(p => [p[1], p[2]]) });
      }
    }

    say('Reading stop times (this is the big file)…');
    const served = {};
    const servedDir = {};  // route -> stop -> set of direction_ids that call there
    scanStopTimes(await readFile(zip, 'stop_times.txt', true), tripRoute, (t, stopId) => {
      (served[t.key] || (served[t.key] = new Set())).add(stopId);
      const m = servedDir[t.key] || (servedDir[t.key] = {});
      (m[stopId] || (m[stopId] = new Set())).add(t.dir);
    });

    say('Reading stops…');
    const allStops = parseCsv(await readFile(zip, 'stops.txt', true));
    const stopById = new Map(allStops.map(s => [s.stop_id, s]));
    const stops = {};
    for (const [key, set] of Object.entries(served)) {
      routes[key].stops = [...set];
      for (const id of set) {
        const s = stopById.get(id);
        if (!s) continue;
        const o = stops[id] || (stops[id] = { id, name: s.stop_name, code: s.stop_code || '', lat: r5(+s.stop_lat), lon: r5(+s.stop_lon), routes: [], dirs: {}, rail: false });
        o.routes.push(key);
        o.dirs[key] = [...servedDir[key][id]];
        if (routes[key].type === 2 || routes[key].type === 1) o.rail = true;
      }
    }
    say('');
    return { routes, stops, loadedAt: new Date().toISOString() };
  }

  const api = { parseCsv, loadGtfsZip };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Gtfs = api;
})(typeof window !== 'undefined' ? window : globalThis);
