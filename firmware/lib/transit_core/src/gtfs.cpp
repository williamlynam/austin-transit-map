#include "gtfs.h"

#include <stdlib.h>
#include <string.h>

#include <pb_encode.h>

#include "gtfs-realtime.pb.h"

void Feed::noteOther(const char *routeId) {
  otherRoutes++;
  if (!routeId[0]) {
    noRouteId++;
    return;
  }
  for (int k = 0; k < otherIdCount; k++)
    if (strncmp(otherIds[k], routeId, sizeof otherIds[k] - 1) == 0) return;
  if (otherIdCount >= MAX_OTHER_IDS) return;
  strncpy(otherIds[otherIdCount], routeId, sizeof otherIds[0] - 1);
  otherIds[otherIdCount][sizeof otherIds[0] - 1] = '\0';
  otherIdCount++;
}

namespace {

struct StrBuf {
  char *buf;
  size_t cap;
};

// Copy a string field, truncating if it's longer than the buffer.
bool readString(pb_istream_t *stream, const pb_field_t *, void **arg) {
  StrBuf *sb = static_cast<StrBuf *>(*arg);
  size_t n = stream->bytes_left;
  size_t keep = n < sb->cap - 1 ? n : sb->cap - 1;
  if (!pb_read(stream, reinterpret_cast<pb_byte_t *>(sb->buf), keep)) return false;
  sb->buf[keep] = '\0';
  return pb_read(stream, nullptr, n - keep);
}

struct DecodeCtx {
  const LedMap *map;
  Feed *out;
  const transit_realtime_FeedMessage *msg;
  uint64_t skipIfT;
  bool unchanged;
};

bool readEntity(pb_istream_t *stream, const pb_field_t *, void **arg) {
  DecodeCtx *ctx = static_cast<DecodeCtx *>(*arg);
  // The header is field 1 and arrives before any entity, so this is the first
  // point where we know whether the feed changed.
  if (ctx->skipIfT && ctx->msg->header.has_timestamp && ctx->msg->header.timestamp == ctx->skipIfT) {
    ctx->unchanged = true;
    return false;
  }

  char entityId[24] = "", routeId[16] = "", vid[24] = "", label[16] = "";
  StrBuf sbEntity{entityId, sizeof entityId}, sbRoute{routeId, sizeof routeId}, sbVid{vid, sizeof vid},
      sbLabel{label, sizeof label};
  transit_realtime_FeedEntity e = transit_realtime_FeedEntity_init_zero;
  e.id.funcs.decode = readString;
  e.id.arg = &sbEntity;
  e.vehicle.trip.route_id.funcs.decode = readString;
  e.vehicle.trip.route_id.arg = &sbRoute;
  e.vehicle.vehicle.id.funcs.decode = readString;
  e.vehicle.vehicle.id.arg = &sbVid;
  e.vehicle.vehicle.label.funcs.decode = readString;
  e.vehicle.vehicle.label.arg = &sbLabel;
  if (!pb_decode(stream, transit_realtime_FeedEntity_fields, &e)) return false;

  if (!e.has_vehicle || (e.has_is_deleted && e.is_deleted)) return true;
  if (!routeId[0] || ctx->map->routeIndex(routeId) < 0) {
    ctx->out->noteOther(routeId);
    return true;
  }
  Feed &f = *ctx->out;
  if (f.count >= MAX_VEHICLES) {
    f.dropped++;
    return true;
  }
  Vehicle &v = f.v[f.count++];
  memset(&v, 0, sizeof v);
  const transit_realtime_VehiclePosition &vp = e.vehicle;
  strcpy(v.id, vid[0] ? vid : entityId);
  strcpy(v.routeId, routeId);
  strcpy(v.label, label);
  v.dir = vp.trip.has_direction_id ? int16_t(vp.trip.direction_id) : -1;
  v.hasPos = vp.has_position;
  v.lat = vp.position.latitude;
  v.lon = vp.position.longitude;
  v.hasBearing = vp.has_position && vp.position.has_bearing;
  v.bearing = vp.position.bearing;
  v.ts = vp.has_timestamp ? vp.timestamp : 0;
  return true;
}

// --- encoding (simulated feed) ---

struct EncodeStr {
  const char *s;
};

bool writeString(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
  const char *s = static_cast<const EncodeStr *>(*arg)->s;
  if (!s || !*s) return true;
  return pb_encode_tag_for_field(stream, field) &&
         pb_encode_string(stream, reinterpret_cast<const pb_byte_t *>(s), strlen(s));
}

bool writeEntities(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
  const Feed &feed = *static_cast<const Feed *>(*arg);
  for (int k = 0; k < feed.count; k++) {
    const Vehicle &v = feed.v[k];
    EncodeStr id{v.id}, route{v.routeId}, label{v.label};
    transit_realtime_FeedEntity e = transit_realtime_FeedEntity_init_zero;
    e.id.funcs.encode = writeString;
    e.id.arg = &id;
    e.has_vehicle = true;
    e.vehicle.has_trip = true;
    e.vehicle.trip.route_id.funcs.encode = writeString;
    e.vehicle.trip.route_id.arg = &route;
    if (v.dir >= 0) {
      e.vehicle.trip.has_direction_id = true;
      e.vehicle.trip.direction_id = uint32_t(v.dir);
    }
    e.vehicle.has_vehicle = true;
    e.vehicle.vehicle.id.funcs.encode = writeString;
    e.vehicle.vehicle.id.arg = &id;
    e.vehicle.vehicle.label.funcs.encode = writeString;
    e.vehicle.vehicle.label.arg = &label;
    if (v.hasPos) {
      e.vehicle.has_position = true;
      e.vehicle.position.latitude = v.lat;
      e.vehicle.position.longitude = v.lon;
      e.vehicle.position.has_bearing = v.hasBearing;
      e.vehicle.position.bearing = v.bearing;
    }
    if (v.ts) {
      e.vehicle.has_timestamp = true;
      e.vehicle.timestamp = v.ts;
    }
    if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, transit_realtime_FeedEntity_fields, &e))
      return false;
  }
  return true;
}

// Keep a board vehicle; returns null for other routes / overflow.
Vehicle *keep(const LedMap &map, Feed &out, const char *routeId) {
  if (!routeId[0] || map.routeIndex(routeId) < 0) {
    out.noteOther(routeId);
    return nullptr;
  }
  if (out.count >= MAX_VEHICLES) {
    out.dropped++;
    return nullptr;
  }
  Vehicle *v = &out.v[out.count++];
  memset(v, 0, sizeof *v);
  v->dir = -1;
  strncpy(v->routeId, routeId, sizeof v->routeId - 1);
  return v;
}

}  // namespace

DecodeResult decodeFeedPb(pb_istream_t *stream, const LedMap &map, uint64_t skipIfT, Feed &out, std::string &err) {
  out.t = 0;
  out.clearCounts();
  transit_realtime_FeedMessage msg = transit_realtime_FeedMessage_init_zero;
  DecodeCtx ctx{&map, &out, &msg, skipIfT, false};
  msg.entity.funcs.decode = readEntity;
  msg.entity.arg = &ctx;
  bool ok = pb_decode(stream, transit_realtime_FeedMessage_fields, &msg);
  out.t = msg.header.has_timestamp ? msg.header.timestamp : 0;
  if (ctx.unchanged) return DecodeResult::Unchanged;
  if (!ok) {
    err = std::string("protobuf decode failed: ") + PB_GET_ERROR(stream);
    return DecodeResult::Error;
  }
  return DecodeResult::Ok;
}

size_t encodeFeedPb(const Feed &feed, uint8_t *buf, size_t cap) {
  transit_realtime_FeedMessage msg = transit_realtime_FeedMessage_init_zero;
  if (feed.t) {
    msg.header.has_timestamp = true;
    msg.header.timestamp = feed.t;
  }
  msg.entity.funcs.encode = writeEntities;
  msg.entity.arg = const_cast<Feed *>(&feed);
  pb_ostream_t os = pb_ostream_from_buffer(buf, cap);
  if (!pb_encode(&os, transit_realtime_FeedMessage_fields, &msg)) return 0;
  return os.bytes_written;
}
