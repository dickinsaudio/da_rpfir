#!/bin/sh
# Build the native end to end harness.  Usage: build_native.sh <out_dir>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../..
OUT=${1:-/tmp/a2b_tunnel_native}
DEV=$ROOT/extern/a2b_master/device
IOLIB=$ROOT/WIZnet-PICO-C/libraries/ioLibrary_Driver/Ethernet
mkdir -p $OUT/src
# Copy the server so its "log.hpp" include resolves to the native stub, not the SDK one
cp $ROOT/daes67_lite/http_server.cpp $ROOT/daes67_lite/http_server.hpp $OUT/src/
DEFS="-D_WIZCHIP_=W5500 -DPICO_BUILD"
INC="-I$HERE/stubs -I$OUT/src -I$DEV -I$IOLIB -I$IOLIB/W5500"
gcc -c -O1 -g $DEFS $INC $IOLIB/wizchip_conf.c -o $OUT/wizchip_conf.o
gcc -c -O1 -g $DEFS $INC $IOLIB/W5500/w5500.c -o $OUT/w5500.o
gcc -c -O1 -g -Wall $DEFS $INC $DEV/vw5500.c -o $OUT/vw5500.o
gcc -c -O1 -g -Wall $DEFS $INC $DEV/tunnel_proto.c -o $OUT/tunnel_proto.o
g++ -c -O1 -g -std=gnu++17 -Wno-format-overflow $DEFS $INC -include $HERE/stubs/log.hpp $OUT/src/http_server.cpp -o $OUT/http_server.o
g++ -c -O1 -g -std=gnu++17 -Wall $DEFS $INC $HERE/native_sim.cpp -o $OUT/native_sim.o
g++ -o $OUT/native_sim $OUT/*.o
echo "built $OUT/native_sim"
