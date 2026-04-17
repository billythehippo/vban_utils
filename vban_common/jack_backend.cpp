#include "jack_backend.h"
#include "vban_functions.h"
#include <cstddef>
#include <cstdint>
#include <jack/types.h>
#include <sys/timerfd.h>


void help_emitter(void)
{
    fprintf(stderr, "VBAN Jack Audio Connection Kit emitter for network and pipes/fifos\n\nBy Billy the Hippo\n\nusage: vban_emitter_jack <args>\r\n\n");
    //fprintf(stderr, "-m - multistream mode on\r\n");
    fprintf(stderr, "-i - ip address or pipe name (default ip=0, default pipename - stdin\r\n");
    fprintf(stderr, "-p - ip port (if 0 - pipe mode)\r\n");
    fprintf(stderr, "-s - Stream/Receptor name, up to 16 symbols\r\n");
    //fprintf(stderr, "-r - samplerate (default 48000)\r\n");
    //fprintf(stderr, "-q - quantum, buffer size (reserved)\r\n"); //Attention!!! Default is 128!!! Made for musicians.
    fprintf(stderr, "-c - number of channels/clients\r\n");
    fprintf(stderr, "-n - redundancy 1 to 10 (\"net quality\")\r\n");
    fprintf(stderr, "-d - device mode for jack ports\r\n");
    fprintf(stderr, "-f - format: 16, 24, 32f\r\n");
    fprintf(stderr, "-e - enable 32 frames slicing\r\n");
    fprintf(stderr, "-h - show this help\r\n");
    exit(0);
}


void help_receptor(void)
{
    fprintf(stderr, "VBAN Jack Audio Connection Kit receptor for network and pipes/fifos\n\nBy Billy the Hippo\n\nusage: vban_receptor_jack <args>\r\n\n");
    fprintf(stderr, "-m - multistream mode on\r\n");
    fprintf(stderr, "-i - ip address or pipe name (default ip=0, default pipename - stdin\r\n");
    fprintf(stderr, "-p - ip port (if 0 - pipe mode)\r\n");
    fprintf(stderr, "-s - Stream/Receptor name, up to 16 symbols\r\n");
    //fprintf(stderr, "-r - samplerate (default 48000)\r\n");
    //fprintf(stderr, "-q - quantum, buffer size (reserved)\r\n"); //Attention!!! Default is 128!!! Made for musicians.
    fprintf(stderr, "-c - number of channels/clients\r\n");
    fprintf(stderr, "-n - redundancy 1 to 10 (\"net quality\")\r\n");
    fprintf(stderr, "-d - device mode for jack ports\r\n");
    //fprintf(stderr, "-f - format: 16, 24, 32f\r\n");
    fprintf(stderr, "-e - enable resampler-corrector\r\n");
    fprintf(stderr, "-h - show this help\r\n");
    exit(0);
}


int get_emitter_options(vban_stream_context_t* stream, int argc, char *argv[])
{
    int index = 0;
    char c;
    static const struct option options[] =
    {
        //{"multistream", required_argument,  0, 'm'},
        {"ipaddr",      required_argument,  0, 'i'},
        {"port",        required_argument,  0, 'p'},
        {"streamname",  required_argument,  0, 's'},
        {"samplerate",  required_argument,  0, 'r'},
        {"bufsize",     required_argument,  0, 'q'},
        {"nbchannels",  required_argument,  0, 'c'},
        {"redundancy",  required_argument,  0, 'n'},
        {"device",      required_argument,  0, 'd'},
        {"autoconnect", required_argument,  0, 'a'},
        {"format",      required_argument,  0, 'f'},
        {"slicing",     required_argument,  0, 'e'},
        {"jackservname",required_argument,  0, 'j'},
        {"help",        no_argument,        0, 'h'},
        {0,             0,                  0,  0 }
    };

    c = getopt_long(argc, argv, "m:i:p:s:r:q:c:n:d:a:f:e:j:h", options, &index);
    if (c==-1) c = 'h';

    while(c!=-1)
    {
        switch (c)
        {
            case 'm':
                break;
            case 'i': // ip addr to filter / input pipe name
                if (stream->txport==0) // PIPE mode
                {
                    memset(stream->pipename, 0, 32);
                    strncpy(stream->pipename, optarg, (strlen(optarg)>32 ? 32 : strlen(optarg)));
                    stream->iptx = 0;
                }
                else // UDP mode
                {
                    if (strlen(optarg)>15)
                    {
                        fprintf(stderr, "Wrong IP address!!!\r\n");
                        return 1;
                    }
                    memcpy(stream->iptxaddr, optarg, strlen(optarg));
                    stream->iptx = inet_addr(optarg);
                }
                break;
            case 'p': // TX port to listen
                stream->txport = atoi(optarg);
                break;
            case 's': // Streamname (in multistream mode - receptor name)
                memset(stream->tx_streamname, 0, VBAN_STREAM_NAME_SIZE);
                memcpy(stream->tx_streamname, optarg, (strlen(optarg)>VBAN_STREAM_NAME_SIZE ? VBAN_STREAM_NAME_SIZE : strlen(optarg)));
                break;
            case 'r': // Samplerate
                stream->samplerate = atoi(optarg);
                break;
            case 'q': // Quantum (buffer size)
                stream->nframes = atoi(optarg);
                break;
            case 'c': // Channel number / Clients number
                stream->nbinputs = atoi(optarg);
                break;
            case 'n': // Redundancy (Network Quality)
                stream->redundancy = atoi(optarg);
                break;
            case 'd': // Device mode in graph
                if ((optarg[0]!='0')&&(optarg[0]!='n')&&(optarg[0]!='N'))
                {
                    stream->flags&=~AUTOCONNECT;
                    stream->flags|= DEVICE_MODE;
                }
                else stream->flags&=~DEVICE_MODE;
                break;
            case 'a':
                if (((stream->flags&DEVICE_MODE)!= DEVICE_MODE)&&(optarg[0]=='y'))
                    stream->flags|= AUTOCONNECT;
            case 'f':
                if (strstr(optarg, "16")!= NULL) stream->vban_output_format = VBAN_BITFMT_16_INT;
                else if (strstr(optarg, "24")!= NULL) stream->vban_output_format = VBAN_BITFMT_24_INT;
                //else if (strstr(optarg, "32")!= NULL) stream->vban_output_format = VBAN_BITFMT_32_FLOAT;
                else stream->vban_output_format = VBAN_BITFMT_32_FLOAT;
                break;
            case 'e':
                if ((optarg[0]!='0')&&(optarg[0]!='n')&&(optarg[0]!='N')) stream->flags|= SLICING;
                else stream->flags&=~SLICING;
                break;
            case 'j':
                stream->jack_server_name = (char*)malloc(strlen(optarg));
                strncpy(stream->jack_server_name, optarg, strlen(optarg));
                break;
            case 'h':
                help_emitter();
                return 1;
            default:
                fprintf(stderr, "Unrecognized parameter -%c", c);
                break;
        }
        c = getopt_long(argc, argv, "m:i:p:s:r:q:c:n:d:a:f:e:j:h", options, &index);
    }
    return 0;
}


int get_receptor_options(vban_stream_context_t* stream, int argc, char *argv[])
{
    int index = 0;
    char c;
    static const struct option options[] =
        {
            {"multistream", required_argument,  0, 'm'},
            {"ipaddr",      required_argument,  0, 'i'},
            {"port",        required_argument,  0, 'p'},
            {"streamname",  required_argument,  0, 's'},
            {"samplerate",  required_argument,  0, 'r'},
            {"bufsize",     required_argument,  0, 'q'},
            {"nbchannels",  required_argument,  0, 'c'},
            {"redundancy",  required_argument,  0, 'n'},
            {"device",      required_argument,  0, 'd'},
            //{"format",      required_argument,  0, 'f'},
            {"correction",  required_argument,  0, 'e'},
            {"jackservname",required_argument,  0, 'j'},
            {"help",        no_argument,        0, 'h'},
            {0,             0,                  0,  0 }
        };

    c = getopt_long(argc, argv, "m:i:p:s:r:q:c:n:d:f:e:j:h", options, &index);
    if (c==-1) c = 'h';

    while(c!=-1)
    {
        switch (c)
        {
        case 'm': // multistream mode
            if ((optarg[0]!='0')&&(optarg[0]!='n')&&(optarg[0]!='N')) stream->flags|= MULTISTREAM;
            else stream->flags&=~MULTISTREAM;
            break;
        case 'i': // ip addr to filter / input pipe name
            if (stream->rxport==0) // PIPE mode
            {
                memset(stream->pipename, 0, 32);
                strncpy(stream->pipename, optarg, (strlen(optarg)>32 ? 32 : strlen(optarg)));
                stream->iprx = 0;
            }
            else // UDP mode
            {
                if (strlen(optarg)>15)
                {
                    fprintf(stderr, "Wrong IP address!!!\r\n");
                    return 1;
                }
                stream->iprx = inet_addr(optarg);
            }
            break;
        case 'p': // RX port to listen
            stream->rxport = atoi(optarg);
            break;
        case 's': // Streamname (in multistream mode - receptor name)
            memset(stream->rx_streamname, 0, VBAN_STREAM_NAME_SIZE);
            memcpy(stream->rx_streamname, optarg, (strlen(optarg)>VBAN_STREAM_NAME_SIZE ? VBAN_STREAM_NAME_SIZE : strlen(optarg)));
            break;
        case 'r': // Samplerate
            stream->samplerate = atoi(optarg);
            break;
        case 'q': // Quantum (buffer size)
            stream->nframes = atoi(optarg);
            break;
        case 'c': // Channel number / Clients number
            stream->nboutputs = atoi(optarg);
            break;
        case 'n': // Redundancy (Network Quality)
            stream->redundancy = atoi(optarg);
            break;
        case 'd': // Device mode in graph
            if ((optarg[0]!='0')&&(optarg[0]!='n')&&(optarg[0]!='N')) stream->flags|= DEVICE_MODE;
            else stream->flags&=~DEVICE_MODE;
            break;
        case 'f':
            break;
        case 'e':
            if ((optarg[0]!='0')&&(optarg[0]!='n')&&(optarg[0]!='N')) stream->flags|= CORRECTION_ON;
            else stream->flags&=~CORRECTION_ON;
            break;
        case 'j':
            stream->jack_server_name = (char*)malloc(strlen(optarg));
            strncpy(stream->jack_server_name, optarg, strlen(optarg));
            break;
        case 'h':
            help_receptor();
            return 1;
        default:
            fprintf(stderr, "Unrecognized parameter -%c", c);
            break;
        }
        c = getopt_long(argc, argv, "m:i:p:s:r:q:c:n:d:f:e:j:h", options, &index);
    }
    return 0;
}


int jack_init_tx_stream(jack_stream_data_t* client)
{
    vban_stream_context_t* stream = (vban_stream_context_t*)client->user_data;
    volatile int index;
    char name[32];
    unsigned long port_flags = JackPortIsInput;

    if ((stream->flags&DEVICE_MODE)!=0) port_flags|= JackPortIsPhysical;
    if (stream->jack_server_name!=nullptr) client->options = JackServerName;

    client->client = jack_client_open(stream->tx_streamname, client->options, &client->status, stream->jack_server_name);
    if (client == NULL)
    {
        fprintf(stderr, "Failed to create JACK client!\n");
        return 1;
    }

    stream->samplerate = jack_get_sample_rate(client->client);
    stream->nframes = jack_get_buffer_size(client->client);
    tune_tx_packets(stream);

//     if (stream->flags&SLICING)
//     {
// #ifdef __linux__
//         // Создание timerfd (Монотонные часы)
//         stream->txtimer->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
//         if (stream->txtimer->tfd == -1)
//         {
//             fprintf(stderr, "Error! Cannot create timerfd descriptor!\r\n");
//             exit(1);
//         }
// #endif
//     }

    client->input_ports = (jack_port_t**)malloc(sizeof(jack_port_t*)*(stream->nbinputs + 1));
    for (index=0; index<stream->nbinputs; index++)
    {
        memset(name, 0, 32);
        sprintf(name, "Input_%u", (index+1));
        client->input_ports[index] = jack_port_register(client->client, name, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
    }
    memset(name, 0, 32);
    sprintf(name, "Input_MIDI");
    client->input_ports[index] = jack_port_register(client->client, name, JACK_DEFAULT_MIDI_TYPE, port_flags, 0);

    if (jack_set_process_callback(client->client, on_tx_process, (void*)client))
    {
        fprintf(stderr, "Failed to set JACK audio process callback!\n");
        return 1;
    }
    // if (jack_set_midi_event_callback(client, on_tx_midi_process, (void*)client))
    // {
    //     fprintf(stderr, "Failed to set JACK midi process callback!\n");
    //     return 1;
    // }
    return 0;
}


int jack_init_rx_stream(jack_stream_data_t* client)
{
    vban_stream_context_t* stream = (vban_stream_context_t*)client->user_data;
    int index;
    char name[32];
    unsigned long port_flags = JackPortIsOutput;

    if ((stream->flags&DEVICE_MODE)!=0) port_flags|= JackPortIsPhysical;
    if ((stream->jack_server_name!= NULL)&&(stream->jack_server_name[0]!= 0)) client->options = JackServerName;

    client->client = jack_client_open(stream->rx_streamname, client->options, &client->status, stream->jack_server_name);
    if (client == NULL)
    {
        fprintf(stderr, "Failed to create JACK client!\n");
        return 1;
    }

    stream->samplerate = jack_get_sample_rate(client->client);
    stream->nframes = jack_get_buffer_size(client->client);

    if (jack_set_process_callback(client->client, on_rx_process, (void*)client))
    {
        fprintf(stderr, "Failed to set JACK audio process callback!\n");
        return 1;
    }
    // if (jack_set_midi_event_callback(client, on_rx_midi_process, (void*)client))
    // {
    //     fprintf(stderr, "Failed to set JACK midi process callback!\n");
    //     return 1;
    // }
    client->output_ports = (jack_port_t**)malloc(sizeof(jack_port_t*)*(stream->nboutputs + 1));
    for (index=0; index<stream->nboutputs; index++)
    {
        memset(name, 0, 32);
        sprintf(name, "Output_%u", (index+1));
        client->output_ports[index] = jack_port_register(client->client, name, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
    }
    memset(name, 0, 32);
    sprintf(name, "Output_MIDI");
    client->output_ports[index] = jack_port_register(client->client, name, JACK_DEFAULT_MIDI_TYPE, port_flags, 0);

    return 0;
}


int jack_run_tx_stream(jack_stream_data_t* client)
{
    vban_stream_context_t* stream = (vban_stream_context_t*)client->user_data;
    int ret = jack_activate(client->client);
    if (ret) fprintf(stderr, "Cannot activate client\n");
    if((stream->flags&DEVICE_MODE)!= DEVICE_MODE) jack_connect_stream(client, stream->nbinputs);
    return ret;
}


void jack_stop_tx_stream(jack_stream_data_t* client)
{
    jack_deactivate(client->client);
    jack_client_close(client->client);
    if (client->input_ports!=nullptr) free(client->input_ports);
}


int jack_run_rx_stream(jack_stream_data_t* client)
{
    vban_stream_context_t* stream = (vban_stream_context_t*)client->user_data;
    int ret = jack_activate(client->client);
    if (ret) fprintf(stderr, "Cannot activate client\n");
    if((stream->flags&DEVICE_MODE)!= DEVICE_MODE) jack_connect_stream(client, 0, stream->nboutputs);
    return ret;
}


void jack_stop_rx_stream(jack_stream_data_t* client)
{
    jack_deactivate(client->client);
    jack_client_close(client->client);
    if (client->output_ports!=nullptr) free(client->output_ports);
}


int jack_connect_stream(jack_stream_data_t* client, int inputs, int outputs, uint8_t* input_matrix, uint8_t* output_matrix)
{
    vban_stream_context_t* stream = (vban_stream_context_t*)client->user_data;

    if (stream->flags&AUTOCONNECT)
    {
        char const** ports;
        volatile size_t port_id;
        volatile size_t pports = 0;
        volatile size_t port_ind;

        if (inputs > 0)
        {
            ports = jack_get_ports(client->client, 0, 0, JackPortIsPhysical|JackPortIsOutput);

            if (ports != 0)
            {
                for (port_id=0; port_id<1024; port_id++)
                {
                    if (ports[port_id]==0) break;
                    if (strstr(ports[port_id], "midi")==NULL) pports++;
                }
                for (port_id=0; port_id<=inputs; port_id++)
                {
                    if (input_matrix!= nullptr) port_ind = input_matrix[port_id];
                    else port_ind = port_id;

                    if (port_ind <= pports)
                    {
                        if ((ports[port_ind]!= NULL)&&(strstr(ports[port_ind], "midi")==NULL))
                        {
                            if (jack_connect(client->client, ports[port_ind], jack_port_name(client->input_ports[port_id])))
                            {
                                fprintf(stderr, "Could not autoconnect\r\n");
                            }
                        }
                    }
                }
                jack_free(ports);
            }
            else
            {
                fprintf(stderr, "Could not autoconnect inputs...\r\n");
            }
        }

        if (outputs > 0)
        {
            ports = jack_get_ports(client->client, 0, 0, JackPortIsPhysical|JackPortIsInput);

            if (ports != 0)
            {
                for (port_id=0; port_id<1024; port_id++)
                {
                    if (ports[port_id]==0) break;
                    if (strstr(ports[port_id], "midi")==NULL) pports++;
                }
                for (port_id=0; port_id<=outputs; port_id++)
                {
                    if (output_matrix!= nullptr) port_ind = output_matrix[port_id];
                    else port_ind = port_id;

                    if (port_ind <= pports)
                    {
                        if ((ports[port_ind]!= NULL)&&(strstr(ports[port_ind], "midi")==NULL))
                        {
                            if (jack_connect(client->client, jack_port_name(client->output_ports[port_id]), ports[port_ind]))
                            {
                                fprintf(stderr, "Could not autoconnect\r\n");
                            }
                        }
                    }
                }
                jack_free(ports);
            }
            else
            {
                fprintf(stderr, "Could not autoconnect inputs...\r\n");
            }
        }
    }
    return 0;
}


int on_tx_process(jack_nframes_t nframes, void *arg)
{
    jack_stream_data_t *data = (jack_stream_data_t*)arg;
    vban_stream_context_t* context = (vban_stream_context_t*)data->user_data;

    clock_gettime(CLOCK_MONOTONIC, &data->callback_delta.ts_new);

    if (context->flags&SLICING)
    {
#ifdef __linux__
        // struct itimerspec sts;
        // sts.it_value = data->callback_delta.ts_new;
        // sts.it_value.tv_nsec+= (context->slice_period>>1);
        // if (sts.it_value.tv_nsec > 1000000000L)
        // {
        //     sts.it_value.tv_nsec-= 1000000000L;
        //     sts.it_value.tv_sec++;
        // }
        // sts.it_interval.tv_sec = 0;
        // sts.it_interval.tv_nsec = context->slice_period;
        // timerfd_settime(context->txtimer->tfd, TFD_TIMER_ABSTIME, &sts, NULL);
        reset_timer(&context->txtimer->tfd, 0, (context->slice_period>>1), 0, context->slice_period);
        context->txdata_ind = 0;
#endif
    }

    calc_delta_filtered(&data->callback_delta, 3, 0.005);
    //fprintf(stderr, "%d %f %f\r", data->callback_delta.deltat, data->callback_delta.deltatf[2], data->callback_delta.deltatf[2]/context->nframes/1000000000*context->fsin[2]);

    static jack_default_audio_sample_t* buffers[VBAN_CHANNELS_MAX_NB];
    static jack_default_audio_sample_t* midibuf;
    jack_midi_event_t in_event;
    uint16_t midipaclen = 0;
    int index = 0;

    if (context->nframes!=nframes)
    {
        context->nframes = nframes;
        //fprintf(stderr, "ip %s, port %d, streamname %s, red %d, format %d\r\n", ipAddr, udpPort, vban_packet.header.streamname, redundancy, format);
        tune_tx_packets(context);
        fprintf(stderr, "Warning: buffer size is changed to %d!\n", nframes);
    }

    for (uint16_t channel = 0; channel < context->nbinputs; channel++)
        buffers[channel] = (jack_default_audio_sample_t*)jack_port_get_buffer(data->input_ports[channel], nframes);
    midibuf = (jack_default_audio_sample_t*)jack_port_get_buffer(data->input_ports[context->nbinputs], nframes);

    for (uint16_t frame=0; frame<nframes; frame++)
        for (uint16_t channel=0; channel < context->nbinputs; channel++)
        {
            vban_sample_convert((void*)(context->txbuf + index), context->vban_output_format, &buffers[channel][frame], VBAN_BITFMT_32_FLOAT, 1);
            index+= VBanBitResolutionSize[context->vban_output_format];
        }

    //timespec ts = data->cbt;//{ .tv_sec = (__time_t)(data->cbt/1000000), .tv_nsec = (__syscall_slong_t)(data->cbt % 1000000)*1000 };

    if ((context->flags&SLICING)==0)
    {
        vban_send_txbuffer(context, 0, 2);
    }

    jack_nframes_t event_count = jack_midi_get_event_count(midibuf);
    if (event_count>0)
    {
        memset(context->txmidipac.data, 0, midipaclen - VBAN_HEADER_SIZE);
        for(uint mi=0; mi < event_count; mi++)
        {
            jack_midi_event_get(&in_event, midibuf, mi);
            for(int mj=0; mj<nframes; mj++)
            // {
            //     if ((event_index<event_count))//&&(in_event.time==mi))
            //     {
            //         fprintf(stderr, "%d %d %d\n", in_event.buffer[0], in_event.buffer[1], in_event.buffer[2]);
            //     }
            //     event_index++;
            // }
            // fprintf(stderr, "%d %d %d\n", in_event.buffer[0], in_event.buffer[1], in_event.buffer[2]);
            context->txmidipac.data[midipaclen] = in_event.buffer[0];
            context->txmidipac.data[midipaclen+1] = in_event.buffer[1];
            context->txmidipac.data[midipaclen+2] = in_event.buffer[2];
            midipaclen+=3;
        }

        if (context->txport)
        {
            udp_send(context->txsock, context->txport, (char*)&context->txmidipac, VBAN_HEADER_SIZE+midipaclen);
            //udp_send(context->txsock, context->txport, (char*)&data->callback_delta.ts_new, sizeof(timespec));
        }
        else
        {
            write(context->pipedesc, (uint8_t*)&context->txmidipac, midipaclen);
        }
        context->txmidipac.header.nuFrame++;
    }

    return 0;
}


int on_rx_process(jack_nframes_t nframes, void *arg)
{
    jack_stream_data_t *data = (jack_stream_data_t*)arg;
    vban_stream_context_t* context = (vban_stream_context_t*)data->user_data;

    static jack_default_audio_sample_t* buffers[VBAN_CHANNELS_MAX_NB];
    static jack_default_audio_sample_t* midibuf;
    unsigned char* evbuffer;
    size_t bufreadspace;
    uint16_t frame;
    uint16_t channel = 0;
    uint16_t lost = 0;
    int framesize = sizeof(float)*context->nboutputs;
    uint8_t corr = 0;

    if (context->flags&CORRECTION_ON) correct_samplerate(context, framesize, &data->callback_delta);

    if (nframes!=context->nframes)
    {
        context->nframes = nframes;
        vban_compute_rx_buffer(nframes, context->nboutputs, &context->rxbuf, &context->rxbuflen, context->lagrange_num);
        if (context->flags&CORRECTION_ON) corr = context->redundancy;
        vban_compute_rx_ringbuffer(nframes, context->vban_nframes_pac, context->nboutputs, context->redundancy, &context->ringbuffer, corr);
        fprintf(stderr, "Warning: buffer size is changed to %d!\n", nframes);
    }

    for (channel = 0; channel < context->nboutputs; channel++)
        buffers[channel] = (jack_default_audio_sample_t*)jack_port_get_buffer(data->output_ports[channel], nframes);
    midibuf = (jack_default_audio_sample_t*)jack_port_get_buffer(data->output_ports[context->nboutputs], nframes);

    read_from_ringbuffer_async_non_interleaved(context, buffers);

    jack_midi_clear_buffer(midibuf);
    while(ringbuffer_read_space(context->ringbuffer_midi)>=3)
    {
        evbuffer = jack_midi_event_reserve(midibuf, 0, 3);
        ringbuffer_read(context->ringbuffer_midi, (char*)evbuffer, 3);
    }

    return 0;
}
