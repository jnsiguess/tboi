/* desk: the POSIX pieces usermods.c needs that MSVC's CRT has not - a directory listing and dlopen */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct DIR { HANDLE h; WIN32_FIND_DATAA fd; int first, done; struct { char d_name[260]; unsigned char d_type; } e; } DIR;

DIR *opendir(const char *p) {
    char pat[1200];
    snprintf(pat, sizeof(pat), "%s/*", p);
    DIR *d = (DIR *) calloc(1, sizeof(DIR));
    if (!d) return NULL;
    d->h = FindFirstFileA(pat, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

void *readdir(DIR *d) {
    if (d->done) return NULL;
    if (!d->first && !FindNextFileA(d->h, &d->fd)) { d->done = 1; return NULL; }
    d->first = 0;
    snprintf(d->e.d_name, sizeof(d->e.d_name), "%s", d->fd.cFileName);
    d->e.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
    return &d->e;
}

int closedir(DIR *d) {
    if (!d) return -1;
    FindClose(d->h);
    free(d);
    return 0;
}

void *dlopen(const char *name, int flags) {
    (void) flags;
    if (!strcmp(name, "libz.so")) return LoadLibraryA("C:/Program Files/Git/mingw64/bin/zlib1.dll");
    return LoadLibraryA(name);
}

void *dlsym(void *h, const char *name) { return (void *) GetProcAddress((HMODULE) h, name); }
