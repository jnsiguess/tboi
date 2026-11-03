/* the reset desk: the log, printed when DESK_LOG is set (desk.c) */
#ifndef DESK_ANDROID_LOG_H
#define DESK_ANDROID_LOG_H
#define ANDROID_LOG_INFO 4
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#endif
