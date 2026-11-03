/* desk build only: the AAudio the port uses, faked by the test (test_audio.c) */
#ifndef DESK_AAUDIO_H
#define DESK_AAUDIO_H
#include <stdint.h>
typedef int32_t aaudio_result_t, aaudio_stream_state_t, aaudio_performance_mode_t, aaudio_format_t,
                aaudio_sharing_mode_t, aaudio_data_callback_result_t;
typedef struct AAudioStreamStruct AAudioStream;
typedef struct AAudioStreamBuilderStruct AAudioStreamBuilder;
enum { AAUDIO_OK = 0, AAUDIO_ERROR_DISCONNECTED = -899, AAUDIO_ERROR_INTERNAL = -896, AAUDIO_ERROR_TIMEOUT = -885 };
enum { AAUDIO_FORMAT_PCM_I16 = 1, AAUDIO_FORMAT_PCM_FLOAT = 2 };
enum { AAUDIO_STREAM_STATE_UNINITIALIZED = 0, AAUDIO_STREAM_STATE_UNKNOWN, AAUDIO_STREAM_STATE_OPEN,
       AAUDIO_STREAM_STATE_STARTING, AAUDIO_STREAM_STATE_STARTED, AAUDIO_STREAM_STATE_PAUSING,
       AAUDIO_STREAM_STATE_PAUSED, AAUDIO_STREAM_STATE_FLUSHING, AAUDIO_STREAM_STATE_FLUSHED,
       AAUDIO_STREAM_STATE_STOPPING, AAUDIO_STREAM_STATE_STOPPED, AAUDIO_STREAM_STATE_CLOSING,
       AAUDIO_STREAM_STATE_CLOSED, AAUDIO_STREAM_STATE_DISCONNECTED };
enum { AAUDIO_SHARING_MODE_EXCLUSIVE = 0, AAUDIO_SHARING_MODE_SHARED = 1 };
enum { AAUDIO_PERFORMANCE_MODE_NONE = 10, AAUDIO_PERFORMANCE_MODE_POWER_SAVING = 11,
       AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12 };
enum { AAUDIO_CALLBACK_RESULT_CONTINUE = 0, AAUDIO_CALLBACK_RESULT_STOP = 1 };
typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(AAudioStream *, void *, void *, int32_t);
typedef void (*AAudioStream_errorCallback)(AAudioStream *, void *, aaudio_result_t);
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, aaudio_format_t f);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t n);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, aaudio_performance_mode_t m);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b, aaudio_sharing_mode_t m);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *u);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *u);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **s);
aaudio_result_t AAudioStream_close(AAudioStream *s);
aaudio_result_t AAudioStream_requestStart(AAudioStream *s);
aaudio_result_t AAudioStream_requestPause(AAudioStream *s);
aaudio_result_t AAudioStream_requestFlush(AAudioStream *s);
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t in,
                                                aaudio_stream_state_t *next, int64_t timeout_ns);
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s);
int32_t AAudioStream_getXRunCount(AAudioStream *s);
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s);
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t n);
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s);
int32_t AAudioStream_getSampleRate(AAudioStream *s);
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *s);
const char *AAudio_convertResultToText(aaudio_result_t r);
const char *AAudio_convertStreamStateToText(aaudio_stream_state_t s);
#endif
