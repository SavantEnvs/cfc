#!/usr/bin/env bash
#
# mayhem/test.sh — run libcimbar's own Catch2 unit tests (built by mayhem/build.sh into
# mayhem/build/test/bin) and report test-case counts as CTRF. The suites assert known values
# (encoded bit patterns, fountain/zstd round trips, reed-solomon output), so a no-op build fails.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
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

BIN="$SRC/mayhem/build/test/bin"
SUITES="bit_file_test chromatic_adaptation_test cimb_translator_test compression_test encoder_test fountain_test image_hash_test"
passed=0; failed=0
for s in $SUITES; do
  if [ ! -x "$BIN/$s" ]; then
    echo "missing test runner $BIN/$s (mayhem/build.sh should have built it)" >&2
    failed=$((failed + 1)); continue
  fi
  out=$(cd "$SRC" && "$BIN/$s" 2>&1); rc=$?
  last=$(printf '%s\n' "$out" | grep -E '^(All tests passed|test cases:)' | tail -1)
  echo "$s: ${last:-<no Catch2 summary>} (exit $rc)"
  if [[ "$last" =~ ^All\ tests\ passed\ \(([0-9]+)\ assertions?\ in\ ([0-9]+)\ test\ cases?\) ]] && [ "$rc" -eq 0 ]; then
    passed=$((passed + BASH_REMATCH[2]))
  elif [[ "$last" =~ test\ cases:\ +([0-9]+)\ \|\ +([0-9]+)\ passed\ \|\ +([0-9]+)\ failed ]]; then
    passed=$((passed + BASH_REMATCH[2])); failed=$((failed + BASH_REMATCH[3]))
    [ "${BASH_REMATCH[3]}" -gt 0 ] || [ "$rc" -eq 0 ] || failed=$((failed + 1))
    [ "${BASH_REMATCH[3]}" -eq 0 ] || printf '%s\n' "$out" | tail -40
  else
    # no summary: the runner did not run its tests (crash, or a neutered binary)
    printf '%s\n' "$out" | tail -20
    failed=$((failed + 1))
  fi
done
emit_ctrf "catch2" "$passed" "$failed" 0
