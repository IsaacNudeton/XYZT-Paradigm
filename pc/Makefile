# Makefile — Unified XYZT PC Engine
#
# CUDA Toolkit 13.1, nvcc, Windows, RTX 2080 Super (sm_75)
#
# Build: make
# Clean: make clean
# Test:  make test
# Bench: make bench

NVCC     = nvcc
ARCH     = sm_75
OPT      = -O2
CFLAGS   = -Xcompiler "/W3"
LDFLAGS  = -lm

# Source files
CU_SRC   = main.cu substrate.cu
C_SRC    = engine.c onetwo.c wire.c transducer.c reporter.c

# Output
TARGET   = xyzt_pc.exe

all: $(TARGET)

$(TARGET): $(CU_SRC) $(C_SRC)
	$(NVCC) $(OPT) -arch=$(ARCH) $(CFLAGS) -o $(TARGET) $(CU_SRC) $(C_SRC) $(LDFLAGS)

# Test: build and run self-test
test: $(TARGET)
	./$(TARGET) test

# Benchmark: GPU performance
bench: $(TARGET)
	./$(TARGET) bench

# Run: interactive mode
run: $(TARGET)
	./$(TARGET) run

clean:
	del /Q $(TARGET) 2>nul

.PHONY: all test bench run clean
