/* What the firmware does with one feed snapshot (FIRMWARE_DESIGN.md §3–§5),
 * minus the hardware: filter to the board's routes, drop stale vehicles, pick the
 * direction path, snap to an LED, apply hysteresis, and collect the vehicles on
 * each physical LED.
 *
 * Snapping itself is Geo.snapToPath, the same function the LED Mapper's test
 * mode uses, so the two tools always agree on which LED a position lights. */
(function (root) {
  'use strict';

  const DEFAULTS = { staleS: 120, hysteresis: true, hyst: 0.6 };

  class Engine {
    constructor(ledmap, Geo, opts) {
      this.map = ledmap; this.Geo = Geo;
      this.opts = Object.assign({}, DEFAULTS, opts || {});
      this.prev = new Map(); // vehicle id -> { route_id, pathIndex, ref }
    }

    reset() { this.prev.clear(); }

    snapOpts() { return { origin: this.map.doc.projection_origin, maxSnap: this.map.doc.max_snap_m, snapFrac: this.map.doc.snap_fraction }; }

    // The LEDs snapping can use on one path: placed and not skipped, in path order.
    usable(route, path) { return this.map.points(route, path.seq).filter(e => e.lat != null && e.lon != null && !e.skip); }

    /* §4.2: direction_id picks the path; without it, bearing (0–90° or 270–360°
     * is Northbound); without either, null = try every path. */
    choosePath(route, v) {
      const paths = this.map.paths(route);
      if (paths.length === 1) return { index: 0, how: 'single path' };
      if (v.dir != null) {
        const k = paths.findIndex(p => p.direction_id != null && Number(p.direction_id) === Number(v.dir));
        if (k >= 0) return { index: k, how: 'direction_id' };
      }
      if (v.bearing != null && isFinite(v.bearing)) {
        const b = ((v.bearing % 360) + 360) % 360;
        const nb = b <= 90 || b >= 270;
        const k = paths.findIndex(p => (nb ? /north/i : /south/i).test(p.name || ''));
        if (k >= 0) return { index: k, how: 'bearing' };
      }
      return { index: null, how: 'unknown direction' };
    }

    snap(route, choice, v) {
      const paths = this.map.paths(route), o = this.snapOpts();
      if (choice.index != null) {
        const pts = this.usable(route, paths[choice.index]);
        const hit = this.Geo.snapToPath(v.lat, v.lon, pts, o);
        return hit && Object.assign(hit, { pathIndex: choice.index, pts });
      }
      let best = null;
      paths.forEach((p, k) => {
        const pts = this.usable(route, p);
        const hit = this.Geo.snapToPath(v.lat, v.lon, pts, o);
        if (hit && (!best || hit.d < best.d)) best = Object.assign(hit, { pathIndex: k, pts });
      });
      // §4.2 fallback 3: only accept an LED that every path shares (not the loop).
      if (best && !paths.every(p => p.seq.includes(best.ref))) best.hidden = true;
      return best;
    }

    /* §4.3 hysteresis: keep the previous LED unless the new projection is more
     * than 0.6 of a segment away from it. */
    hold(v, route, hit) {
      const p = this.prev.get(v.id);
      if (!this.opts.hysteresis || !p || p.route_id !== route.route_id || p.pathIndex !== hit.pathIndex || p.ref === hit.ref) return false;
      const j = hit.pts.findIndex(e => e.ref === p.ref);
      if (j < 0) return false;
      if (Math.abs(hit.k + hit.t - j) > this.opts.hyst) return false;
      hit.heldFrom = hit.ref;
      hit.ref = p.ref;
      return true;
    }

    /* feed: { t, vehicles }. nowS: the clock to judge staleness against. */
    update(feed, nowS) {
      const out = { vehicles: [], leds: new Map(), otherRoutes: new Map(), t: feed.t };
      const seen = new Set();
      for (const src of feed.vehicles) {
        const v = Object.assign({}, src);
        const route = v.route_id != null && this.map.route(String(v.route_id));
        if (!route) {
          const k = v.route_id == null ? '(none)' : String(v.route_id);
          out.otherRoutes.set(k, (out.otherRoutes.get(k) || 0) + 1);
          continue;
        }
        out.vehicles.push(v);
        v.route = route;
        if (v.id == null) v.id = `${route.route_id}?${out.vehicles.length}`;
        const ts = v.ts != null ? v.ts : feed.t;
        v.age = ts != null && nowS != null ? nowS - ts : null;
        if (v.lat == null || v.lon == null || (v.lat === 0 && v.lon === 0)) { v.state = 'no position'; continue; }
        if (v.age != null && v.age > this.opts.staleS) { v.state = 'stale'; continue; }

        const choice = this.choosePath(route, v);
        v.how = choice.how;
        const hit = this.snap(route, choice, v);
        if (!hit) { v.state = 'off route'; continue; }
        v.d = hit.d;
        v.pathName = this.map.paths(route)[hit.pathIndex].name || null;
        if (hit.hidden) { v.state = 'hidden'; v.ref = hit.ref; continue; }

        v.held = this.hold(v, route, hit);
        v.ref = hit.ref;
        v.state = 'lit';
        seen.add(v.id);
        this.prev.set(v.id, { route_id: route.route_id, pathIndex: hit.pathIndex, ref: hit.ref });

        const owner = this.map.ownerRoute(hit.ref), led = this.map.physical(hit.ref);
        v.chain = owner.chain; v.index = led.i; v.borrowed = owner !== route;
        if (!out.leds.has(hit.ref)) out.leds.set(hit.ref, []);
        out.leds.get(hit.ref).push(v);
      }
      for (const id of [...this.prev.keys()]) if (!seen.has(id)) this.prev.delete(id);
      return out;
    }
  }

  const api = { Engine };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else Object.assign(root, api);
})(typeof window !== 'undefined' ? window : globalThis);
