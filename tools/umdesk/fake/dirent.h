#pragma once
/* desk: a real directory listing on Windows (win.c), so the list and the start's cleanup run as on the phone */
typedef struct DIR DIR;
struct dirent { char d_name[260]; unsigned char d_type; };
#define DT_DIR 4
DIR *opendir(const char *p);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
