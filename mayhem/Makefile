# mayhem/Makefile — builds the vendored libcimbar (app/src/cpp/libcimbar) as static libraries for the
# fuzz harnesses and the unit-test oracle. Upstream's libcimbar CMakeLists forces
# CMAKE_INTERPROCEDURAL_OPTIMIZATION (LTO) and builds GLFW/GL code, so we compile the same sources
# directly. Incremental (-MMD) so a re-run of build.sh on a built tree is cheap.
#   make -f mayhem/Makefile OUT=<dir> EXTRA="<flags>" libs
LC      := app/src/cpp/libcimbar
OUT     ?= mayhem/build/fuzz
EXTRA   ?=
CC      ?= clang
CXX     ?= clang++
DEFS    := -DZSTD_STATIC_LINKING_ONLY -DZSTD_DISABLE_ASM
INCS    := -I$(LC)/src/lib -I$(LC)/src/third_party_lib -I$(LC)/src/third_party_lib/wirehair/include -I/usr/include/opencv4 -I$(LC)/src/third_party_lib/libcorrect/include
CFLAGS_ := $(EXTRA) $(DEFS) $(INCS) -MMD -MP
CXXFLAGS_ := -std=c++17 $(CFLAGS_)

ZSTD_SRC := $(wildcard $(LC)/src/third_party_lib/zstd/common/*.c $(LC)/src/third_party_lib/zstd/compress/*.c $(LC)/src/third_party_lib/zstd/decompress/*.c)
CORRECT_SRC := $(wildcard $(LC)/src/third_party_lib/libcorrect/src/reed-solomon/*.c)
WIREHAIR_SRC := $(addprefix $(LC)/src/third_party_lib/wirehair/,wirehair.cpp gf256.cpp WirehairCodec.cpp WirehairTools.cpp)
CIMB_SRC := $(wildcard $(LC)/src/lib/cimb_translator/*.cpp $(LC)/src/lib/extractor/*.cpp) $(LC)/src/lib/cimbar_js/cimbar_recv_js.cpp

obj = $(patsubst $(LC)/%,$(OUT)/obj/%.o,$(1))

# wirehair's gf256 uses SSSE3 intrinsics (upstream's CMake passes -march=native).
# wirehair carves its peeling workspace (PeelRow/PeelRefs structs) out of one byte buffer at
# unaligned offsets, so UBSan's alignment check fires on every fountain decode; relax only that
# check, only for wirehair's own files (x86 tolerates unaligned access). ASan and the rest of UBSan stay on.
$(call obj,$(WIREHAIR_SRC)): CXXFLAGS_ += -mssse3 $(if $(findstring undefined,$(EXTRA)),-fno-sanitize=alignment)

LIBS := $(OUT)/libcimbar.a $(OUT)/libcorrect.a $(OUT)/libwirehair.a $(OUT)/libzstd.a

libs: $(LIBS)

$(OUT)/libcimbar.a: $(call obj,$(CIMB_SRC))
$(OUT)/libcorrect.a: $(call obj,$(CORRECT_SRC))
$(OUT)/libwirehair.a: $(call obj,$(WIREHAIR_SRC))
$(OUT)/libzstd.a: $(call obj,$(ZSTD_SRC))
$(LIBS):
	@mkdir -p $(dir $@)
	rm -f $@ && ar rcs $@ $^

# Rebuild everything when the compiler or flags change (e.g. SANITIZER_FLAGS / COVERAGE_FLAGS overrides).
FLAGS_ID := $(CC) $(CXX) $(CFLAGS_)
$(shell mkdir -p $(OUT) && (echo '$(FLAGS_ID)' | cmp -s - $(OUT)/flags.stamp || echo '$(FLAGS_ID)' > $(OUT)/flags.stamp))

$(OUT)/obj/%.c.o: $(LC)/%.c $(OUT)/flags.stamp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(OUT)/obj/%.cpp.o: $(LC)/%.cpp $(OUT)/flags.stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_) -c $< -o $@

-include $(shell find $(OUT)/obj -name '*.d' 2>/dev/null)
.PHONY: libs
