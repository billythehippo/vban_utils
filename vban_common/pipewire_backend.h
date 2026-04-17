#ifndef PIPEWIRE_BACKEND_H_
#define PIPEWIRE_BACKEND_H_

//#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>
#include <getopt.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include "vban_functions.h"
#include "udp.h"

extern mutexcond_t rx_run_mutex;


typedef struct
{
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_audio_info format;
    struct timestamp_delta callback_delta;
    void* user_data;
} pw_stream_data_t;


void* pw_rx_run_thread_handler(void* arg);

// __always_inline int pw_run_rx_stream(pw_stream_data_t *data, mutexcond_t* mutex)
// {
//     pthread_attr_init(&mutex->attr);
//     pthread_create(&mutex->tid, &mutex->attr, pw_rx_run_thread_handler, (void*)&data);

//     return 0;
// }


// __always_inline int pw_stop_rx_stream(pw_stream_data_t *data, mutexcond_t* mutex)
// {
//     pw_main_loop_quit(data->loop);
//     pthread_join(mutex->tid, NULL);

//     return 0;
// }

enum spa_audio_format format_vban_to_spa(enum VBanBitResolution format_vban);
static void on_stream_param_changed(void *userdata, uint32_t id, const struct spa_pod *param);
void help_emitter(void);
void help_receptor(void);
int get_emitter_options(vban_stream_context_t* stream, int argc, char *argv[]);
int get_receptor_options(vban_stream_context_t* stream, int argc, char *argv[]);
static void on_tx_process(void *userdata);
static void on_rx_process(void *userdata);
static void do_quit(void *userdata, int signal_number);
int pw_init_tx_stream(pw_stream_data_t *data, struct pw_stream_events *stream_events, vban_stream_context_t* vban_stream, spa_audio_format format);
int pw_init_rx_stream(pw_stream_data_t *data, struct pw_stream_events *stream_events, vban_stream_context_t* vban_stream, spa_audio_format format);
int pw_run_stream(pw_stream_data_t *data, mutexcond_t* mutex);
int pw_stop_stream(pw_stream_data_t *data, mutexcond_t* mutex);

#endif
