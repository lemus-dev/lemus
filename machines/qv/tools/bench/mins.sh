#!/bin/sh
# Millions of instructions per second of audio, per workload: each run's count from /usr/bin/time -l
# less the boot's (qvbench run none), over the workload's audio length. Steady to about 0.2 %,
# where timings swing with the machine's load.
#   tools/bench/mins.sh [ROM] [BENCH]
ROM=${1:-../private/roms/qv/qvv203.bin}
BENCH=${2:-build/qv/qvbench}
count() { /usr/bin/time -l "$BENCH" "$ROM" run "$1" 2>&1 >/dev/null | awk '/instructions retired/ {print $1}'; }
seconds() { "$BENCH" "$ROM" run "$1" | awk '{print $2}'; }
base=$(count none)
for w in $("$BENCH" "$ROM" list); do
	n=$(count "$w")
	s=$(seconds "$w")
	awk -v n="$n" -v b="$base" -v s="$s" -v w="$w" 'BEGIN { printf "%-20s %8.0f Mins/s\n", w, (n - b) / s / 1e6 }'
done
