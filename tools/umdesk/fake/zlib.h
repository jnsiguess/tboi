#pragma once
/* desk: the NDK's own zlib.h - the z_stream the phone's libz.so takes; the build puts the NDK sysroot's
   include folder last (-idirafter) */
#include_next <zlib.h>
