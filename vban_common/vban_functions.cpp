#include "vban_functions.h"
#include <signal.h>

int get_value_by_key(const char *source, const char *key, char *res, size_t res_size)
{
    if (!source || !key || !res || res_size == 0) return 0;
    size_t key_len = strlen(key);
    const char *ptr = source;
    while ((ptr = strstr(ptr, key)) != NULL)
    {
        int left_ok = (ptr == source) || *(ptr - 1) == ' ' || *(ptr - 1) == ',' || *(ptr - 1) == ';';
        int right_ok = (*(ptr + key_len) == '=');
        if (left_ok && right_ok)
        {
            const char *val_start = ptr + key_len + 1;
            char quote_char = '\0';
            if (*val_start == '"' || *val_start == '\'')
            {
                quote_char = *val_start;
                val_start++;
            }
            size_t val_len = 0;
            if (quote_char != '\0') while (val_start[val_len] != '\0' && val_start[val_len] != quote_char) val_len++;
            else while (val_start[val_len] != '\0' && val_start[val_len] != ' ' && val_start[val_len] != ',' && val_start[val_len] != ';') val_len++;
            if (val_len >= res_size) return 0;
            strncpy(res, val_start, val_len);
            res[val_len] = '\0';
            return 1;
        }
        ptr++;
    }
    return 0;
}

void scan_receptor(vban_stream_context_t* stream)
{
    uint ips[16];
    uint ipnum = 0;
    char rxname[32];
    VBanPacket inforequest;
    VBanPacket info;
    memset(&inforequest, 0, sizeof(VBanPacket));
    inforequest.header.vban = VBAN_HEADER_FOURC;
    inforequest.header.format_SR = VBAN_PROTOCOL_TXT;
    strcat(inforequest.header.streamname, "INFO");
    strcat(inforequest.data, "/info");
    memset(&info, 0, sizeof(VBanPacket));
    memset(ips, 0, 16 * sizeof(int));
    getipaddresses(ips, &ipnum);
    while(stream->iptx == 0)
    {
        sleep(1);
        for (uint i = 0; i < ipnum; i++)
        {
            uint32_t from_ip = ips[i];
            fprintf(stderr, "%d.%d.%d.%d\r\n", ((uint8_t*)&from_ip)[0], ((uint8_t*)&from_ip)[1], ((uint8_t*)&from_ip)[2], ((uint8_t*)&from_ip)[3]);
        }
        for (uint i = 1; i < ipnum; i++)
        {
            udp_send(stream->txsock, stream->txport, (char*)&inforequest, VBAN_HEADER_SIZE + 5, ips[i]|0xFF000000);
            int received = 1500;
            while(poll(stream->pd, 1, 100) > 0 && (stream->pd[0].revents & POLLIN) && received > 0)
            {
                received = udp_recv(stream->txsock, &info, VBAN_PROTOCOL_MAX_SIZE);
                if (received > VBAN_HEADER_SIZE && info.header.vban == VBAN_HEADER_FOURC && (info.header.format_SR&VBAN_PROTOCOL_MASK) == VBAN_PROTOCOL_TXT)
                {
                    uint32_t from_ip = stream->txsock->c_addr.sin_addr.s_addr;
                    uint ind = 0;
                    for (ind = 0; ind < ipnum; ind++) if (from_ip == ips[ind]) break;
                    if (ind == ipnum)
                    {
                        memset(rxname, 0, 32);
                        if (stream->servername[0]!= 0)
                        {
                            ;
                        }
                        else
                        {
                            get_value_by_key(info.data, "streamnamerx", rxname, sizeof(rxname));
                            fprintf(stderr, "Streamnames: there %s, here %s\r\n", rxname, stream->tx_streamname);
                            if (strncmp(stream->tx_streamname, rxname, strlen(rxname)) == 0)
                            {
                                stream->iptx = from_ip;
                                fprintf(stderr, "Receptor found on %d.%d.%d.%d\r\n", ((uint8_t*)&from_ip)[0], ((uint8_t*)&from_ip)[1], ((uint8_t*)&from_ip)[2], ((uint8_t*)&from_ip)[3]);
                                //i = ipnum;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

int parse_passport(const char* input, uint32_t* ip, uint16_t* port)
{
    fprintf(stderr, "%s\r\n", input);
    uint8_t len = 6;
    char xip_buff[16];
    int xport_val;
    struct in_addr xa;
    char garbage;
    if (sscanf(input, "%15[^:]:%d%c", xip_buff, &xport_val, &garbage))
        len = 6;
    if (xport_val < 0 || xport_val > 65535)
        len = 0;
    if (inet_pton(AF_INET, xip_buff, &xa) != 1)
        len = 0;
    if (len == 6) {
        *ip = xa.s_addr;
        *port = htons((uint16_t)xport_val);
        fprintf(stderr, "Extended udp passport: %u.%u.%u.%u:%u\r\n", (*ip & 0xFF), ((*ip >> 8) & 0xFF), ((*ip >> 16) & 0xFF), (*ip >> 24), htons(*port));
    }
    return len;
}

void tune_tx_packets(vban_stream_context_t* stream)
{
    volatile uint16_t max_packet_frames;
    stream->pacnum = 1;
    stream->vban_nframes_pac = stream->nframes;
    stream->txbuflen = stream->nframes * stream->nbinputs * VBanBitResolutionSize[stream->vban_output_format];
    if (stream->txbuf != NULL)
        free(stream->txbuf);
    stream->txbuf = (char*)malloc(stream->txbuflen);
    memset(stream->txbuf, 0, stream->txbuflen);
    stream->pacdatalen = stream->txbuflen;
    if (stream->flags & SLICING) {
        if (stream->samplerate < 88200)
            stream->tx_nframes = 64;
        else if (stream->samplerate < 176400)
            stream->tx_nframes = 128;
        else if (stream->samplerate < 352800)
            stream->tx_nframes = 256;
        else {
            stream->tx_nframes = VBAN_SAMPLES_MAX_NB;
            fprintf(stderr, "Samplerate is too large for SLICING mode");
            stream->flags &= ~SLICING;
        }
        max_packet_frames = stream->tx_nframes;
    } else {
        max_packet_frames = VBAN_SAMPLES_MAX_NB;
    }
    while ((stream->pacdatalen > VBAN_DATA_MAX_SIZE) || (stream->vban_nframes_pac > max_packet_frames)) {
        stream->pacnum = stream->pacnum * 2;
        stream->vban_nframes_pac = stream->vban_nframes_pac / 2;
        stream->pacdatalen = stream->pacdatalen / 2;
    }
    stream->txpacket.header.format_nbs = stream->vban_nframes_pac - 1;
    if (stream->flags & SLICING) {
        stream->tx_transactions = stream->nframes / stream->tx_nframes;
        stream->pacnum_t32 = stream->pacnum * stream->tx_nframes / stream->nframes;
        stream->slice_period = (uint32_t)((uint64_t)stream->tx_nframes * 1000000000 / stream->samplerate + 1);
    }
}

void vban_fill_receptor_info(vban_stream_context_t* context)
{
    memset(&context->info, 0, VBAN_PROTOCOL_MAX_SIZE);
    context->info.header.vban = VBAN_HEADER_FOURC;
    context->info.header.format_SR = VBAN_PROTOCOL_TXT;
    strcpy(context->info.header.streamname, "INFO");
    if (context->flags & MULTISTREAM) {
        sprintf(context->info.data, "servername=%s ", context->rx_streamname);
    } else {
        sprintf(context->info.data, "streamnamerx=%s nbchannels=%d ", context->rx_streamname, context->nboutputs);
    }
    sprintf(context->info.data + strlen(context->info.data), "samplerate=%d format=%d flags=%d", context->samplerate, context->vban_output_format, context->flags);
}

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#define TARGET_CPU 1
#define INTERVAL_NS 333333
#define PERIOD_NS 666666

void reset_timer(int* timerfd, uint64_t delay_sec, uint64_t delay_nsec, uint64_t period_sec, uint64_t period_nsec)
{
    struct itimerspec ts;
    ts.it_interval.tv_sec = period_sec;
    ts.it_interval.tv_nsec = period_nsec;
    ts.it_value.tv_sec = delay_sec;
    ts.it_value.tv_nsec = delay_nsec;

    int err = timerfd_settime(*timerfd, 0, &ts, NULL);

    if ((delay_sec | delay_nsec | period_sec | period_nsec) == 0) {
        if (err == -1)
            fprintf(stderr, "Cannot stop timer...\r\n");
        // else
        //     fprintf(stderr, "Timer on fd %d successfully stopped\r\n", *timerfd);
    } else {
        if (err == -1)
            fprintf(stderr, "Cannot change timer parameters...\r\n");
        // else
        //     fprintf(stderr, "Timer on fd %d has been successfully reset\r\n", *timerfd);
    }
}
#endif

void* timerThread(void* arg)
{
    vban_multistream_context_t* context = (vban_multistream_context_t*)arg;
    // client_id_t* clients = (client_id_t*)arg;
    client_id_t* client = NULL;
    // client_id_t* next = NULL;
    pid_t pidtokill;
    pid_t pidtokilltx;
    uint index;

    while ((*context->flags & RECEIVING) == RECEIVING) // Thread is enabled cond
    {
        usleep(50000);
        client = context->clients;
        if (context->active_clients_num)
            for (index = 0; index < context->active_clients_num; index++) {
                if (client->timer == 4) {
                    if (index == 0) // Remove 1-st
                    {
                        pidtokill = context->clients->pid;
                        pidtokilltx = context->clients->txpid;
                        pop(&context->clients);
                    } else {
                        pidtokill = client->pid;
                        pidtokilltx = client->txpid;
                        if (index == context->active_clients_num)
                            remove_last(context->clients);
                        else
                            remove_by_index(&context->clients, index);
                    }
                    kill(pidtokill, SIGINT);
                    pclose2(pidtokill);
                    if (pidtokilltx) {
                        kill(pidtokilltx, SIGTERM);
                        pclose2(pidtokilltx);
                    }
                    context->active_clients_num--;
                    break;
                } else
                    client->timer++;
                // if (active_clients_num!=0)
                client = client->next;
            }
    }
}

void* rxThread(void* arg)
{
    timespec ts;
    vban_stream_context_t* stream = (vban_stream_context_t*)arg;
    VBanPacket packet;
    int packetlen;
    int datalen;
    uint32_t ip_in = 0;
    uint8_t handle_packet = 0;
    // uint16_t port_in = 0;

    fprintf(stderr, "rxThread started\r\n");

    while ((stream->flags & RECEIVING) == RECEIVING) {
        while ((poll(stream->pd, 1, 100)) && ((stream->flags & RECEIVING) == RECEIVING)) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
            if (stream->rxport != 0) // UDP
            {
#ifndef __linux__
                packetlen = udp_recv(stream->rxsock, &packet, VBAN_PROTOCOL_MAX_SIZE);
#else
                packetlen = udp_recv_m(stream->rxsock, &packet, VBAN_PROTOCOL_MAX_SIZE, &stream->input_ts);
                //packetlen = udp_recv(stream->rxsock, &packet, VBAN_PROTOCOL_MAX_SIZE);
#endif
                ip_in = stream->rxsock->c_addr.sin_addr.s_addr;
                stream->ansport = htons(stream->rxsock->c_addr.sin_port);
                if ((packet.header.format_SR & VBAN_PROTOCOL_MASK) == VBAN_PROTOCOL_AUDIO || (packet.header.format_SR & VBAN_PROTOCOL_MASK) == VBAN_PROTOCOL_SERIAL)
                {
                    if (stream->iprx == ip_in && memcmp(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE) == 0) handle_packet = 1;
                    else if (stream->iprx == 0 && stream->rx_streamname[0] == 0) // IP 0, streamname empty
                    {
                        stream->iprx = ip_in;
                        strncpy(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE);
                    }
                    else if (stream->iprx == ip_in && stream->rx_streamname[0] == 0) // IP matches, streamname empty
                       strncpy(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE);
                    else if (stream->iprx == 0 && memcmp(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE) == 0) // IP 0, streamname matches
                       stream->iprx = ip_in;
                    // if (stream->iprx == 0) // stream->iprx = ip_in;
                    // {
                    //     if (strncmp(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE) == 0)
                    //         stream->iprx = ip_in;
                    //     else if (stream->rx_streamname[0] == 0)
                    //     {
                    //         strncpy(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE);
                    //         stream->iprx = ip_in;
                    //     }
                    // }
                    // else if ((stream->iprx == ip_in) && (stream->rx_streamname[0] == 0))
                    //     strncpy(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE);
                }
                else
                {
                    ip_in = stream->rxsock->c_addr.sin_addr.s_addr;
                    stream->ansport = htons(stream->rxsock->c_addr.sin_port);
                    handle_packet = 1;
                }
            }
            else
            {
                stream->input_ts = ts;
                packetlen = read(stream->pd[0].fd, &packet, VBAN_HEADER_SIZE);
                if (((packet.header.format_SR & VBAN_PROTOCOL_MASK) == VBAN_PROTOCOL_AUDIO) || ((packet.header.format_SR & VBAN_PROTOCOL_MASK) == VBAN_PROTOCOL_TXT))
                {
                    datalen = VBanBitResolutionSize[packet.header.format_bit & VBAN_BIT_RESOLUTION_MASK] * (packet.header.format_nbc + 1) * (packet.header.format_nbs + 1);
                    if (datalen == read(stream->pd[0].fd, packet.data, datalen))
                        packetlen += datalen;
                    if (stream->rx_streamname[0] == 0)
                        strncpy(stream->rx_streamname, packet.header.streamname, VBAN_STREAM_NAME_SIZE);
                }
                if ((packetlen >= VBAN_HEADER_SIZE) && (packet.header.vban == VBAN_HEADER_FOURC)) handle_packet = 1;
            }
            if (handle_packet)
            {
                handle_packet = 0;
                vban_rx_handle_packet(&packet, packetlen, stream, ip_in, stream->ansport);
                memset(&packet, 0, sizeof(VBanPacket));
            }
        }
    }

    fprintf(stderr, "RX thread stopped\r\n");
    return NULL;
}
