#!/usr/bin/env bash
#
# mayhem/build.sh — build the cfc / libcimbar fuzz harnesses and the libcimbar unit-test oracle.
#
# cfc is an Android app; its native code is app/src/cpp (a JNI layer over the vendored libcimbar).
# Upstream's app CMakeLists is Android-only and libcimbar's own CMakeLists forces LTO and links
# GLFW/GL, so mayhem/Makefile compiles the same library sources (cimb_translator, extractor,
# cimbar_recv_js, libcorrect, wirehair, zstd) directly against the system OpenCV.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export CC CXX

cd "$SRC"

LC=app/src/cpp/libcimbar
INCS="-I$LC/src/lib -I$LC/src/third_party_lib -I$LC/src/third_party_lib/wirehair/include -I$LC/src/third_party_lib/libcorrect/include -I/usr/include/opencv4"
DEFS="-DZSTD_STATIC_LINKING_ONLY -DZSTD_DISABLE_ASM"
CV_LIBS="-lopencv_calib3d -lopencv_photo -lopencv_imgcodecs -lopencv_imgproc -lopencv_core"

# 1) libcimbar + its vendored deps, sanitized and with DWARF 3 (the fuzzed code is instrumented).
FUZZ_OUT=mayhem/build/fuzz
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link"
make -f mayhem/Makefile -j"$MAYHEM_JOBS" OUT="$FUZZ_OUT" EXTRA="$FUZZ_FLAGS" libs
FUZZ_LIBS="$FUZZ_OUT/libcimbar.a $FUZZ_OUT/libcorrect.a $FUZZ_OUT/libwirehair.a $FUZZ_OUT/libzstd.a"

# 2) harnesses: a libFuzzer binary and a run-once -standalone reproducer each.
mkdir -p "$FUZZ_OUT/h"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$FUZZ_OUT/h/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_OUT/h/standalone_main.o"
for h in fuzz_decode fuzz_fountain fuzz_scan fuzz_roundtrip fuzz_mtdecoder; do
  $CXX -std=c++17 -O1 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $DEFS $INCS -Iapp/src/cpp \
      -c "mayhem/harnesses/$h.cpp" -o "$FUZZ_OUT/h/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_OUT/h/$h.o" "$FUZZ_OUT/h/lsan_off.o" \
      $FUZZ_LIBS $CV_LIBS -lpthread -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$FUZZ_OUT/h/$h.o" "$FUZZ_OUT/h/standalone_main.o" "$FUZZ_OUT/h/lsan_off.o" \
      $FUZZ_LIBS $CV_LIBS -lpthread -o "$SRC/$h-standalone"
done

# 3) libcimbar's own Catch2 unit tests, with normal flags (no sanitizers), for mayhem/test.sh.
#    Only the suites whose fixtures are in this tree: libcimbar's samples/ directory (the PNG/hash
#    fixtures some tests load) is not part of the subtree cfc vendors.
TEST_OUT=mayhem/build/test
make -f mayhem/Makefile -j"$MAYHEM_JOBS" OUT="$TEST_OUT" EXTRA="-O2 -g $COVERAGE_FLAGS" libs
TEST_LIBS="$TEST_OUT/libcimbar.a $TEST_OUT/libcorrect.a $TEST_OUT/libwirehair.a $TEST_OUT/libzstd.a"
mkdir -p "$TEST_OUT/bin" "$TEST_OUT/tobj"
# the unit-test objects are rebuilt when their source or the flags change
TFLAGS="$CXX -std=c++17 -O1 -g $COVERAGE_FLAGS $DEFS $INCS"
if [ "$(cat "$TEST_OUT/tobj/flags.stamp" 2>/dev/null)" != "$TFLAGS" ]; then
  rm -rf "$TEST_OUT/tobj"; mkdir -p "$TEST_OUT/tobj"; printf '%s' "$TFLAGS" > "$TEST_OUT/tobj/flags.stamp"
fi
T=$LC/src/lib
declare -A SUITES=(
  [fountain_test]="fountain/test/test.cpp fountain/test/FountainEncodingTest.cpp fountain/test/FountainMetadataTest.cpp fountain/test/fountain_sinkTest.cpp fountain/test/fountain_sinkSpecialTest.cpp fountain/test/fountain_streamTest.cpp"
  [compression_test]="compression/test/test.cpp compression/test/zstd_compressorTest.cpp compression/test/zstd_decompressorTest.cpp compression/test/zstd_header_checkTest.cpp"
  [bit_file_test]="bit_file/test/test.cpp bit_file/test/bitbufferTest.cpp bit_file/test/bitreaderTest.cpp"
  [cimb_translator_test]="cimb_translator/test/test.cpp cimb_translator/test/AdjacentCellFinderTest.cpp cimb_translator/test/CellDriftTest.cpp cimb_translator/test/CellPositionsTest.cpp cimb_translator/test/CimbEncoderTest.cpp cimb_translator/test/CimbWriterTest.cpp cimb_translator/test/FloodDecodePositionsTest.cpp cimb_translator/test/InterleaveTest.cpp cimb_translator/test/LinearDecodePositionsTest.cpp"
  [encoder_test]="encoder/test/test.cpp encoder/test/aligned_streamTest.cpp encoder/test/escrow_buffer_writerTest.cpp encoder/test/reed_solomon_streamTest.cpp"
  [image_hash_test]="image_hash/test/test.cpp image_hash/test/averageHashTest.cpp image_hash/test/bitExtractorTest.cpp image_hash/test/fuzzyAhashTest.cpp"
  [chromatic_adaptation_test]="chromatic_adaptation/test/test.cpp chromatic_adaptation/test/color_correctionTest.cpp"
)
for suite in "${!SUITES[@]}"; do
  objs=()
  for src in ${SUITES[$suite]}; do
    dir=${src%%/*}
    o="$TEST_OUT/tobj/${src%.cpp}.o"
    mkdir -p "$(dirname "$o")"
    if [ ! -f "$o" ] || [ "$T/$src" -nt "$o" ]; then
      $CXX -std=c++17 -O1 -g $COVERAGE_FLAGS $DEFS $INCS -I"$LC/test" -I"$T/$dir" \
          -DLIBCIMBAR_PROJECT_ROOT="\"$SRC/$LC\"" -c "$T/$src" -o "$o"
    fi
    objs+=("$o")
  done
  $CXX $COVERAGE_FLAGS "${objs[@]}" $TEST_LIBS $CV_LIBS -lpthread -o "$TEST_OUT/bin/$suite"
done
