// GTFS-realtime VehiclePositions decoding (FIRMWARE_DESIGN.md §3.2).
//
// decodeFeedPb streams: the FeedMessage's entities are decoded one at a time
// through a nanopb callback and only vehicles on the map's routes are kept, so
// peak RAM is one entity whatever the feed size.
#pragma once
#include <stdint.h>

#include <string>

#include <ArduinoJson.h>
#include <pb_decode.h>

#include "feed.h"
#include "ledmap.h"

enum class DecodeResult : uint8_t { Ok, Unchanged, Error };

// skipIfT: when nonzero and the header timestamp equals it, stop reading after
// the header and return Unchanged (out.t is still set).
DecodeResult decodeFeedPb(pb_istream_t *stream, const LedMap &map, uint64_t skipIfT, Feed &out, std::string &err);

// Write a feed as a GTFS-rt FeedMessage (simulated-feed mode feeds this back
// through decodeFeedPb). Returns the encoded size, 0 if it didn't fit.
size_t encodeFeedPb(const Feed &feed, uint8_t *buf, size_t cap);
