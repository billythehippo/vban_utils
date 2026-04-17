#include "../vban_common/jack_backend.h"
#include "../vban_common/udp.h"

struct timespec ttso, ttsn;

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#define TARGET_CPU 1
#define INTERVAL_NS 333333
#define PERIOD_NS 666666

void* tx_timer_thread(void* arg)
{
    vban_stream_context_t* context = (vban_stream_context_t*)arg;

    // // Bind to CPU core (Affinity)
    // cpu_set_t cpuset;
    // CPU_ZERO(&cpuset);
    // CPU_SET(TARGET_CPU, &cpuset);
    // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // Set high prio (SCHED_FIFO)
    struct sched_param param;
    param.sched_priority = 90;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
    {
        fprintf(stderr, "Error! Cannot set pthread_setschedparam!\r\nSet up limits or run as root!\r\n");
    }

    // Создание timerfd (Монотонные часы)
    context->txtimer->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (context->txtimer->tfd == -1)
    {
        fprintf(stderr, "Error! Cannot create timerfd descriptor!\r\n");
        exit(1);
    }

    int epfd = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = context->txtimer->tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, context->txtimer->tfd, &event);

    struct epoll_event events[1];
    uint64_t expirations;

    while (context->flags&SENDING) // running — атомарный флаг
    {
        int nfds = epoll_wait(epfd, events, 1, -1);
        if (nfds > 0 && (events[0].events & EPOLLIN))
        {
            // Обязательно вычитываем данные, иначе epoll будет срабатывать постоянно
            read(context->txtimer->tfd, &expirations, sizeof(expirations));

            //clock_gettime(CLOCK_MONOTONIC_RAW, &ttsn);
            //fprintf(stderr, "%ld\r\n", ttsn.tv_nsec - ttso.tv_nsec);
            //ttso = ttsn;

            vban_send_t32_fragment(context, 0, 2);
            if (context->txdata_ind == context->tx_transactions - 1)
            {
                context->txdata_ind = 0;
                //reset_timer(&context->txtimer->tfd, 0, 0, 0, 0);
            }
            else context->txdata_ind++;
        }
    }

    close(context->txtimer->tfd);
    return NULL;
}

#endif


int main(int argc, char *argv[])
{
    vban_stream_context_t stream;

    memset(&stream, 0, sizeof(vban_stream_context_t));

    stream.txport = 6980;
    stream.vban_output_format = VBAN_BITFMT_32_FLOAT;
    stream.samplerate = 48000;
    stream.tx_nframes = 32;
    stream.nbinputs = 2;

    if (get_emitter_options(&stream, argc, argv)) return 1;

    if (stream.txport!=0) // UDP tx mode
    {
        stream.txsock = udp_init(0, stream.txport, NULL, stream.iptxaddr, 0, 6, 1);
        if (stream.txsock==NULL)
        {
            fprintf(stderr, "Cannot init UDP socket!\r\n");
            return 1;
        }
        set_recverr(stream.txsock->fd);
        stream.pd[0].fd = stream.txsock->fd;
    }
    else // PIPE tx mode
    {
        if (strncmp(stream.pipename, "stdin", 6)) // named pipe
        {
            stream.pipedesc = open(stream.pipename, O_WRONLY);
            mkfifo(stream.pipename, 0666);
        }
        else stream.pipedesc = 0; // stdin
        stream.pd[0].fd = stream.pipedesc;
    }

    //Create stream
    stream.txpacket.header.vban = VBAN_HEADER_FOURC;
    stream.txpacket.header.format_SR = VBAN_SR_MASK&vban_get_format_SR(stream.samplerate);
    stream.txpacket.header.format_bit = stream.vban_output_format;
    stream.txpacket.header.format_nbc = stream.nbinputs - 1;
    stream.txpacket.header.format_nbs = stream.vban_nframes_pac - 1;
    strncpy(stream.txpacket.header.streamname, stream.tx_streamname, (strlen(stream.tx_streamname)>16 ? 16 : strlen(stream.tx_streamname)));

    stream.txmidipac.header.vban = VBAN_HEADER_FOURC;
    stream.txmidipac.header.format_SR = VBAN_PROTOCOL_SERIAL;
    strncpy(stream.txmidipac.header.streamname, stream.tx_streamname, (strlen(stream.tx_streamname)>16 ? 16 : strlen(stream.tx_streamname)));

    stream.txmeta.header.vban = VBAN_HEADER_FOURC;
    stream.txmeta.header.format_SR = VBAN_PROTOCOL_USER;
    strncpy(stream.txmeta.header.streamname, stream.tx_streamname, (strlen(stream.tx_streamname)>16 ? 16 : strlen(stream.tx_streamname)));
    strncpy(stream.txmeta.data, "META", 4);

    stream.flags|= TRANSMITTER;

    jack_stream_data_t jack_stream;
    memset(&jack_stream, 0, sizeof(jack_stream_data_t));
    jack_stream.user_data = (void*)&stream;

    jack_init_tx_stream(&jack_stream);
    stream.flags|= SENDING;

    if (stream.flags&SLICING)
    {
#ifdef __linux__
        stream.txtimer = (timerfd_timer_t*)calloc(1, sizeof(timerfd_timer_t));

        pthread_attr_init(&stream.txtimer->timmutex.attr);
        if (pthread_create(&stream.txtimer->timmutex.tid, &stream.txtimer->timmutex.attr, tx_timer_thread, (void*)&stream) != 0)
        {
            fprintf(stderr, "Cannot create TX timer thread, SLICING is OFF!\r\n");
            stream.flags&=~SLICING;
        }
        else
        {
            ;
        }
#else
        stream.flags&=~SLICING;
        fprintf(stderr, "Sorry! SLICING mode is not ready for non-Linux platforms yet!\r\n");
#endif
    }

    jack_run_tx_stream(&jack_stream);

    while(1) sleep(1);

    return 0;
}
