#ifndef VBAN_FUNCTIONS_H
#define VBAN_FUNCTIONS_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <arpa/inet.h>
#include <errno.h>
//#include <signal.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include "vban.h"
#include "vban_client_list.h"
#include "ringbuffer.h"
#include "zita-resampler/vresampler.h"
#include "udp.h"
#include "popen2.h"

// TYPE DEFS

#define CMDLEN_MAX 600

typedef struct timestamp_delta
{
    struct timespec ts_new;
    struct timespec ts_old;
    int deltat;
    double deltatf[5];
} timestamp_delta;


typedef struct
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_mutex_t threadlock;// = PTHREAD_MUTEX_IN ITIALIZER;
    pthread_cond_t  dataready;// = PTHREAD_COND_INITIALIZER;
} mutexcond_t;

typedef struct timer_simple_t
{
    struct pollfd tds[1];
    uint64_t tval;
    int tlen;
    mutexcond_t timmutex;
} timer_simple_t;

#ifdef __linux__
typedef struct timerfd_timer_t
{
    timer_t t;
    int tfd;
    struct itimerspec ts;
    //uint64_t tval;
    //int tlen;
    mutexcond_t timmutex;
} timerfd_timer_t;
#endif

// typedef struct
// {
//     udpc_t* udpsocket;
//     mutexcond_t* mutexcond;
//     ringbuffer_t* ringbuffer;
// } rxcontext_t;

// CONTEXT VBAN

typedef struct vban_stream_context_t {
    char rx_streamname[VBAN_STREAM_NAME_SIZE];
    char tx_streamname[VBAN_STREAM_NAME_SIZE];
    char servername[VBAN_STREAM_NAME_SIZE * 2];
    uint16_t nbinputs = 0;
    uint16_t nboutputs = 0;
    uint16_t nframes;
    uint8_t nperiods;
    int samplerate;
    int samplerate_resampler;
    char* portbuf;
    char* txbuf;
    float* rxbuf;
    int portbuflen;
    int txbuflen;
    int rxbuflen;
    int lagrange_num;
    ringbuffer_t* ringbuffer;
    ringbuffer_t* ringbuffer_midi;
    char iptxaddr[16];
    union
    {
        uint32_t iptx;
        uint8_t iptx_bytes[4];
    };
    uint16_t txport;
    union
    {
        uint32_t iprx;
        uint8_t iprx_bytes[4];
    };
    union
    {
        uint32_t iplocal;
        uint8_t iplocal_bytes[4];
    };
    uint16_t rxport;
    uint16_t ansport;
    char pipename[32];
    int pipedesc;
    pollfd pd[1];
    uint32_t nu_frame;
    uint8_t vban_input_format;
    uint8_t vban_output_format;
    uint8_t redundancy;
    int vban_nframes_pac;
    uint16_t pacnum;
    uint16_t pacnum_t32;
    uint16_t txdata_ind;
    uint16_t pacdatalen;
    uint16_t tx_transactions;
    uint16_t tx_nframes;
    uint32_t slice_period;
    uint8_t lost_pac_cnt;
    udpc_t* rxsock;
    udpc_t* txsock;
    mutexcond_t rxmutex;
    mutexcond_t cmdmutex;
    uint16_t flags;
    char* command;
    char* message;
    VBanPacket txpacket;
    VBanPacket txmidipac;
    VBanPacket info;
    VBanPacket txmeta;
    char* jack_server_name;
    VResampler* resampler;
    float* resampler_inbuf;
    int resampler_inbuflen;
//    int resampler_infrag;
    float* resampler_outbuf;
    int resampler_outbuflen;

    struct timespec input_ts;
    struct timespec time_old;
    struct timespec time_cycle_start;
    int64_t deltaTold;
    int64_t deltaT[4] = {0, 0, 0, 0};
    struct timestamp_delta rx_cycle_delta;
    int cycle_pac_cnt = 0;
    int cycle_frames = 0;
#define CFMSIZE 5
    int cframes_marr[CFMSIZE];
    int cframes;
    uint8_t cfind;
    int rbfill;
    double fsin;
    double fslo;
    double rratio;
    double rratiof;
    double rbfill_integral;
    uint16_t add_delay_frames;

#ifdef __linux__
    timerfd_timer_t* txtimer;
#endif
} vban_stream_context_t;

#ifdef __linux__
#include <linux/errqueue.h>
#endif

typedef struct vban_metadata{
    uint32_t metaforc = 'ATEM';
    scm_timestamping timestamps;
} vban_metadata;
//FLAGS COMMON
// flags defines
#define RECEIVER        0x0001
#define TRANSMITTER     0x0002
#define RECEIVING       0x0004
#define SENDING         0x0008
#define CMD_PRESENT     0x0010
#define CONNECTED       0x0020
#define CORRECTION_ON   0x0040
#define SLICING         0x0080
#define MULTISTREAM     0x0100
#define DEVICE_MODE     0x0200
#define AUTOCONNECT     0x0400
#define RESAMPLER       0x0800
#define MSG_PRESENT     0x1000
#define NET_XRUN        0x4000
#define HALT            0x8000

typedef struct vban_multistream_context_t
{
    timer_simple_t offtimer;
    uint8_t vban_clients_min = 0;
    uint8_t vban_clients_max = 0;
    uint8_t active_clients_num = 0;
    uint8_t active_clients_ind = 0;
    client_id_t* clients;
    client_id_t* client;
    uint16_t* flags;
} vban_multistream_context_t;

#define CARD_NAME_LENGTH 32
//#ifdef __linux__
//#endif

#define CMD_SIZE 300

void vban_fill_receptor_info(vban_stream_context_t* context);
void tune_tx_packets(vban_stream_context_t* stream);
//void* tx_timer_thread(void* arg);
void reset_timer(int* timerfd, uint64_t interval_sec, uint64_t interval_nsec, uint64_t period_sec, uint64_t period_nsec);
void* timerThread(void* arg);
void* rxThread(void* arg);


inline int cfmed(int* median_array)
{
    int min;
    uint16_t mind;
    uint16_t mid = (CFMSIZE>>1) + 1;
    int array[CFMSIZE];
    memcpy(array, median_array, CFMSIZE*sizeof(int));
    for (int i = 0; i < CFMSIZE - 1; i++)
    {
        min = array[i];
        mind = i;
        for (int j = i + 1; j < CFMSIZE; j++)
        {
            if (array[j] < min)
            {
                min = array[j];
                mind = j;
            }
        }
        array[mind] = array[i];
        array[i] = min;
        if (i == mid) break;
    }
    return array[mid];
}


inline uint16_t int16betole(u_int16_t input)
{
    return ((((uint8_t*)&input)[0])<<8) + ((uint8_t*)&input)[1];
}


inline uint32_t int32betole(uint32_t input)
{
    return (((((((uint8_t*)&input)[0]<<8)+((uint8_t*)&input)[1])<<8)+((uint8_t*)&input)[2])<<8)+((uint8_t*)&input)[3];
}


inline void vban_inc_nuFrame(VBanHeader* header)
{
    header->nuFrame++;
}


inline int vban_sample_convert(void* dstptr, uint8_t format_bit_dst, void* srcptr, uint8_t format_bit_src, int num)
{
    int ret = 0;
    uint8_t* dptr;
    uint8_t* sptr;
    int32_t tmp;

    int dst_sample_size;
    int src_sample_size;

    if (format_bit_dst==format_bit_src)
    {
        memcpy(dstptr, srcptr, VBanBitResolutionSize[format_bit_dst]*num);
        return 0;
    }

    dptr = (uint8_t*)dstptr;
    sptr = (uint8_t*)srcptr;

    dst_sample_size = VBanBitResolutionSize[format_bit_dst];
    src_sample_size = VBanBitResolutionSize[format_bit_src];

    switch (format_bit_dst)
    {
    case VBAN_BITFMT_8_INT:
        switch (format_bit_src)
        {
        case VBAN_BITFMT_16_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_24_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[sample*dst_sample_size] = sptr[2 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[sample*dst_sample_size] = sptr[3 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_FLOAT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[sample*dst_sample_size] = (int8_t)roundf((float)(1<<7)*((float*)sptr)[sample]);
            }
            break;
        default:
            fprintf(stderr, "Convert Error! Unsuppotred source format!%d\n", format_bit_src);
            ret = 1;
        }
        break;
    case VBAN_BITFMT_16_INT:
        switch (format_bit_src)
        {
        case VBAN_BITFMT_8_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_24_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
                dptr[1 + sample*dst_sample_size] = sptr[2 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = sptr[2 + sample*src_sample_size];
                dptr[1 + sample*dst_sample_size] = sptr[3 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_FLOAT:
            for (int sample=0; sample<num; sample++)
            {
                //((int16_t*)dptr)[sample*dst_sample_size] = (int16_t)roundf((float)(1<<15)*((float*)sptr)[sample]);
                //((int16_t*)dptr)[sample] = (int16_t)roundf((float)(1<<15)*((float*)sptr)[sample]);
                tmp = (int32_t)roundf((float)(1<<31)*((float*)sptr)[sample]);
                memcpy(&dptr[sample*dst_sample_size], (uint8_t*)&tmp + 2, dst_sample_size);
            }
            break;
        default:
            fprintf(stderr, "Convert Error! Unsuppotred source format!%d\n", format_bit_src);
            ret = 1;
        }
        break;
    case VBAN_BITFMT_24_INT:
        switch (format_bit_src)
        {
        case VBAN_BITFMT_8_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = 0;
                dptr[2 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_16_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
                dptr[2 + sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
                dptr[1 + sample*dst_sample_size] = sptr[2 + sample*src_sample_size];
                dptr[2 + sample*dst_sample_size] = sptr[3 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_FLOAT:
            for (int sample=0; sample<num; sample++)
            {
                //((int32_t*)dptr)[sample*dst_sample_size] = (int32_t)roundf((float)(1<<23)*((float*)sptr)[sample]);
                tmp = (int32_t)roundf((float)(1<<31)*((float*)sptr)[sample]);
                memcpy(&dptr[sample*dst_sample_size], (uint8_t*)&tmp + 1, dst_sample_size);
            }
            break;
        default:
            fprintf(stderr, "Convert Error! Unsuppotred source format!%d\n", format_bit_src);
            ret = 1;
        }
        break;
    case VBAN_BITFMT_32_INT:
        switch (format_bit_src)
        {
        case VBAN_BITFMT_8_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = 0;
                dptr[2 + sample*dst_sample_size] = 0;
                dptr[3 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_16_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = 0;
                dptr[2 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
                dptr[3 + sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_24_INT:
            for (int sample=0; sample<num; sample++)
            {
                dptr[0 + sample*dst_sample_size] = 0;
                dptr[1 + sample*dst_sample_size] = sptr[0 + sample*src_sample_size];
                dptr[2 + sample*dst_sample_size] = sptr[1 + sample*src_sample_size];
                dptr[3 + sample*dst_sample_size] = sptr[2 + sample*src_sample_size];
            }
            break;
        case VBAN_BITFMT_32_FLOAT:
            for (int sample=0; sample<num; sample++)
            {
                ((int32_t*)dptr)[sample] = (int32_t)roundf((float)(1<<31)*((float*)sptr)[sample]);
            }
            break;
        default:
            fprintf(stderr, "Convert Error! Unsuppotred source format!%d\n", format_bit_src);
            ret = 1;
        }
        break;
    case VBAN_BITFMT_32_FLOAT:
        switch (format_bit_src)
        {
        case VBAN_BITFMT_8_INT:
            for (int sample=0; sample<num; sample++)
            {
                ((float*)dptr)[sample] = (float)sptr[sample]/(float)(1<<7);
            }
            break;
        case VBAN_BITFMT_16_INT:
            for (int sample=0; sample<num; sample++)
            {
                ((float*)dptr)[sample] = (float)(((int16_t*)sptr)[sample])/(float)(1<<15);
            }
            break;
        case VBAN_BITFMT_24_INT:
            for (int sample=0; sample<num; sample++)
            {
                ((float*)dptr)[sample] = (float)((((int8_t)sptr[2 + sample*src_sample_size])<<16)+(sptr[1 + sample*src_sample_size]<<8)+sptr[0 + sample*src_sample_size])/(float)(1<<23);
            }
            break;
        case VBAN_BITFMT_32_INT:
            for (int sample=0; sample<num; sample++)
            {
                ((float*)dptr)[sample] = (float)(((int32_t*)sptr)[sample])/(float)(1<<31);
            }
            break;
        default:
            fprintf(stderr, "Convert Error! Unsuppotred source format!%d\n", format_bit_src);
            ret = 1;
        }
        break;
    default:
        fprintf(stderr, "Convert Error! Unsuppotred destination format!%d\n", format_bit_dst);
        ret = 1;
    }
    return ret;
}


inline int vban_get_format_SR(long host_samplerate)
{
    int i;
    for (i=0; i<VBAN_SR_MAXNUMBER; i++) if (host_samplerate==VBanSRList[i]) return i;
    return -1;
}


inline uint vban_strip_vban_packet(uint8_t format_bit, uint16_t nbchannels)
{
    uint framesize = VBanBitResolutionSize[format_bit]*nbchannels;
    uint nframes = VBAN_DATA_MAX_SIZE/framesize;
    if (nframes>VBAN_SAMPLES_MAX_NB) nframes = VBAN_SAMPLES_MAX_NB;
    return nframes*framesize;
}


inline uint vban_strip_vban_data(uint datasize, uint8_t format_bit, uint16_t nbchannels)
{
    uint framesize = VBanBitResolutionSize[format_bit]*nbchannels;
    uint nframes = datasize/framesize;
    if (nframes>VBAN_SAMPLES_MAX_NB) nframes = VBAN_SAMPLES_MAX_NB;
    return nframes*framesize;
}


inline uint vban_calc_nbs(uint datasize, uint8_t resolution, uint16_t nbchannels)
{
    return datasize/VBanBitResolutionSize[resolution]*nbchannels;
}


inline uint vban_packet_to_float_buffer(uint pktdatalen, uint8_t resolution)
{
    return sizeof(float)*pktdatalen/VBanBitResolutionSize[resolution];
}


inline int file_exists(const char* __restrict filename)
{
    if (access(filename, F_OK)==0) return 1;
    return 0;
}


inline void vban_free_rx_ringbuffer(ringbuffer_t* ringbuffer)
{
    if (ringbuffer!= NULL) ringbuffer_free(ringbuffer);
}


inline int calc_delta_filtered(struct timestamp_delta* deltastruct, uint8_t order = 3, float weight = 0.05)
{
    if ((deltastruct->ts_new.tv_sec==deltastruct->ts_old.tv_sec)&&(deltastruct->ts_new.tv_nsec==deltastruct->ts_old.tv_nsec)) clock_gettime(CLOCK_MONOTONIC_RAW, &deltastruct->ts_new);
    if (order > 5) order = 5;
    if (deltastruct->ts_old.tv_sec)
    {
        deltastruct->deltat = (deltastruct->ts_new.tv_sec - deltastruct->ts_old.tv_sec) * 1000000000 + deltastruct->ts_new.tv_nsec - deltastruct->ts_old.tv_nsec;
        if (deltastruct->deltatf[order - 1] == 0)
        {
            deltastruct->deltatf[0] = (double)deltastruct->deltat;
            for (int f = 1; f < order; f++) deltastruct->deltatf[f] = deltastruct->deltatf[0];
        }
        else
        {
            deltastruct->deltatf[0] = (1 - weight)*deltastruct->deltatf[0] + weight*(double)deltastruct->deltat;
            for (int f = 1; f < order; f++) deltastruct->deltatf[f] = (1 - weight)*deltastruct->deltatf[f] + weight*deltastruct->deltatf[f - 1];
        }
    }
    deltastruct->ts_old = deltastruct->ts_new;
    return 0;
}


inline double calc_rbfill_pi(int rbfill_needed, int rbfill_real, double* rbf_integral, double weight = 1, double kp = 1, double ki = 0.01, double kd = 0)
{
    double ppm;
    if (rbf_integral == NULL) return 0;
    int rbfill_delta = rbfill_needed - rbfill_real;
    *rbf_integral+= (double)rbfill_delta;
    if (*rbf_integral > 1000) *rbf_integral = 1000;
    if (*rbf_integral <-1000) *rbf_integral =-1000;
    ppm = weight*(kp * rbfill_delta + ki * *rbf_integral);
    if (ppm < 0) return (1 + ppm);
    return (1 + 2*ppm);
}


inline void calc_ratio_correction(vban_stream_context_t* context, timestamp_delta* callback_delta, int framesize, int rbfill_target)
{
    calc_delta_filtered(callback_delta, 3, 0.005);
    if (callback_delta->deltatf[2]!= 0) context->fslo = (double)context->nframes*1e9/callback_delta->deltatf[2];
    //else fprintf(stderr, "Ёбань!\r\n");
    if (context->fslo!= 0) context->rratio = context->fsin/context->fslo;
    //else fprintf(stderr, "Ёбань сраная!\r\n");
    // if (context->rratio > 1.002) context->rratio = 1.002;
    // else if (context->rratio < 0.998) context->rratio = 0.998;
    context->rratiof = 0.99*context->rratiof + 0.01*context->rratio;
    if (abs(context->rratiof - 1) > 0.0025) context->rratiof = 1;
    //int rbfill = ringbuffer_read_space(context->ringbuffer)/framesize;
    double rbfill_pi = calc_rbfill_pi(rbfill_target, context->rbfill, &context->rbfill_integral, 0.0001, 5, 0.005);
    context->rratio = 0.99 * context->rratiof + 0.01 * rbfill_pi;
    if (context->rratio > 1.003) context->rratio = 1.003;
    else if (context->rratio < 0.998) context->rratio = 0.998;
    fprintf(stderr, "fsin %f, fslocal %f, ratio %f fill %d\r", context->fsin, context->fslo, context->rratio, context->rbfill);
}


inline void correct_samplerate(vban_stream_context_t* context, int framesize, timestamp_delta* cbdelta)
{
    int32_t ust;
    //ust = context->cframes + context->nframes;
    ust = (context->cframes > context->nframes ? context->cframes : context->nframes);
    ust = ust + (ust>>1) + (ust>>2)*(context->redundancy) + 3;
    fprintf(stderr, "ust %d %d ", ust, context->cframes);
    if (context->cframes <= context->nframes)
        context->rbfill = ringbuffer_read_space(context->ringbuffer)/framesize;
    calc_ratio_correction(context, cbdelta, framesize, ust + context->add_delay_frames);
    context->resampler->set_rratio(context->rratio);
}


inline void vban_compute_rx_ringbuffer(int nframes, int nframes_pac, int nbchannels, int redundancy, ringbuffer_t** ringbuffer, uint8_t corr = 0)
{
    char* zeros;
    char div = 1;
    char mod = 1;
    int rbsize;

    mod+= corr;
    nframes = (nframes>nframes_pac ? nframes : nframes_pac);
    if (*ringbuffer!= NULL)
    {
        rbsize = (**ringbuffer).size;
        fprintf(stderr, "Cleaning old ringbuffer %d bytes...\r\n", rbsize);
        ringbuffer_free(*ringbuffer);
    }
    rbsize = 2 * nframes * nbchannels * (redundancy + 1) * sizeof(float);
    *ringbuffer = ringbuffer_create(rbsize);
    fprintf(stderr, "Creating ringbuffer %d bytes (%d frames)\r\n", rbsize, rbsize>>2);
    memset((*ringbuffer)->buf, 0, (*ringbuffer)->size);
    if (redundancy>0) // TODO : REWORK THIS!!!
    {
        if (redundancy<2) div = 2;
        zeros = (char*)calloc(1, (*ringbuffer)->size>>div);
        ringbuffer_write(*ringbuffer, zeros, (*ringbuffer)->size>>div);
        free(zeros);
    }
}


inline void vban_free_line_buffer(void** buffer, int* bufsize)
{
    if (*buffer!= NULL)
    {
        free(*buffer);
        *buffer = NULL;
    }
    bufsize = 0;
}


inline void vban_compute_rx_buffer(int nframes, int nbchannels, float** rxbuffer, int* rxbuflen, int lagrange_add = 3)
{
    vban_free_line_buffer((void**)rxbuffer, rxbuflen);
    *rxbuflen = nbchannels*(nframes + lagrange_add);
    *rxbuffer = (float*)malloc(*rxbuflen*sizeof(float));
}


inline int vban_compute_line_buffer(char** buffer, int nframes, int nbchannels, int bitres)
{
    int size = nframes*nbchannels*bitres;
    vban_free_line_buffer((void**)buffer, NULL);
    *buffer = (char*)malloc(size);
    return size;
}


inline uint8_t vban_compute_tx_packets(uint16_t* pacdatalen, uint16_t* pacnum, int nframes, int nbchannels, int bitres)
{
    *pacdatalen = nframes*nbchannels*bitres;
    *pacnum = 1;
    while((*pacdatalen>VBAN_DATA_MAX_SIZE)||((*pacdatalen/(bitres*nbchannels))>256))
    {
        *pacdatalen = *pacdatalen>>1;
        *pacnum = *pacnum<<1;
    }
    return *pacdatalen/(bitres*nbchannels) - 1;
}


inline int vban_read_frame_from_ringbuffer(float* dst, ringbuffer_t* ringbuffer, int num)
{
    size_t size = num*sizeof(float);
    if (ringbuffer_read_space(ringbuffer)>=size)
    {
        ringbuffer_read(ringbuffer, (char*)dst, size);
        return 0;
    }
    return 1;
}


inline int vban_add_frame_from_ringbuffer(float* dst, ringbuffer_t* ringbuffer, int num)
{
    float fsamples[256];
    size_t size = num*sizeof(float);
    if (ringbuffer_read_space(ringbuffer)>=size)
    {
        ringbuffer_read(ringbuffer, (char*)fsamples, size);
        for (int i=0; i<num; i++) dst[i] = (dst[i] + fsamples[i])/2;
        return 0;
    }
    return 1;
}


inline int vban_send_txbuffer(vban_stream_context_t* context, in_addr_t txip = 0, uint8_t attempts = 2)
{
    int ret = 0;

    for (uint16_t pac = 0; pac < context->pacnum; pac++)
    {
        memcpy(context->txpacket.data, context->txbuf + pac*context->pacdatalen, context->pacdatalen);

        for (uint8_t red = 0; red <= context->redundancy; red++)
            if (context->txport!= 0)
            {
                udp_send(context->txsock, context->txport, (char*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen, txip);

                int icnt = 0;
                usleep(10);
                while((check_send_status(context->txsock->fd)==111)&&(icnt<2))
                {
                    udp_send(context->txsock, context->txport, (char*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen, txip);
                    usleep(10);
                    icnt++;
                }
                if (icnt==attempts) ret = 1; // ICMP PORT UNREACHABLE
            }
            else write(context->pipedesc, (uint8_t*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen);
        context->txpacket.header.nuFrame++;
    }
    return ret;
}


inline int vban_send_t32_fragment(vban_stream_context_t* context, in_addr_t txip = 0, uint8_t attempts = 2)
{
    int ret = 0;
    int txbuf_pos;

    for (uint16_t pac = 0; pac < context->pacnum_t32; pac++)
    {
        txbuf_pos = (context->txdata_ind * context->pacnum_t32 + pac) * context->pacdatalen;
        memcpy(context->txpacket.data, context->txbuf + txbuf_pos, context->pacdatalen);

        for (uint8_t red = 0; red <= context->redundancy; red++)
            if (context->txport!= 0)
            {
                udp_send(context->txsock, context->txport, (char*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen, txip);

                int icnt = 0;
                usleep(10);
                while((check_send_status(context->txsock->fd)==111)&&(icnt<2))
                {
                    udp_send(context->txsock, context->txport, (char*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen, txip);
                    usleep(10);
                    icnt++;
                }
                if (icnt==attempts) ret = 1; // ICMP PORT UNREACHABLE
            }
            else write(context->pipedesc, (uint8_t*)&context->txpacket, VBAN_HEADER_SIZE+context->pacdatalen);
        context->txpacket.header.nuFrame++;
        // fprintf(stderr, "%d\r\n", txbuf_pos);
    }

    return ret;
}


inline void read_from_ringbuffer_async(vban_stream_context_t* context)
{
    int bframe = 0;
    int pframe = 0;
    int lframe = 0;
    int lost = 0;
    int llost = 0;
    int lcnt = 0;
    int rb_readspace;
    int rxbuf_pos = 0;

    memcpy(context->rxbuf, context->rxbuf + context->nframes * context->nboutputs, context->lagrange_num * context->nboutputs * sizeof(float)); // TAIL TO HEAD
    for (bframe = 0; bframe<context->nframes; bframe++)
    {
        rxbuf_pos = context->nboutputs * (lframe + context->lagrange_num);
        rb_readspace = ringbuffer_read_space(context->ringbuffer);
        if (rb_readspace < sizeof(float)*context->nboutputs)
        {
            llost++;
            lost++;
            if (llost < 3)//(llost == 1)
            {
                lcnt++;
                for (int channel=0; channel<context->nboutputs; channel++)
                    ((float*)(context->rxbuf + rxbuf_pos))[channel] =   0.8 * 3 * ((float*)(context->rxbuf + rxbuf_pos - context->nboutputs))[channel] -
                                                                        0.8 * 3 * ((float*)(context->rxbuf + rxbuf_pos - 2 * context->nboutputs))[channel] +
                                                                        0.8 * ((float*)(context->rxbuf + rxbuf_pos - 3 * context->nboutputs))[channel]; // LAGRANGE
                lframe++;
            }
        }
        else
        {
            pthread_mutex_lock(&context->rxmutex.threadlock);
            vban_read_frame_from_ringbuffer(context->rxbuf + rxbuf_pos, context->ringbuffer, context->nboutputs);
            if (llost < 3)
            {
                for (int channel=0; channel<context->nboutputs; channel++)
                    ((float*)(context->rxbuf + rxbuf_pos))[channel] =   0.3 * ((float*)(context->rxbuf + rxbuf_pos))[channel] +
                                                                        0.9 * ((float*)(context->rxbuf + rxbuf_pos - context->nboutputs))[channel] -
                                                                        0.9 * ((float*)(context->rxbuf + rxbuf_pos - 2 * context->nboutputs))[channel] +
                                                                        0.3 * ((float*)(context->rxbuf + rxbuf_pos - 3 * context->nboutputs))[channel];
            }
            pthread_mutex_unlock(&context->rxmutex.threadlock);
            llost = 0;
            lframe++;
        }
    }

    if (lframe < (context->nframes >> 1))
        memmove(context->rxbuf + context->nboutputs * (context->nframes - 1 - lframe) + context->lagrange_num,
                context->rxbuf + context->nboutputs * lframe + context->lagrange_num,
                context->nboutputs * lframe * sizeof(float));

    // for (bframe=0; bframe<context->nframes; bframe++)
    // {
    //     //while(pthread_mutex_trylock(&context->rxmutex.threadlock));
    //     pthread_mutex_lock(&context->rxmutex.threadlock);
    //     if (vban_read_frame_from_ringbuffer(&context->rxbuf[pframe*context->nboutputs], context->ringbuffer, context->nboutputs))
    //     {
    //         lost++;
    //         //if (pframe!=0) memcpy(&context->rxbuf[pframe*context->nboutputs], &context->rxbuf[(pframe - 1)*context->nboutputs], sizeof(float)*context->nboutputs);
    //     }
    //     pthread_mutex_unlock(&context->rxmutex.threadlock);
    //     pframe++;
    // }

    if (lost==0)
    {
        pthread_mutex_lock(&context->rxmutex.threadlock);
        context->lost_pac_cnt = 0;
        pthread_mutex_unlock(&context->rxmutex.threadlock);
    }
    else
    {
        if (context->lost_pac_cnt<9) fprintf(stderr, "%d samples lost\n", lost);
        if (lost==context->nframes)
        {
            if (context->lost_pac_cnt<10) context->lost_pac_cnt++;
            if (context->lost_pac_cnt==9)
                memset(context->rxbuf, 0, context->rxbuflen*sizeof(float));
        }
    }
}


inline void read_from_ringbuffer_async_non_interleaved(vban_stream_context_t* context, float** buffers)
{
    int bframe = 0;
    int lframe = 0;
    int lost = 0;
    int llost = 0;
    int lcnt = 0;
    int rb_readspace;
    int rxbuf_pos = 0;

    memcpy(context->rxbuf, context->rxbuf + context->nframes * context->nboutputs, context->lagrange_num * context->nboutputs * sizeof(float)); // TAIL TO HEAD
    for (bframe = 0; bframe<context->nframes; bframe++)
    {
        rxbuf_pos = context->nboutputs * (lframe + context->lagrange_num);
        rb_readspace = ringbuffer_read_space(context->ringbuffer);
        if (rb_readspace < sizeof(float)*context->nboutputs)
        {
            llost++;
            lost++;
            if (llost < 3)//(llost == 1)
            {
                lcnt++;
                for (int channel=0; channel<context->nboutputs; channel++)
                    ((float*)(context->rxbuf + rxbuf_pos))[channel] =   0.8 * 3 * ((float*)(context->rxbuf + rxbuf_pos - context->nboutputs))[channel] -
                                                                        0.8 * 3 * ((float*)(context->rxbuf + rxbuf_pos - 2 * context->nboutputs))[channel] +
                                                                        0.8 * ((float*)(context->rxbuf + rxbuf_pos - 3 * context->nboutputs))[channel]; // LAGRANGE
                for (int channel=0; channel<context->nboutputs; channel++)
                    buffers[channel][lframe] = ((float*)(context->rxbuf + context->nboutputs * lframe))[channel];
                lframe++;
            }
        }
        else
        {
            pthread_mutex_lock(&context->rxmutex.threadlock);
            vban_read_frame_from_ringbuffer(context->rxbuf + rxbuf_pos, context->ringbuffer, context->nboutputs);
            if (llost < 3)
            {
                for (int channel=0; channel<context->nboutputs; channel++)
                    ((float*)(context->rxbuf + rxbuf_pos))[channel] =   0.3 * ((float*)(context->rxbuf + rxbuf_pos))[channel] +
                                                                        0.9 * ((float*)(context->rxbuf + rxbuf_pos - context->nboutputs))[channel] -
                                                                        0.9 * ((float*)(context->rxbuf + rxbuf_pos - 2 * context->nboutputs))[channel] +
                                                                        0.3 * ((float*)(context->rxbuf + rxbuf_pos - 3 * context->nboutputs))[channel];
            }
            pthread_mutex_unlock(&context->rxmutex.threadlock);
            llost = 0;
            for (int channel=0; channel<context->nboutputs; channel++)
                buffers[channel][lframe] = ((float*)(context->rxbuf + context->nboutputs * lframe))[channel];
            lframe++;
        }
    }

    if (lframe < (context->nframes >> 1))
    {
        memmove(context->rxbuf + context->nboutputs * (context->nframes - 1 - lframe) + context->lagrange_num,
                context->rxbuf + context->nboutputs * lframe + context->lagrange_num,
                context->nboutputs * lframe * sizeof(float));
        for (bframe = context->nframes - 1 - lframe; bframe < context->nframes; bframe++)
            for (int channel=0; channel<context->nboutputs; channel++)
                buffers[channel][bframe] = ((float*)(context->rxbuf + context->nboutputs * bframe))[channel];
    }

    if (lost)
    {
        if (context->lost_pac_cnt<9) fprintf(stderr, "%d samples lost\n", lost);
        if (lost==context->nframes)
        {
            if (context->lost_pac_cnt<10) context->lost_pac_cnt++;
            if (context->lost_pac_cnt>=9)
            {
                for (int channel = 0; channel < context->nboutputs; channel++)
                    for (int frame=0; frame<context->nframes; frame++)
                        buffers[channel][frame] = 0;
            }
        }
    }
}


// Returns 0 - new value is in filter, 1 - no error, no new value, 2 - XRun handled, 3 - XRun detected
inline int calc_input_samplerate(vban_stream_context_t* context, VBanPacket* vban_packet)
{
    int ret = 0;
    struct timespec timetmp = context->input_ts;
    int64_t timedeltatmp = (timetmp.tv_sec - context->time_old.tv_sec) * 1000000000 + timetmp.tv_nsec - context->time_old.tv_nsec;
    if (timedeltatmp > 100000)
    {
        if (context->time_cycle_start.tv_sec != 0)
        {
            context->deltaT[0] = (timetmp.tv_sec - context->time_cycle_start.tv_sec) * 1000000000 + timetmp.tv_nsec - context->time_cycle_start.tv_nsec;
            if (context->deltaT[3] == 0) for (int f = 1; f < 4; f++) context->deltaT[f] = context->deltaT[f - 1]<<3;
            context->deltaT[1] = context->deltaT[1] - (context->deltaT[1]>>3) + context->deltaT[0];
            context->deltaT[2] = context->deltaT[2] - (context->deltaT[2]>>3) + context->deltaT[1];
            context->deltaT[3] = context->deltaT[3] - (context->deltaT[3]>>3) + context->deltaT[2];

            context->rx_cycle_delta.ts_new = timetmp;
            calc_delta_filtered(&context->rx_cycle_delta, 3, 0.005);
            if (((context->flags&NET_XRUN)==0)&&(context->cframes!= 0)&&(vban_packet->header.nuFrame == context->nu_frame + 1))
            {
                if (context->fsin==0) context->fsin = context->samplerate;
                context->fsin = (double)context->cframes*1e9/(double)context->rx_cycle_delta.deltatf[2];
            }
            else
            {
                fprintf(stderr, "\r\nXRUN! pacs %d\r\n", context->cycle_pac_cnt);
                ret = 2;
            }

            context->cframes_marr[context->cfind] = context->cycle_frames;
            if (context->cfind == CFMSIZE - 1) context->cfind = 0;
            else context->cfind++;
            context->cframes = cfmed(context->cframes_marr);

            context->cycle_pac_cnt = 1;
            context->cycle_frames = vban_packet->header.format_nbs + 1;
            context->flags&=~NET_XRUN;

        }
        else ret = 3;
        context->time_cycle_start = timetmp;
    }
    else
    {
        if (vban_packet->header.nuFrame != context->nu_frame + 1) context->flags|= NET_XRUN;
        context->cycle_pac_cnt+= 1;
        context->cycle_frames+= vban_packet->header.format_nbs + 1;
        ret = 1;
    }
    context->time_old = timetmp;
    return ret;
}


inline static int vban_rx_handle_packet(VBanPacket* vban_packet, int packetlen, vban_stream_context_t* context, uint32_t ip_in, u_int16_t port_in)
{
    int bframe = 0;
    int iframe = 0;
    int frame = 0;
    uint16_t nbc;
    static float fsamples[VBAN_CHANNELS_MAX_NB];
    size_t framesize;
    size_t outframesize;
    char* srcptr;
    uint16_t eventptr;
    uint32_t iplocal = context->iplocal;

    switch (vban_packet->header.format_SR&VBAN_PROTOCOL_MASK)
    {
    case VBAN_PROTOCOL_AUDIO:
        if (context->ringbuffer==nullptr)  // ringbuffer is not created that means client is not created too
        {
            if (context->nboutputs==0) context->nboutputs = vban_packet->header.format_nbc + 1;
            context->samplerate_resampler = VBanSRList[vban_packet->header.format_SR];

            // Let main loop continue
            if (pthread_mutex_trylock(&context->cmdmutex.threadlock)==0)
            {
                pthread_cond_signal(&context->cmdmutex.dataready);
                pthread_mutex_unlock(&context->cmdmutex.threadlock);
            }
            return 1;
        }

        if ((strncmp(vban_packet->header.streamname, context->rx_streamname, VBAN_STREAM_NAME_SIZE)==0)&& // stream name matches
            //(vban_packet->header.format_SR  == vban_get_format_SR(context->samplerate))&& // will be deprecated after resampler
            (vban_packet->header.nuFrame!= context->nu_frame)&& // number of packet is not same
            (context->iprx==ip_in))//&&(ip_in!= iplocal))
        {
            nbc = ((vban_packet->header.format_nbc + 1) < context->nboutputs ? (vban_packet->header.format_nbc + 1) : context->nboutputs);
            framesize = (vban_packet->header.format_nbc + 1)*VBanBitResolutionSize[vban_packet->header.format_bit];
            outframesize = context->nboutputs*sizeof(float);
            srcptr = vban_packet->data;

            if (context->resampler_inbuflen < (vban_packet->header.format_nbs + 1))
            {
                if (context->resampler_inbuf!=nullptr) free(context->resampler_inbuf);
                if (context->resampler_outbuf!=nullptr) free(context->resampler_outbuf);
                context->resampler_inbuflen = vban_packet->header.format_nbs + 1;
                context->resampler_inbuf = (float*)calloc(context->resampler_inbuflen*context->nboutputs, sizeof(float));
                context->resampler_outbuflen = context->resampler_inbuflen * context->samplerate/context->samplerate_resampler + 1;
                context->resampler_outbuf = (float*)calloc(context->resampler_outbuflen*context->nboutputs, sizeof(float));
            }
            else
            {
                context->resampler_inbuflen = vban_packet->header.format_nbs + 1;
                context->resampler_outbuflen = context->resampler_inbuflen * context->samplerate/context->samplerate_resampler + 1;
            }

            bframe = 0;
            for (iframe = 0; iframe<=vban_packet->header.format_nbs; iframe++)
            {
                vban_sample_convert(&context->resampler_inbuf[bframe*context->nboutputs], VBAN_BITFMT_32_FLOAT, srcptr, vban_packet->header.format_bit, nbc);
                srcptr+= framesize;
                bframe++;
            }

            context->resampler->inp_count = context->resampler_inbuflen;
            context->resampler->inp_data = context->resampler_inbuf;
            context->resampler->out_count = context->resampler_outbuflen;
            context->resampler->out_data = context->resampler_outbuf;
            context->resampler->process();

            for (frame = 0; frame < (context->resampler_outbuflen - context->resampler->out_count); frame++)
            {
                // while(pthread_mutex_trylock(&context->rxmutex.threadlock));
                pthread_mutex_lock(&context->rxmutex.threadlock);
                if (ringbuffer_write_space(context->ringbuffer)>=outframesize)
                    ringbuffer_write(context->ringbuffer, (const char*)&context->resampler_outbuf[frame*nbc], outframesize);
                pthread_mutex_unlock(&context->rxmutex.threadlock);
            }

            if (context->flags&CORRECTION_ON)
            {
                int srerr = calc_input_samplerate(context, vban_packet);
                if ((context->cframes > context->nframes)&&(srerr == 0))
                {
                    context->rbfill = ringbuffer_read_space(context->ringbuffer)/framesize + context->cframes - frame;
                    //fprintf (stderr, "%d\r", context->rbfill);
                }
            }

            context->nu_frame = vban_packet->header.nuFrame;

            //return 0;
        }
        break;
    case VBAN_PROTOCOL_SERIAL:
        if ((vban_packet->header.vban==VBAN_HEADER_FOURC)&&
            (vban_packet->header.format_SR==0x2E)&&
            ((strcmp(vban_packet->header.streamname, context->rx_streamname)==0)||
            (strcmp(vban_packet->header.streamname, "MIDI1")==0)))
        {
            if (context->ringbuffer_midi!=0)
            {
                eventptr = 0;
                while((eventptr+VBAN_HEADER_SIZE)<packetlen)
                {
                    if (ringbuffer_write_space(context->ringbuffer_midi)>=3)
                    {
                        ringbuffer_write(context->ringbuffer_midi, &vban_packet->data[eventptr], 3);
                        eventptr+= 3;
                    }
                }
            }
        }
        break;
    case VBAN_PROTOCOL_TXT:
        if ((memcmp(vban_packet->header.streamname, "info", 4)==0)||(memcmp(vban_packet->header.streamname, "INFO", 4)==0))
        {
            if (((packetlen - VBAN_HEADER_SIZE) <= 5)&&(ip_in!= iplocal)) // INFO REQUEST
            {
                fprintf(stderr, "Info request from %d.%d.%d.%d:%d\n", ((uint8_t*)&ip_in)[0], ((uint8_t*)&ip_in)[1], ((uint8_t*)&ip_in)[2], ((uint8_t*)&ip_in)[3], port_in);
                udp_send(context->rxsock, port_in, (char*)&context->info, VBAN_HEADER_SIZE + strlen(context->info.data), ip_in);
                //vban_fill_receptor_info(context);
                // context->flags|= CMD_PRESENT;
                // if (pthread_mutex_trylock(&context->cmdmutex.threadlock)==0)
                // {
                //     pthread_cond_signal(&context->cmdmutex.dataready);
                //     pthread_mutex_unlock(&context->cmdmutex.threadlock);
                // }
            }
            else if ((packetlen - VBAN_HEADER_SIZE) > 5) // PROCESS INCOMING INFO
            {
                if (context->command!= nullptr)
                {
                    memset(context->command, 0, strlen(context->command));
                    strncat(context->command, "info ", 5);
                    memcpy(&context->command[5], vban_packet->data, strlen(vban_packet->data));
                    memset(vban_packet->data, 0, VBAN_DATA_MAX_SIZE);
                    if (sizeof(context->command) - strlen(context->command) > 25)
                    {
                        strncat(context->command, " ipother=", 9);
                        strncat(context->command, inet_ntoa(*(in_addr*)&ip_in), strlen(inet_ntoa(*(in_addr*)&ip_in)));
                        context->flags|= CMD_PRESENT;
                        if (pthread_mutex_trylock(&context->cmdmutex.threadlock)==0)
                        {
                            pthread_cond_signal(&context->cmdmutex.dataready);
                            pthread_mutex_unlock(&context->cmdmutex.threadlock);
                        }
                    }
                }
            }
        }
        else if ((memcmp(vban_packet->header.streamname, "command", 7)==0)||(memcmp(vban_packet->header.streamname, "COMMAND", 7)==0))
        {
            if (context->command!= nullptr)
            {
                memcpy(context->command, vban_packet->data, strlen(vban_packet->data));
                memset(vban_packet->data, 0, VBAN_DATA_MAX_SIZE);
                // pthread_mutex_lock(&context->cmdmutex.threadlock);
                // context->flags|= CMD_PRESENT;
                // pthread_cond_signal(&context->cmdmutex.dataready);
                // pthread_mutex_unlock(&context->cmdmutex.threadlock);
                context->flags|= CMD_PRESENT;
                if (pthread_mutex_trylock(&context->cmdmutex.threadlock)==0)
                {
                    pthread_cond_signal(&context->cmdmutex.dataready);
                    pthread_mutex_unlock(&context->cmdmutex.threadlock);
                }
            }
        }
        else if ((memcmp(vban_packet->header.streamname, "message", 7)==0)||(memcmp(vban_packet->header.streamname, "MESSAGE", 7)==0))
        {
            fprintf(stderr, "Message from %d.%d.%d.%d:%d\r\n%s\r\n", ((uint8_t*)&ip_in)[0], ((uint8_t*)&ip_in)[1], ((uint8_t*)&ip_in)[2], ((uint8_t*)&ip_in)[3], port_in, vban_packet->data);
        }
        break;
    case VBAN_PROTOCOL_USER:
        break;
    default:
        break;
    }
    return 0;
}

#endif
