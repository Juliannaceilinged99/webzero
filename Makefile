# WebZero Makefile
# Targets:
#   make            → Linux native (requires gcc or musl-gcc)
#   make static     → Linux static binary (requires musl-gcc)
#   make windows    → Windows XP target (requires i686-w64-mingw32-gcc)
#   make debug      → Linux debug build with AddressSanitizer
#   make clean

# ─────────────────────────── sources ────────────────────────────────
SRCS = main.c \
       core/pool.c \
       core/bundle.c \
       core/router.c \
       core/vm.c \
       platform/linux.c

SRCS_WIN = main.c \
           core/pool.c \
           core/bundle.c \
           core/router.c \
           core/vm.c \
           platform/windows.c

# ─────────────────────────── Linux native ───────────────────────────
CC      = gcc
CFLAGS  = -std=c99 -O3 -Wall -Wextra -Wpedantic \
          -fno-stack-protector -fomit-frame-pointer \
          -I.
OUT     = webzero

all: $(OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Built $@ ($(shell wc -c < $@) bytes)"

# ─────────────────────────── Linux static (musl) ────────────────────
CC_MUSL   = musl-gcc
CFLAGS_ST = -std=c99 -O3 -static -Wall -Wextra -Wpedantic \
            -fno-stack-protector -fomit-frame-pointer \
            -I.
OUT_ST    = webzero-static

static: $(SRCS)
	$(CC_MUSL) $(CFLAGS_ST) -o $(OUT_ST) $^
	@echo "Built $(OUT_ST) ($(shell wc -c < $(OUT_ST)) bytes)"
	@strip $(OUT_ST)
	@echo "Stripped $(OUT_ST) ($(shell wc -c < $(OUT_ST)) bytes)"

# ─────────────────────────── Windows XP target ──────────────────────
CC_WIN    = i686-w64-mingw32-gcc
CFLAGS_WIN= -std=c99 -O3 -Wall -Wextra \
            -D_WIN32_WINNT=0x0501 \
            -fno-stack-protector -fomit-frame-pointer \
            -I.
LDFLAGS_WIN = -lws2_32 -lkernel32 -lmswsock
OUT_WIN   = webzero.exe

windows:
	$(CC_WIN) $(CFLAGS_WIN) -o $(OUT_WIN) $(SRCS_WIN) $(LDFLAGS_WIN)
	@echo "Built $(OUT_WIN)"

# ─────────────────────────── Debug build ────────────────────────────
CFLAGS_DBG = -std=c99 -O0 -g3 -Wall -Wextra -Wpedantic \
             -DWZ_DEBUG \
             -fsanitize=address,undefined \
             -I.
OUT_DBG = webzero-debug

debug: $(SRCS)
	$(CC) $(CFLAGS_DBG) -o $(OUT_DBG) $^
	@echo "Built $(OUT_DBG) (debug + ASan)"

# ─────────────────────────── Utilities ──────────────────────────────
clean:
	rm -f $(OUT) $(OUT_ST) $(OUT_DBG) $(OUT_WIN) *.o

size: all static
	@echo "=== Binary sizes ==="
	@ls -lh $(OUT) $(OUT_ST) 2>/dev/null

.PHONY: all static windows debug clean size
