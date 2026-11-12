#!/usr/bin/env bash
# Builds the sanitized fuzz harnesses (+ standalone repros) and the upstream check programs the
# oracle (mayhem/test.sh) runs. ZQCNN is header-heavy: the Net loader lives in headers, and only
# ZQ_CNN_Tensor4D.cpp + one resize kernel are separate TUs. The SIMD forward kernels (not reachable
# from the fuzzed paths) are replaced by the repo's own link-time tripwire stubs
# (tools/zq_net_fwd_tripwires.h), the same recipe tools/run_zqlib_checks.py uses.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
SRC="${SRC:-/mayhem}"
cd "$SRC"

INC="-I$SRC -I$SRC/ZQCNN -I$SRC/ZQ_GEMM -I$SRC/3rdparty/include -I$SRC/tools -I$SRC/mayhem/omp_stub"
ARCH="-mavx2 -mfma"
B="$SRC/build-mayhem"
rm -rf "$B"
mkdir -p "$B"

# 1) The fuzzed library TUs, instrumented.
LIBFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 $ARCH"
$CXX $LIBFLAGS $INC -c ZQCNN/ZQ_CNN_Tensor4D.cpp -o "$B/t4d.o" &
$CC $LIBFLAGS $INC -c ZQCNN/layers_c/zq_cnn_resize_32f_align_c.c -o "$B/rz.o" &
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$B/lsan_off.o" &
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o" &
wait

# 2) Harnesses: fuzzer binary and standalone reproducer.
for h in fuzz_net_load fuzz_tensor4d; do
  $CXX $LIBFLAGS $INC -c "mayhem/$h.cpp" -o "$B/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/$h.o" "$B/t4d.o" "$B/rz.o" "$B/lsan_off.o" -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$B/$h.o" "$B/standalone_main.o" "$B/t4d.o" "$B/rz.o" "$B/lsan_off.o" -o "$SRC/$h-standalone"
done

# 3) The upstream check programs, with NORMAL flags (no sanitizers) for the oracle. They hardcode a
#    WSL model path, so compile patched temp copies; the repo files stay untouched.
OFLAGS="-O1 $ARCH $COVERAGE_FLAGS"
mkdir -p "$B/oracle"
$CXX $OFLAGS $INC -c ZQCNN/ZQ_CNN_Tensor4D.cpp -o "$B/oracle/t4d.o" &
$CC $OFLAGS $INC -c ZQCNN/layers_c/zq_cnn_resize_32f_align_c.c -o "$B/oracle/rz.o" &
wait
sed "s#\"/mnt/d/ZQCNN/model\"#\"$SRC/model\"#" tools/zq_loadbuffer_check.cpp > "$B/oracle/zq_loadbuffer_check.cpp"
cp tools/zq_tile_check.cpp "$B/oracle/zq_tile_check.cpp"
for c in zq_loadbuffer_check zq_tile_check; do
  $CXX $OFLAGS $INC -I"$SRC/tools" "$B/oracle/$c.cpp" "$B/oracle/t4d.o" "$B/oracle/rz.o" -o "$B/oracle/$c" &
done
wait
