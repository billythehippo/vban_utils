#include <arpa/inet.h>
#include "../vban_common/pipewire_backend.h"
#include "../vban_common/udp.h"
#include "../vban_common/vban_client_list.h"

int main(int argc, char *argv[])
{
    vban_stream_context_t stream;

    memset(&stream, 0, sizeof(vban_stream_context_t));

    stream.txport = 6980;
    stream.vban_output_format = VBAN_BITFMT_32_FLOAT;
    stream.samplerate = 48000;
    stream.nbinputs = 2;
    stream.nframes = 128;

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
    stream.txmidipac.header.format_SR = VBAN_PROTOCOL_SERIAL&11;
    strncpy(stream.txmidipac.header.streamname, stream.tx_streamname, (strlen(stream.tx_streamname)>16 ? 16 : strlen(stream.tx_streamname)));

    stream.flags|= TRANSMITTER;
    pw_stream_data_t data = { 0, };
    static struct pw_stream_events stream_events;
    data.user_data = (void*)&stream;

    pw_init_tx_stream(&data, &stream_events, &stream, format_vban_to_spa((VBanBitResolution)stream.vban_output_format));

    mutexcond_t runmutex;
    pw_run_stream(&data, &runmutex);

    while(1) sleep(1);

    pw_stop_stream(&data, &runmutex);

    if (stream.txsock.socket!=0) udp_free(stream.txsock);
    if (stream.txbuf!= nullptr) free(stream.txbuf);

    return 0;
}
