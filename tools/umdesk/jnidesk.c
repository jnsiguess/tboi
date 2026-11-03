/*
 * DESK STAND WITH JAVA (hotfix 1.75): the real usermods.c behind the real UserMods.java, as one process of the
 * game - the installer's natives (listPut, gameReady, installing, restartOwed) answered by usermods.c itself, real
 * threads and real locks (rt/pthread.h), the disk's look on a thread of its own. The 1.74 stands called list_put
 * from C and ran UserMods.java with no layer at all: the order of calls the phone makes was never played.
 *
 * JniDesk.java (tools/umdesk/run_jni.sh) is the game around it: setConfigDir's start, the MODS page asking every
 * frame, the add window's question.
 */
#define main umdesk_main
#include "desk.c"
#undef main

static char *jdesk_str(JNIEnv *env, jstring s) { return jstr(env, s); }

/* setConfigDir's start (loader.c), with the app's own folder named first (UserMods.innerDir) */
JNIEXPORT void JNICALL Java_ru_isaac_port_JniDesk_start(JNIEnv *env, jclass cls, jstring jcfg, jstring jinner) {
    (void) cls;
    char *cfg = jdesk_str(env, jcfg), *inner = jdesk_str(env, jinner);
    if (inner && inner[0]) snprintf(g_inner, sizeof(g_inner), "%s/state", inner);
    shim_usermods_setup(cfg);
    /* Windows: Java names the folder with '\' (File), the layer with '/' - the phone has '/' on both sides, so the
       desk makes them one spelling, or listPut would refuse "the game reads mods from ..., not ..." here alone */
    for (char *p = g_dir; *p; p++) if (*p == '/') *p = '\\';
    free(cfg);
    free(inner);
}

/* the MODS page's ask, as tweaks.c's restart_row makes it every frame */
JNIEXPORT jboolean JNICALL Java_ru_isaac_port_JniDesk_changed(JNIEnv *env, jclass cls) {
    (void) env; (void) cls;
    return shim_usermods_changed() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_ru_isaac_port_JniDesk_count(JNIEnv *env, jclass cls) { (void) env; (void) cls; return shim_usermods_count(); }
JNIEXPORT jboolean JNICALL Java_ru_isaac_port_JniDesk_pending(JNIEnv *env, jclass cls, jint i) {
    (void) env; (void) cls;
    return shim_usermods_pending(i) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jstring JNICALL Java_ru_isaac_port_JniDesk_id(JNIEnv *env, jclass cls, jint i) {
    (void) cls;
    return (*env)->NewStringUTF(env, shim_usermods_id(i));
}
JNIEXPORT jboolean JNICALL Java_ru_isaac_port_JniDesk_on(JNIEnv *env, jclass cls, jint i) {
    (void) env; (void) cls;
    return shim_usermods_on(i) ? JNI_TRUE : JNI_FALSE;
}
/* this run's index, in order */
JNIEXPORT jstring JNICALL Java_ru_isaac_port_JniDesk_owners(JNIEnv *env, jclass cls) {
    (void) cls;
    static char s[MODS_MAX * (ID_MAX + 1) + 1];
    size_t n = 0;
    s[0] = 0;
    for (int o = 0; o < g_owners; o++) n += (size_t) snprintf(s + n, sizeof(s) - n, "%s%s", o ? "," : "", g_owner[o]);
    return (*env)->NewStringUTF(env, s);
}
/* the next ask looks at the disk at once (every 2 s on the phone) */
JNIEXPORT void JNICALL Java_ru_isaac_port_JniDesk_diskNow(JNIEnv *env, jclass cls) { (void) env; (void) cls; g_disk_at = 0; }
JNIEXPORT jint JNICALL Java_ru_isaac_port_JniDesk_diskBusy(JNIEnv *env, jclass cls) { (void) env; (void) cls; return g_disk_busy; }
