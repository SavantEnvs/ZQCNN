#!/usr/bin/env bash
#
# Runs ZQCNN's own regression checks (tools/zq_tile_check.cpp, tools/zq_loadbuffer_check.cpp),
# built by mayhem/build.sh with normal flags. Both compare against independently computed values:
#  - zq_tile_check: ZQ_CNN_Tensor4D::Tile against a reference for every cell, in all three tensor
#    layouts, plus integer-wraparound and non-positive factors that must be rejected;
#  - zq_loadbuffer_check: every shipped model loads through ZQ_CNN_Net::LoadFromBuffer, and each is
#    rejected when its weights are 1/4/16/64 bytes short.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
SRC="${SRC:-/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

O="$SRC/build-mayhem/oracle"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
passed=0
failed=0

for c in zq_tile_check zq_loadbuffer_check; do
  if [ ! -x "$O/$c" ]; then
    echo "missing $O/$c — mayhem/build.sh did not build it" >&2
    failed=$((failed + 1))
    continue
  fi
  timeout 600 "$O/$c" > "$OUT/$c.out" 2>&1
  rc=$?
  cat "$OUT/$c.out" | tail -n 12
  [ "$rc" -eq 0 ] || { echo "$c exited $rc" >&2; failed=$((failed + 1)); }
done

# zq_tile_check summary: 共 N 个用例：全对 A，有错 B，崩溃/搭建失败 C
if [ -f "$OUT/zq_tile_check.out" ]; then
  nums="$(grep -E '^共 [0-9]+ 个用例' "$OUT/zq_tile_check.out" | tail -n 1 | grep -oE '[0-9]+' | tr '\n' ' ')"
  set -- $nums
  if [ $# -eq 4 ]; then
    [ "$2" -gt 0 ] || { echo "zq_tile_check: no passing cases" >&2; failed=$((failed + 1)); }
    passed=$((passed + $2)); failed=$((failed + $3 + $4))
  else
    echo "zq_tile_check: summary line missing" >&2; failed=$((failed + 1))
  fi
fi

# zq_loadbuffer_check summary: 共 T 个 .zqparams：跑过 N 项（通过 P，被拒 R），无配套权重跳过 S
if [ -f "$OUT/zq_loadbuffer_check.out" ]; then
  nums="$(grep -E '个 \.zqparams：跑过' "$OUT/zq_loadbuffer_check.out" | tail -n 1 | grep -oE '[0-9]+' | tr '\n' ' ')"
  set -- $nums
  if [ $# -eq 5 ]; then
    [ "$3" -gt 0 ] || { echo "zq_loadbuffer_check: no passing cases" >&2; failed=$((failed + 1)); }
    passed=$((passed + $3)); failed=$((failed + $4))
  else
    echo "zq_loadbuffer_check: summary line missing" >&2; failed=$((failed + 1))
  fi
fi

fails="$(cat "$OUT"/*.out 2>/dev/null | grep -c '\*\*FAIL\*\*')"
failed=$((failed + fails))

emit_ctrf "zqcnn-checks" "$passed" "$failed" 0
