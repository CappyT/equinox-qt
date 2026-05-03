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

CFLAGS := -O2 -std=gnu99 -fPIC \
  -I$(ENET)/include -I$(MLC)/src \
  -I$(MLC)/nanors -I$(MLC)/nanors/deps -I$(MLC)/nanors/deps/obl \
  -DHAS_SOCKLEN_T -DHAVE_CLOCK_GETTIME=1 \
  $(shell pkg-config --cflags openssl)

LDFLAGS := -lpthread $(shell pkg-config --libs openssl)

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

.PHONY: all test clean

all: libmoonlight-common-c.so test_dlmopen

libmoonlight-common-c.so: $(MLC_SOURCES)
	@echo "==> Building $@"
	gcc -shared $(CFLAGS) $^ $(LDFLAGS) -o $@

test_dlmopen: test_dlmopen.c
	@echo "==> Building $@"
	gcc -O2 -Wall -Wextra -o $@ $< -ldl

test: all
	@echo "==> Running tests"
	@LD_LIBRARY_PATH=. ./test_dlmopen

clean:
	rm -f libmoonlight-common-c.so test_dlmopen
