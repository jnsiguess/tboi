/* the tweaks desk: blocks below 4 GB (tweaks.c keeps addresses in `unsigned long`, 32 bits on Windows) -
   readable, writable and runnable (the image's trampolines) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *desk_low(size_t n) {
    for (unsigned long long at = 0x10000000ULL; at < 0xF0000000ULL; at += 0x01000000ULL) {
        void *p = VirtualAlloc((void *) at, n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}
