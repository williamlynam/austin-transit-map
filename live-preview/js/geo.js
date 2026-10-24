/* Geometry helpers, plus a copy of the firmware's snapping rule
 * (FIRMWARE_DESIGN.md §4.3) so test mode lights the same LED the board would. */
(function (root) {
  'use strict';

  const ORIGIN = [30.2672, -97.7431];

  function projector(origin) {
    const [lat0, lon0] = origin || ORIGIN;
    const kx = Math.cos(lat0 * Math.PI / 180) * 111320, ky = 110540;
    return {
      fwd: (lat, lon) => [(lon - lon0) * kx, (lat - lat0) * ky],
      inv: (x, y) => [y / ky + lat0, x / kx + lon0]
    };
  }

  // Closest point to p on segment a-b, all in metres.
  function onSegment(p, a, b) {
    const dx = b[0] - a[0], dy = b[1] - a[1];
    const len2 = dx * dx + dy * dy;
    let t = len2 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2 : 0;
    t = Math.max(0, Math.min(1, t));
    const q = [a[0] + t * dx, a[1] + t * dy];
    return { t, q, d: Math.hypot(p[0] - q[0], p[1] - q[1]) };
  }

  // Nearest point on any of several [lat, lon] polylines.
  function nearestOnLines(lat, lon, lines, origin) {
    const P = projector(origin || [lat, lon]);
    const p = P.fwd(lat, lon);
    let best = null;
    for (const line of lines) {
      let prev = null;
      for (const ll of line) {
        const cur = P.fwd(ll[0], ll[1]);
        if (prev) {
          const r = onSegment(p, prev, cur);
          if (!best || r.d < best.d) best = { d: r.d, q: r.q };
        }
        prev = cur;
      }
    }
    if (!best) return null;
    const [la, lo] = P.inv(best.q[0], best.q[1]);
    return { lat: la, lon: lo, d: best.d };
  }

  /* Snap a vehicle to an LED the way the firmware does.
   * path: [{ref, lat, lon, skip}] in path order. Returns {ref, d, k, t} or null. */
  function snapToPath(lat, lon, path, opts) {
    opts = opts || {};
    const P = projector(opts.origin);
    const maxD = opts.maxSnap || 400;
    const frac = opts.snapFrac != null ? opts.snapFrac : 0.3;
    const pts = path.filter(e => e.lat != null && e.lon != null && !e.skip);
    const p = P.fwd(lat, lon);
    let best = null;
    if (pts.length === 1) {
      const q = P.fwd(pts[0].lat, pts[0].lon);
      best = { ref: pts[0].ref, d: Math.hypot(p[0] - q[0], p[1] - q[1]), k: 0, t: 0 };
      return best.d <= maxD ? best : null;
    }
    // Each segment's corridor is maxSnap wide, or snapFrac of its own length when
    // that is wider: track between LEDs kilometres apart curves away from the chord.
    for (let k = 0; k + 1 < pts.length; k++) {
      const a = P.fwd(pts[k].lat, pts[k].lon), b = P.fwd(pts[k + 1].lat, pts[k + 1].lon);
      const r = onSegment(p, a, b);
      if (r.d > Math.max(maxD, frac * Math.hypot(b[0] - a[0], b[1] - a[1]))) continue;
      if (!best || r.d < best.d) best = { ref: (r.t < 0.5 ? pts[k] : pts[k + 1]).ref, d: r.d, k, t: r.t };
    }
    return best;
  }

  // A polyline measured in metres: locate a point on it, or find the point at a distance along it.
  function measure(line, origin) {
    const P = projector(origin || line[0]);
    const xy = line.map(ll => P.fwd(ll[0], ll[1]));
    const cum = [0];
    for (let i = 1; i < xy.length; i++) cum.push(cum[i - 1] + Math.hypot(xy[i][0] - xy[i - 1][0], xy[i][1] - xy[i - 1][1]));
    return {
      length: cum[cum.length - 1],
      locate(lat, lon) {
        const p = P.fwd(lat, lon);
        let best = null;
        for (let i = 1; i < xy.length; i++) {
          const r = onSegment(p, xy[i - 1], xy[i]);
          if (!best || r.d < best.d) best = { d: r.d, s: cum[i - 1] + r.t * (cum[i] - cum[i - 1]) };
        }
        return best;
      },
      pointAt(s) {
        s = Math.max(0, Math.min(cum[cum.length - 1], s));
        let i = 1;
        while (i < cum.length - 1 && cum[i] < s) i++;
        const seg = cum[i] - cum[i - 1], t = seg ? (s - cum[i - 1]) / seg : 0;
        const x = xy[i - 1][0] + t * (xy[i][0] - xy[i - 1][0]), y = xy[i - 1][1] + t * (xy[i][1] - xy[i - 1][1]);
        return P.inv(x, y);
      }
    };
  }

  /* Points spaced evenly along a route line between a and b ([lat, lon]).
   * lines: [{pts, dir}] candidate shapes. forwardOnly: b must come after a on the
   * shape (a one-way path); otherwise either order is fine. Returns
   * {points, line} or null when no shape passes within maxOff metres of both ends. */
  function evenAlong(a, b, n, lines, opts) {
    opts = opts || {};
    const maxOff = opts.maxOff || 150;
    let best = null;
    for (const line of lines) {
      const m = measure(line.pts);
      const la = m.locate(a[0], a[1]), lb = m.locate(b[0], b[1]);
      if (!la || !lb || la.d > maxOff || lb.d > maxOff) continue;
      if (opts.forwardOnly && lb.s <= la.s) continue;
      const span = Math.abs(lb.s - la.s);
      const pref = opts.preferDir != null && String(line.dir) === String(opts.preferDir) ? 0 : 1;
      if (!best || pref < best.pref || (pref === best.pref && span < best.span)) best = { m, la, lb, span, pref, line };
    }
    if (!best) return null;
    const pts = [];
    for (let k = 1; k <= n; k++) pts.push(best.m.pointAt(best.la.s + (best.lb.s - best.la.s) * k / (n + 1)));
    return { points: pts, line: best.line };
  }

  // Fallback: evenly spaced on the straight line between a and b.
  function evenStraight(a, b, n) {
    const out = [];
    for (let k = 1; k <= n; k++) out.push([a[0] + (b[0] - a[0]) * k / (n + 1), a[1] + (b[1] - a[1]) * k / (n + 1)]);
    return out;
  }

  const api = { ORIGIN, projector, nearestOnLines, snapToPath, measure, evenAlong, evenStraight };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Geo = api;
})(typeof window !== 'undefined' ? window : globalThis);
