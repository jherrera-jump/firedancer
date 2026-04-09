FD_HAS_ASAN:=1
FD_HAS_DEEPASAN:=1
CPPFLAGS+=-DFD_HAS_ASAN=1
CFLAGS+=-DFD_HAS_DEEPASAN=1

# ASan's fake stack allocator guarantees only 32-byte alignment.
# GCC with AVX-512 may emit vmovdqa64 (requires 64-byte alignment)
# for stack-local memcpy of 64-byte structs, causing SIGSEGV.
# Limiting preferred vector width to 256 bits avoids this.
CPPFLAGS+=-fsanitize=address,leak  -fno-omit-frame-pointer -mprefer-vector-width=256

LDFLAGS+=-fsanitize=address,leak
