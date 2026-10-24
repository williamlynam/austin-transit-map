/* Reading CapMetro's GTFS-realtime VehiclePositions feed.
 *
 * decodePb() is a small protobuf reader for just the fields the firmware uses
 * (FIRMWARE_DESIGN.md §3.2), with field numbers from Google's gtfs-realtime.proto.
 * Presence is kept: a trip without direction_id gives dir = null, not 0, because
 * the firmware falls back to bearing in that case (§4.2).
 *
 * Every reader returns { t: header timestamp (s) | null, vehicles: [Vehicle] }
 * where Vehicle = { id, route_id, dir, lat, lon, bearing, ts, label, status }. */
(function (root) {
  'use strict';

  class Reader {
    constructor(buf, start, end) { this.b = buf; this.p = start || 0; this.end = end == null ? buf.length : end; }
    more() { return this.p < this.end; }
    varint() {
      // Numbers up to 2^53 are exact, which covers timestamps and ids.
      let n = 0, mul = 1, byte;
      do {
        if (this.p >= this.end) throw new Error('Truncated protobuf (varint)');
        byte = this.b[this.p++];
        n += (byte & 0x7f) * mul;
        mul *= 128;
      } while (byte & 0x80);
      return n;
    }
    tag() { const t = this.varint(); return [Math.floor(t / 8), t & 7]; }
    bytes() {
      const len = this.varint(), s = this.p;
      if (s + len > this.end) throw new Error('Truncated protobuf (length)');
      this.p += len;
      return [s, s + len];
    }
    sub() { const [s, e] = this.bytes(); return new Reader(this.b, s, e); }
    string() { const [s, e] = this.bytes(); return utf8.decode(this.b.subarray(s, e)); }
    float() { const v = new DataView(this.b.buffer, this.b.byteOffset + this.p, 4).getFloat32(0, true); this.p += 4; return v; }
    skip(wire) {
      if (wire === 0) this.varint();
      else if (wire === 1) this.p += 8;
      else if (wire === 2) this.bytes();
      else if (wire === 5) this.p += 4;
      else throw new Error('Unsupported protobuf wire type ' + wire);
    }
  }
  const utf8 = new TextDecoder('utf-8');

  // Loop over a message's fields; fn(field, wire, reader) returns true if it consumed the value.
  function fields(r, fn) {
    while (r.more()) {
      const [f, w] = r.tag();
      if (!fn(f, w, r)) r.skip(w);
    }
  }

  function decodePb(input) {
    const buf = input instanceof Uint8Array ? input : new Uint8Array(input);
    const out = { t: null, vehicles: [] };
    fields(new Reader(buf), (f, w, r) => {
      if (f === 1 && w === 2) { // FeedHeader
        fields(r.sub(), (hf, hw, hr) => (hf === 3 && hw === 0) ? (out.t = hr.varint(), true) : false);
        return true;
      }
      if (f === 2 && w === 2) { // FeedEntity
        let id = null, v = null, deleted = false;
        fields(r.sub(), (ef, ew, er) => {
          if (ef === 1 && ew === 2) { id = er.string(); return true; }
          if (ef === 2 && ew === 0) { deleted = !!er.varint(); return true; }
          if (ef === 4 && ew === 2) { v = vehiclePosition(er.sub()); return true; }
          return false;
        });
        if (v && !deleted) { if (v.id == null) v.id = id; out.vehicles.push(v); }
        return true;
      }
      return false;
    });
    return out;
  }

  function vehiclePosition(r) {
    const v = { id: null, route_id: null, dir: null, lat: null, lon: null, bearing: null, ts: null, label: null, status: null };
    fields(r, (f, w, x) => {
      if (f === 1 && w === 2) { // TripDescriptor
        fields(x.sub(), (tf, tw, tr) => {
          if (tf === 5 && tw === 2) { v.route_id = tr.string(); return true; }
          if (tf === 6 && tw === 0) { v.dir = tr.varint(); return true; }
          if (tf === 1 && tw === 2) { v.trip_id = tr.string(); return true; }
          return false;
        });
        return true;
      }
      if (f === 2 && w === 2) { // Position
        fields(x.sub(), (pf, pw, pr) => {
          if (pw !== 5) return false;
          if (pf === 1) v.lat = pr.float();
          else if (pf === 2) v.lon = pr.float();
          else if (pf === 3) v.bearing = pr.float();
          else return false;
          return true;
        });
        return true;
      }
      if (f === 4 && w === 0) { v.status = x.varint(); return true; }
      if (f === 5 && w === 0) { v.ts = x.varint(); return true; }
      if (f === 8 && w === 2) { // VehicleDescriptor
        fields(x.sub(), (df, dw, dr) => {
          if (df === 1 && dw === 2) { v.id = dr.string(); return true; }
          if (df === 2 && dw === 2) { v.label = dr.string(); return true; }
          return false;
        });
        return true;
      }
      return false;
    });
    return v;
  }

  /* The JSON flavour of the same feed. Accepts snake_case or camelCase keys,
   * and numbers sent as strings. */
  function decodeJson(doc) {
    if (typeof doc === 'string') doc = JSON.parse(doc);
    const g = (o, a, b) => o == null ? undefined : (o[a] !== undefined ? o[a] : o[b]);
    const num = x => x == null || x === '' ? null : Number(x);
    const header = g(doc, 'header', 'Header') || {};
    const entities = g(doc, 'entity', 'entities') || [];
    const out = { t: num(header.timestamp), vehicles: [] };
    for (const e of entities) {
      const vp = g(e, 'vehicle', 'Vehicle');
      if (!vp || g(e, 'is_deleted', 'isDeleted')) continue;
      const trip = g(vp, 'trip', 'Trip') || {}, pos = g(vp, 'position', 'Position') || {}, vd = g(vp, 'vehicle', 'Vehicle') || {};
      const rid = g(trip, 'route_id', 'routeId');
      out.vehicles.push({
        id: (g(vd, 'id', 'Id') != null ? String(g(vd, 'id', 'Id')) : (e.id != null ? String(e.id) : null)),
        route_id: rid == null ? null : String(rid),
        dir: num(g(trip, 'direction_id', 'directionId')),
        trip_id: g(trip, 'trip_id', 'tripId') || null,
        lat: num(pos.latitude), lon: num(pos.longitude), bearing: num(pos.bearing),
        ts: num(vp.timestamp), label: g(vd, 'label', 'Label') || null,
        status: g(vp, 'current_status', 'currentStatus') == null ? null : g(vp, 'current_status', 'currentStatus')
      });
    }
    return out;
  }

  // Guess the format from the first bytes: JSON starts with "{" (after whitespace).
  function decodeAny(bytes) {
    const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    let i = 0;
    while (i < b.length && (b[i] === 0x20 || b[i] === 0x0a || b[i] === 0x0d || b[i] === 0x09 || b[i] === 0xef || b[i] === 0xbb || b[i] === 0xbf)) i++;
    if (b[i] === 0x7b) return Object.assign(decodeJson(utf8.decode(b)), { format: 'json' });
    return Object.assign(decodePb(b), { format: 'protobuf' });
  }

  const api = { decodePb, decodeJson, decodeAny };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Feed = api;
})(typeof window !== 'undefined' ? window : globalThis);
