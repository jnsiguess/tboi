#pragma once
#include <io.h>
#define O_WRONLY 1
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_CLOEXEC 0
#define open(p, f, m) _open(p, 0x0001 | 0x0100 | 0x0200, 0x0180)
