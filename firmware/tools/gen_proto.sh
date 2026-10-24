#!/bin/sh
# Regenerate lib/nanopb/gtfs-realtime.pb.{c,h} from proto/. Only needed if the
# .proto or .options change; the generated files are checked in.
#   pip install nanopb   (brings the generator and protobuf)
set -e
cd "$(dirname "$0")/../proto"
nanopb_generator -I . -D ../lib/nanopb gtfs-realtime.proto
