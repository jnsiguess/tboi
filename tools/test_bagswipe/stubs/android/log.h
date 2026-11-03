/* desk build only: items.c logs nothing with ISAAC_DIAG 0 */
#ifndef TEST_ANDROID_LOG_H
#define TEST_ANDROID_LOG_H
#define ANDROID_LOG_INFO 4
#define __android_log_print(...) 0
#endif
