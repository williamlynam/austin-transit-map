/* The map document: the firmware's ledmap.json (FIRMWARE_DESIGN.md §7) kept in
 * memory as-is, with helpers for walking routes and placing LEDs.
 *
 * Positions are per route: a route's own LEDs live in route.leds, and LEDs it
 * borrows from another chain (the D10/D23 crossings) live in route.shared with
 * that route's own lat/lon. */
(function (root) {
  'use strict';

  const DEFAULT_COLORS = { '550': '#FF0000', '801': '#FF00FF', '803': '#0000FF', '837': '#FF8000', '800': '#00FF00' };
  const round6 = v => v == null ? null : Math.round(v * 1e6) / 1e6;

  class LedMap {
    constructor(doc) {
      if (!doc || !Array.isArray(doc.routes)) throw new Error('This is not a ledmap.json (no "routes" list).');
      this.doc = JSON.parse(JSON.stringify(doc));
      this.reindex();
    }

    reindex() {
      this.owner = new Map();
      for (const route of this.doc.routes) {
        if (!Array.isArray(route.leds)) throw new Error(`Route ${route.route_id} has no "leds" list.`);
        for (const led of route.leds) this.owner.set(led.ref, { route, led });
      }
      const bad = [];
      for (const route of this.doc.routes) {
        for (const s of route.shared || []) {
          const o = this.owner.get(s.ref);
          if (!o) bad.push(`${route.route_id} shares ${s.ref}, which is not on the board`);
          else if (o.route.chain !== s.chain || o.led.i !== s.i) bad.push(`${route.route_id} shares ${s.ref} as ${s.chain}[${s.i}], but it is ${o.route.chain}[${o.led.i}]`);
        }
        for (const p of this.paths(route)) {
          for (const r of p.seq) if (!this.entry(route, r)) bad.push(`${route.route_id} path "${p.name}" names ${r}, which is neither its own LED nor in its shared list`);
        }
      }
      if (bad.length) throw new Error(bad.join('; '));
    }

    get routes() { return this.doc.routes; }
    route(id) { return this.doc.routes.find(r => r.route_id === id); }
    physical(ref) { const o = this.owner.get(ref); return o && o.led; }
    ownerRoute(ref) { const o = this.owner.get(ref); return o && o.route; }

    // The entry holding this route's position for ref: its shared entry, else its own LED.
    entry(route, ref) {
      const s = (route.shared || []).find(x => x.ref === ref);
      if (s) return s;
      return route.leds.find(l => l.ref === ref) || null;
    }
    isShared(route, ref) { return (route.shared || []).some(x => x.ref === ref); }

    // Snapping paths. Without explicit paths the chain order serves both directions.
    paths(route) {
      return Array.isArray(route.paths) && route.paths.length
        ? route.paths
        : [{ direction_id: null, name: 'Both', seq: route.leds.map(l => l.ref) }];
    }

    // The order the tool walks a route in: its first path, then anything that
    // only appears in the other paths (the far side of the 837 loop).
    sequence(route) {
      const seen = new Set(), out = [];
      const add = r => { if (!seen.has(r)) { seen.add(r); out.push(r); } };
      for (const p of this.paths(route)) p.seq.forEach(add);
      route.leds.forEach(l => add(l.ref));
      (route.shared || []).forEach(s => add(s.ref));
      return out;
    }

    /* The LEDs strictly between a and b on one of the route's paths, in path order.
     * A one-way path (when the route has several) only counts if a comes before b. */
    between(route, a, b) {
      const paths = this.paths(route);
      let best = null;
      paths.forEach((p, k) => {
        const ia = p.seq.indexOf(a), ib = p.seq.indexOf(b);
        if (ia < 0 || ib < 0 || ia === ib) return;
        if (paths.length > 1 && ib < ia) return;
        const refs = ia < ib ? p.seq.slice(ia + 1, ib) : p.seq.slice(ib + 1, ia).reverse();
        if (!best || refs.length < best.refs.length) best = { path: p, pathIndex: k, refs, oneWay: paths.length > 1 };
      });
      return best;
    }

    isPlaced(route, ref) { const e = this.entry(route, ref); return !!e && e.lat != null && e.lon != null; }

    progress(route) {
      const seq = this.sequence(route);
      return { placed: seq.filter(r => this.isPlaced(route, r)).length, total: seq.length };
    }

    place(route, ref, lat, lon, stop) {
      const e = this.entry(route, ref);
      e.lat = round6(lat); e.lon = round6(lon);
      e.stop_id = stop ? stop.id : null;
      e.label = stop ? stop.name : null;
    }

    clear(route, ref) {
      const e = this.entry(route, ref);
      e.lat = e.lon = null; e.stop_id = null; e.label = null;
    }

    setSkip(route, ref, on) {
      const e = this.entry(route, ref);
      if (on) e.skip = true; else delete e.skip;
    }

    points(route, seq) {
      return seq.map(ref => { const e = this.entry(route, ref); return { ref, lat: e.lat, lon: e.lon, skip: !!e.skip }; });
    }

    /* Which LED a vehicle at lat/lon lights, per §4.2/§4.3.
     * pathIndex: index into paths(route), or null when the direction is unknown:
     * then every path is tried and the hit only counts if that LED is in every path. */
    snap(route, pathIndex, lat, lon, Geo) {
      const opts = { origin: this.doc.projection_origin, maxSnap: this.doc.max_snap_m, snapFrac: this.doc.snap_fraction };
      const paths = this.paths(route);
      if (pathIndex != null) return Geo.snapToPath(lat, lon, this.points(route, paths[pathIndex].seq), opts);
      let best = null;
      for (const p of paths) {
        const hit = Geo.snapToPath(lat, lon, this.points(route, p.seq), opts);
        if (hit && (!best || hit.d < best.d)) best = hit;
      }
      if (best && !paths.every(p => p.seq.includes(best.ref))) return Object.assign(best, { hidden: true });
      return best;
    }

    problems() {
      const out = [];
      for (const route of this.routes) {
        const un = this.sequence(route).filter(r => !this.isPlaced(route, r));
        if (un.length) out.push(`${route.route_id}: ${un.length} LED${un.length > 1 ? 's' : ''} not placed`);
        const ps = this.paths(route);
        if (ps.length > 1 && ps.some(p => p.direction_id == null)) out.push(`${route.route_id}: directions not matched to GTFS`);
      }
      return out;
    }

    toJSON() {
      const doc = JSON.parse(JSON.stringify(this.doc));
      if (!doc.projection_origin) doc.projection_origin = [30.2672, -97.7431];
      if (doc.max_snap_m == null) doc.max_snap_m = 400;
      for (const r of doc.routes) if (!r.color && DEFAULT_COLORS[r.route_id]) r.color = DEFAULT_COLORS[r.route_id];
      doc.updated = new Date().toISOString();
      doc.generated_by = 'LED Mapper';
      return doc;
    }
  }

  function colorOf(route) { return route.color || DEFAULT_COLORS[route.route_id] || '#888'; }

  const api = { LedMap, colorOf };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else Object.assign(root, api);
})(typeof window !== 'undefined' ? window : globalThis);
