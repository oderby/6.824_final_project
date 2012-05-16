#!/usr/bin/env bash

ulimit -c unlimited

CLIENT_LOG=$1
DO_DISC=$2

CLIENT_PORT=$(head -1 $CLIENT_LOG | sed 's/.*([0-9\.]*:\([0-9]*\):.*/\1/')
echo "disconnecting client on port $CLIENT_PORT"
./disconnect_tester $CLIENT_PORT $DO_DISC