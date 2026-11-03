/*
 * modcheck_jni.c - the installer's judge of mod.json (plan 30.09, M6 and
 * 4.10 part 4): UserMods.checkModJson hands the zip's mod.json here, and
 * json.c - the same reader the game uses at start - says what is wrong.
 * One judge: org.json on the Java side forgives trailing commas, single
 * quotes, comments and repeated keys, json.c does not, and a mod taken by
 * the one and refused by the other would be installed and then "broken" in
 * the game with nobody told.
 *
 * Since hotfix 1.71 json.c reads a mod.json leniently (as 1.666's org.json
 * did): a slip is a note, not a refusal, and strictModJson hands the installer
 * the strict text it read, for org.json to take the fields from.
 *
 * The answer is the issues, one per line, "level [code]: text" (the lines
 * check.txt carries), "" when there are none. It goes back as UTF-8 bytes,
 * not a jstring: an issue quotes the author's text, and NewStringUTF takes
 * only modified UTF-8 - a character past the BMP there is an abort under
 * CheckJNI.
 */
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

JNIEXPORT jbyteArray JNICALL
Java_ru_isaac_port_UserMods_checkModJson(JNIEnv *env, jclass cls, jbyteArray jtext) {
    (void) cls;
    if (!jtext) return NULL;
    jsize n = (*env)->GetArrayLength(env, jtext);
    char *text = malloc((size_t) n + 1);
    modj_t *m = calloc(1, sizeof(*m));
    /* every line is at most level + code + text + punctuation */
    size_t room = (size_t) (MODJ_ISSUES_MAX + 1) * (MODJ_ISSUE_TEXT + 48);
    char *out = malloc(room);
    if (!text || !m || !out) {
        free(text);
        free(m);
        free(out);
        return NULL;                            /* the Java side reads null as "no judge" */
    }
    (*env)->GetByteArrayRegion(env, jtext, 0, n, (jbyte *) text);
    text[n] = 0;
    modj_read_text(m, text, (size_t) n);
    size_t len = 0;
    out[0] = 0;
    for (int i = 0; i < m->nissues && len < room; i++) {
        const modj_issue_t *is = &m->issues[i];
        int k = snprintf(out + len, room - len, "%s [%s]: %s\n",
                         modj_level_name(is->level), modj_reason_code(is->reason), is->text);
        if (k > 0) len += (size_t) k < room - len ? (size_t) k : room - len - 1;
    }
    if (m->issues_lost > 0 && len < room) {
        int k = snprintf(out + len, room - len, "... and %d more\n", m->issues_lost);
        if (k > 0) len += (size_t) k < room - len ? (size_t) k : room - len - 1;
    }
    modj_free(m);
    free(m);
    free(text);
    jbyteArray res = (*env)->NewByteArray(env, (jsize) len);
    if (res) (*env)->SetByteArrayRegion(env, res, 0, (jsize) len, (const jbyte *) out);
    free(out);
    return res;
}

/* the mod.json as json.c reads it, written out as strict JSON, or null when even the lenient reading fails */
JNIEXPORT jbyteArray JNICALL
Java_ru_isaac_port_UserMods_strictModJson(JNIEnv *env, jclass cls, jbyteArray jtext) {
    (void) cls;
    if (!jtext) return NULL;
    jsize n = (*env)->GetArrayLength(env, jtext);
    char *text = malloc((size_t) n + 1);
    if (!text) return NULL;
    (*env)->GetByteArrayRegion(env, jtext, 0, n, (jbyte *) text);
    text[n] = 0;
    size_t on = 0;
    char *o = json_lenient_text(text, (size_t) n, &on, NULL);
    free(text);
    if (!o) return NULL;
    jbyteArray res = (*env)->NewByteArray(env, (jsize) on);
    if (res) (*env)->SetByteArrayRegion(env, res, 0, (jsize) on, (const jbyte *) o);
    free(o);
    return res;
}
