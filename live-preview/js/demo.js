/* A made-up feed for trying the preview without CapMetro: buses drive along the
 * loaded map's own paths, plus a few awkward cases the firmware has to handle
 * (no direction_id, no bearing either, off route, stale, a route not on the board). */
(function (root) {
  'use strict';

  class Demo {
    constructor(ledmap, Geo, speedup) {
      this.map = ledmap; this.Geo = Geo; this.speedup = speedup || 5;
      this.cars = []; this.last = null;
      let n = 0;
      for (const route of ledmap.routes) {
        const paths = ledmap.paths(route);
        const legs = [];
        for (const p of paths) {
          const pts = ledmap.points(route, p.seq).filter(e => e.lat != null && e.lon != null).map(e => [e.lat, e.lon]);
          if (pts.length < 2) continue;
          if (paths.length > 1) legs.push({ pts, dir: p.direction_id, name: p.name });
          else { legs.push({ pts, dir: 0 }); legs.push({ pts: pts.slice().reverse(), dir: 1 }); }
        }
        for (const leg of legs) {
          const m = Geo.measure(leg.pts);
          const count = Math.max(1, Math.round(m.length / 9000));
          for (let k = 0; k < count; k++) {
            this.cars.push({ id: `demo-${route.route_id}-${++n}`, route_id: route.route_id, dir: leg.dir, m, s: m.length * (k + Math.random() * 0.6) / count, speed: 6 + Math.random() * 6, mode: 'normal' });
          }
        }
      }
      // The awkward cases, on the 837 loop when it has direction paths.
      const r837 = ledmap.route('837');
      const multi = this.cars.filter(c => r837 && c.route_id === '837');
      if (multi.length >= 2) { multi[0].mode = 'bearing only'; multi[1].mode = 'no direction'; }
      if (this.cars.length > 3) { this.cars[2].mode = 'off route'; this.cars[3].mode = 'stale'; }
      this.cars.push({ id: 'demo-other-1', route_id: '20', mode: 'other', lat: 30.2849, lon: -97.7341 });
    }

    tick(nowS) {
      const dt = this.last == null ? 0 : Math.min(60, nowS - this.last);
      this.last = nowS;
      const vehicles = [];
      for (const c of this.cars) {
        if (c.mode === 'other') { vehicles.push({ id: c.id, route_id: c.route_id, dir: 0, lat: c.lat, lon: c.lon, bearing: 0, ts: Math.round(nowS) }); continue; }
        c.s += c.speed * dt * this.speedup;
        if (c.s > c.m.length) c.s -= c.m.length;
        let [lat, lon] = c.m.pointAt(c.s);
        const [la2, lo2] = c.m.pointAt(Math.min(c.m.length, c.s + 30));
        const bearing = (Math.atan2((lo2 - lon) * Math.cos(lat * Math.PI / 180), la2 - lat) * 180 / Math.PI + 360) % 360;
        lat += (Math.random() - 0.5) * 0.0003; lon += (Math.random() - 0.5) * 0.0003; // ±15 m of GPS noise
        const v = { id: c.id, route_id: c.route_id, dir: c.dir, lat, lon, bearing: Math.round(bearing), ts: Math.round(nowS) - Math.round(Math.random() * 20), label: c.id.replace('demo-', '') };
        if (c.mode === 'bearing only') v.dir = null;
        if (c.mode === 'no direction') { v.dir = null; v.bearing = null; }
        if (c.mode === 'off route') { v.lat += 0.02; }
        if (c.mode === 'stale') v.ts = Math.round(nowS) - 300;
        vehicles.push(v);
      }
      return { t: Math.round(nowS), vehicles, format: 'demo' };
    }
  }

  root.Demo = Demo;
})(typeof window !== 'undefined' ? window : globalThis);
