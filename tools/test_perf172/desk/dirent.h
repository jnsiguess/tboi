/* desk build only: a directory listing over FindFirstFile */
#ifndef DESK_DIRENT_H
#define DESK_DIRENT_H
struct dirent { char d_name[260]; unsigned char d_type; long d_ino; long d_off; };
typedef struct desk_dir DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
#endif
