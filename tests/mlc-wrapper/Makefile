# Phase 1 TDD scaffold for the dlmopen-based MlcWrapper.
#
# Builds libmoonlight-common-c as a shared object out of tree (no changes to the
# project's moonlight-common-c.pro) and runs validation tests against it. The
# tests pin the architectural assumptions of option D before any wrapper code is
# written.
#
# Usage:
#   make test       # build .so, build driver, run all tests
#   make clean      # remove build artefacts
#
# All artefacts are gitignored. If a test fails, the run aborts non-zero so this
# can be wired into CI later.

REPO := $(shell git rev-parse --show-toplevel 2>/dev/null)
ifeq ($(REPO),)
$(error not inside a git repo, or git not installed)
endif

MLC  := $(REPO)/moonlight-common-c/moonlight-common-c
ENET := $(MLC)/enet

CFLAGS := -O2 -g -std=gnu99 -fPIC \
  -I$(ENET)/include -I$(MLC)/src \
  -I$(MLC)/nanors -I$(MLC)/nanors/deps -I$(MLC)/nanors/deps/obl \
  -DHAS_SOCKLEN_T -DHAVE_CLOCK_GETTIME=1 \
  $(shell pkg-config --cflags openssl)

LDFLAGS := -lpthread $(shell pkg-config --libs openssl)

# === Bundled-OpenSSL build (gating prerequisite for dlmopen mode) ===
#
# Each dlmopen LM_ID_NEWLM load of libmoonlight-common-c.so brings in its own
# libcrypto.so from /lib64, but OpenSSL's per-process state assumption breaks
# under that scheme: OPENSSL_init_crypto SIGSEGVs on the second namespace.
# Workaround: link a static OpenSSL into the .so itself with --whole-archive,
# and hide every openssl symbol from the .so's dynamic export table with
# --exclude-libs so the dynamic linker never tries to share them with the
# main namespace's libcrypto.
#
# The build-openssl-static.sh script populates openssl-static/ once.

OPENSSL_STATIC_DIR := $(CURDIR)/openssl-static
OPENSSL_STATIC_LIB := $(OPENSSL_STATIC_DIR)/lib64
OPENSSL_STATIC_INC := $(OPENSSL_STATIC_DIR)/include
ifeq ($(wildcard $(OPENSSL_STATIC_LIB)/libcrypto.a),)
  OPENSSL_STATIC_LIB := $(OPENSSL_STATIC_DIR)/lib
endif

BUNDLED_CFLAGS := -O2 -g -std=gnu99 -fPIC \
  -I$(ENET)/include -I$(MLC)/src \
  -I$(MLC)/nanors -I$(MLC)/nanors/deps -I$(MLC)/nanors/deps/obl \
  -DHAS_SOCKLEN_T -DHAVE_CLOCK_GETTIME=1 \
  -I$(OPENSSL_STATIC_INC)

BUNDLED_LDFLAGS := -lpthread \
  -Wl,--whole-archive \
    $(OPENSSL_STATIC_LIB)/libssl.a \
    $(OPENSSL_STATIC_LIB)/libcrypto.a \
  -Wl,--no-whole-archive \
  -Wl,--exclude-libs,libssl.a:libcrypto.a

# Mirror of moonlight-common-c.pro on Linux (excludes Windows-only enet/win32.c
# and the platform-specific files not selected on this OS). Update when upstream
# adds or removes sources.
MLC_SOURCES := \
  $(ENET)/callbacks.c $(ENET)/compress.c $(ENET)/host.c $(ENET)/list.c \
  $(ENET)/packet.c $(ENET)/peer.c $(ENET)/protocol.c $(ENET)/unix.c \
  $(MLC)/src/rswrapper.c \
  $(MLC)/src/AudioStream.c $(MLC)/src/ByteBuffer.c $(MLC)/src/Connection.c \
  $(MLC)/src/ConnectionTester.c $(MLC)/src/ControlStream.c \
  $(MLC)/src/FakeCallbacks.c $(MLC)/src/InputStream.c \
  $(MLC)/src/LinkedBlockingQueue.c $(MLC)/src/Misc.c \
  $(MLC)/src/Platform.c $(MLC)/src/PlatformCrypto.c \
  $(MLC)/src/PlatformSockets.c $(MLC)/src/RtpAudioQueue.c \
  $(MLC)/src/RtpVideoQueue.c $(MLC)/src/RtspConnection.c \
  $(MLC)/src/RtspParser.c $(MLC)/src/SdpGenerator.c \
  $(MLC)/src/SimpleStun.c $(MLC)/src/VideoDepacketizer.c \
  $(MLC)/src/VideoStream.c

APP_DIR := $(REPO)/app/streaming

.PHONY: all test clean bundled test-bundled openssl-static

all: libmoonlight-common-c.so test_dlmopen test_wrapper

libmoonlight-common-c.so: $(MLC_SOURCES)
	@echo "==> Building $@"
	gcc -shared $(CFLAGS) $^ $(LDFLAGS) -o $@

test_dlmopen: test_dlmopen.c
	@echo "==> Building $@"
	gcc -O2 -Wall -Wextra -o $@ $< -ldl

test_wrapper: test_wrapper.cpp $(APP_DIR)/MlcWrapper.cpp $(APP_DIR)/MlcWrapper.h
	@echo "==> Building $@"
	g++ -O2 -Wall -Wextra -std=c++17 \
	  -I$(APP_DIR) -I$(MLC)/src \
	  -o $@ test_wrapper.cpp $(APP_DIR)/MlcWrapper.cpp -ldl

test: all
	@echo "==> Running test_dlmopen (C, dlmopen mechanics)"
	@LD_LIBRARY_PATH=. ./test_dlmopen
	@echo
	@echo "==> Running test_wrapper (C++, MlcWrapper API)"
	@LD_LIBRARY_PATH=. ./test_wrapper

# === Bundled OpenSSL targets ===

bundled: libmoonlight-common-c-bundled.so test_dlmopen-bundled test_wrapper-bundled

openssl-static:
	@if [ ! -f $(OPENSSL_STATIC_LIB)/libcrypto.a ]; then \
	    echo "==> openssl-static missing, running build-openssl-static.sh"; \
	    ./build-openssl-static.sh; \
	fi

libmoonlight-common-c-bundled.so: $(MLC_SOURCES) | openssl-static
	@echo "==> Building $@ (with embedded static OpenSSL)"
	gcc -shared $(BUNDLED_CFLAGS) $(MLC_SOURCES) $(BUNDLED_LDFLAGS) -o $@
	@echo "    Verifying OpenSSL symbols are hidden from dynamic exports:"
	@if nm -D --defined-only $@ 2>/dev/null | grep -E " (EVP_|OPENSSL_|SSL_|ERR_set_mark)" >/dev/null; then \
	    echo "    !! WARNING: OpenSSL symbols are exported (--exclude-libs failed)"; \
	    nm -D --defined-only $@ | grep -E " (EVP_|OPENSSL_|SSL_)" | head -5; \
	else \
	    echo "    OK -- OpenSSL embedded but not exported"; \
	fi

test_dlmopen-bundled: test_dlmopen.c
	@echo "==> Building $@ (target: bundled .so)"
	gcc -O2 -Wall -Wextra \
	    -DMLC_PATH='"./libmoonlight-common-c-bundled.so"' \
	    -DMLC_LIBNAME='"libmoonlight-common-c-bundled.so"' \
	    -DMLC_BUNDLED_OPENSSL \
	    -o $@ $< -ldl

test_wrapper-bundled: test_wrapper.cpp $(APP_DIR)/MlcWrapper.cpp $(APP_DIR)/MlcWrapper.h
	@echo "==> Building $@ (target: bundled .so, dlmopen mode in wrapper)"
	g++ -O2 -Wall -Wextra -std=c++17 \
	    -DMLCWRAPPER_USE_DLMOPEN \
	    -DMLC_PATH='"./libmoonlight-common-c-bundled.so"' \
	    -I$(APP_DIR) -I$(MLC)/src \
	    -o $@ test_wrapper.cpp $(APP_DIR)/MlcWrapper.cpp -ldl

test-bundled: bundled
	@echo "==> Running test_dlmopen-bundled (C, against embedded-openssl .so)"
	@LD_LIBRARY_PATH=. ./test_dlmopen-bundled
	@echo
	@echo "==> Running test_wrapper-bundled (C++, MlcWrapper in dlmopen mode)"
	@LD_LIBRARY_PATH=. ./test_wrapper-bundled

clean:
	rm -f libmoonlight-common-c.so libmoonlight-common-c-bundled.so \
	      test_dlmopen test_dlmopen-bundled \
	      test_wrapper test_wrapper-bundled
