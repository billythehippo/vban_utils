#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <iostream>
#include <math.h>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <regex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include "MedianFilterInt.h"
#include "zita-alsa-pcmi.h"
#include "../vban_common/vban.h"
#include "../vban_common/ringbuffer.h"
#include "../vban_common/zita-resampler/vresampler.h"
#include "../vban_common/zita-resampler/resampler-table.h"


void convertFloatToVban(const float* src, char* dest, unsigned int n, uint8_t format)
{
    if (!src || !dest || n == 0) return;

    if (format == VBAN_BITFMT_16_INT)
    {
        int16_t* dest16 = reinterpret_cast<int16_t*>(dest);
        for (unsigned int i = 0; i < n; i++)
        {
            float scaled = src[i] * 32767.0f;
            float clamped = std::max(-32768.0f, std::min(32767.0f, scaled));
            dest16[i] = static_cast<int16_t>(std::round(clamped));
        }
    }
    else if (format == VBAN_BITFMT_24_INT)
    {
        for (unsigned int i = 0; i < n; ++i)
        {
            float scaled = src[i] * 8388607.0f;
            float clamped = std::max(-8388608.0f, std::min(8388607.0f, scaled));
            int32_t val32 = static_cast<int32_t>(std::round(clamped));
            unsigned int byte_idx = i * 3;
            dest[byte_idx]     = static_cast<char>(val32 & 0xFF);
            dest[byte_idx + 1] = static_cast<char>((val32 >> 8) & 0xFF);
            dest[byte_idx + 2] = static_cast<char>((val32 >> 16) & 0xFF);
        }
    }
}


void convertVbanToFloat(const char* src, float* dest, unsigned int n, uint8_t format)
{
    if (!src || !dest || n == 0) return;

    if (format == VBAN_BITFMT_16_INT)
    {
        const int16_t* src16 = reinterpret_cast<const int16_t*>(src);
        for (unsigned int i = 0; i < n; ++i) dest[i] = static_cast<float>(src16[i]) / 32768.0f;
    }
    else if (format == VBAN_BITFMT_24_INT)
    {
        for (unsigned int i = 0; i < n; ++i)
        {
            unsigned int byte_idx = i * 3;
            int32_t val32 = (static_cast<uint8_t>(src[byte_idx])) |
                            (static_cast<uint8_t>(src[byte_idx + 1]) << 8) |
                            (static_cast<int8_t>(src[byte_idx + 2]) << 16);
            dest[i] = static_cast<float>(val32) / 8388608.0f;
        }
    }
    else return;
}


std::string getVal(const std::string& input, const std::string& key)
{
    std::string search_key = key + "=";
    size_t pos = input.find(search_key);
    if (pos == std::string::npos) return "";
    size_t val_start = pos + search_key.length();
    if (val_start >= input.length()) return "";
    if (input[val_start] == '"')
    {
        val_start++;
        size_t val_end = input.find('"', val_start);
        if (val_end == std::string::npos) return "";
        return input.substr(val_start, val_end - val_start);
    }
    size_t val_end = input.find_first_of(" \t\r\n;", val_start);
    if (val_end == std::string::npos) return input.substr(val_start);
    return input.substr(val_start, val_end - val_start);
}


std::vector<uint32_t> getIPAddresses(const std::string ifname = "")
{
    std::vector<uint32_t> addresses;
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return addresses;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            uint32_t ip_be = sa->sin_addr.s_addr;
            std::string current_name = ifa->ifa_name;
            //std::cout << "Interface: " << current_name << " | IP: " << inet_ntoa(sa->sin_addr) << std::endl;
            if (!ifname.empty() && current_name == ifname)
            {
                addresses.clear();
                addresses.push_back(ip_be);
                break;
            }
            if (ifname.empty()) addresses.push_back(ip_be);
        }
    }
    freeifaddrs(ifaddr);
    return addresses;
}


class UDPSocket
{
public:
    UDPSocket() : _sock_fd(-1)
    {
        memset(&_tx_addr, 0, sizeof(_tx_addr));
        memset(&_rx_addr_info, 0, sizeof(_rx_addr_info));
    }

    ~UDPSocket()
    {
        if (_sock_fd >= 0) close(_sock_fd);
    }

    bool init(uint32_t rx_ip, uint16_t rx_port, uint32_t tx_ip, uint16_t tx_port, int priority = 6, bool reuse_addr = true, bool broadcast_enabled = true)
    {
        _sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (_sock_fd < 0) return false;

        _rx_addr_info.sin_family = AF_INET;
        _rx_addr_info.sin_port = rx_port;
        _rx_addr_info.sin_addr.s_addr = rx_ip;

        if (broadcast_enabled)
        {
            int bcen = 1;
            if (setsockopt(_sock_fd, SOL_SOCKET, SO_BROADCAST, &bcen, sizeof(bcen)) < 0)
            {
                std::cerr << "WARNING! SO_BROADCAST error" << std::endl;
                return false;
            }
        }

        if (reuse_addr)
        {
            int reuse = 1;
            setsockopt(_sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        }

        if (priority < 0) priority = 0;
        if (priority > 7) priority = 7;
        int tos_value = priority << 5;
        setsockopt(_sock_fd, IPPROTO_IP, IP_TOS, &tos_value, sizeof(tos_value));

        int flags = fcntl(_sock_fd, F_GETFL, 0);
        if (flags >= 0)  fcntl(_sock_fd, F_SETFL, flags | O_NONBLOCK);
        if (bind(_sock_fd, (struct sockaddr*)&_rx_addr_info, sizeof(_rx_addr_info)) < 0)
        {
            close(_sock_fd);
            _sock_fd = -1;
            return false;
        }

        setTxTarget(tx_ip, tx_port);
        return true;
    }

    void setTxTarget(uint32_t tx_ip, uint16_t tx_port)
    {
        std::lock_guard<std::mutex> lock(_tx_mutex);
        _tx_addr.sin_family = AF_INET;
        _tx_addr.sin_port = tx_port;
        _tx_addr.sin_addr.s_addr = tx_ip;
    }

    inline ssize_t sendPacket(const VBanPacket& packet, size_t dataSize)
    {
        if (_sock_fd < 0) return -1;
        return sendto(_sock_fd, &packet, VBAN_HEADER_SIZE + dataSize, 0, (struct sockaddr*)&_tx_addr, sizeof(_tx_addr));
    }

    inline ssize_t receivePacket(VBanPacket& packet, uint32_t& from_ip, uint16_t& from_port)
    {
        if (_sock_fd < 0) return -1;
        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);
        ssize_t res = recvfrom(_sock_fd, &packet, sizeof(VBanPacket), 0, (struct sockaddr*)&from_addr, &addr_len);
        if (res >= (ssize_t)sizeof(VBanHeader))
        {
            from_ip = from_addr.sin_addr.s_addr;
            from_port = from_addr.sin_port;
        }
        return res;
    }

    inline int getFd()
    {
        return _sock_fd;
    }

private:
    int _sock_fd;
    struct sockaddr_in _rx_addr_info;
    std::mutex _tx_mutex;
    struct sockaddr_in _tx_addr;
};


inline void calculateRRatio(double* rratio, float* integral, float* error, int bufferFramesNeeded, int bufferFramesCurrent, double Kp = 0.00002, double Ki = 0.0000005)
{
    *error = 0.9 * *error + 0.1 * ((double)bufferFramesCurrent - (double)bufferFramesNeeded);
    *integral += *error;
    double max_integral = 0.003;// / (Ki > 0 ? Ki : 1.0);
    if (*integral >  max_integral) *integral =  max_integral;
    if (*integral < -max_integral) *integral = -max_integral;
    *rratio = *rratio * 0.9 + 0.1 * (1.0 - (Kp * *error + Ki * *integral));
    if (*rratio > 1.003) *rratio = 1.003;
    if (*rratio < 0.997) *rratio = 0.997;
    //std::cerr << bufferFramesNeeded << " " << bufferFramesCurrent << " " << error << " " << *integral << " " << (int)(*rratio*1000000.0) << "\r";// << std::endl;
}


bool ipIsLocal(uint32_t ip, uint32_t* ips, uint32_t ipsnum)
{
    for (uint32_t i = 0; i < ipsnum; i++) if (ip == ips[i]) return true;
    return false;
}


int main(int argc, char** argv)
{
    std::string args("");
    std::string netDev("");
    std::string captDev(""); //hw:2
    std::string playDev(""); //hw:2
    std::string ctrlDev(""); //hw:2
    uint16_t samplerate = 48000;
    uint16_t nframes = 128;
    uint16_t cycleFrames = 0;
    uint8_t nperiods = 3;
    uint8_t vbanFormat = VBAN_BITFMT_32_FLOAT;
    bool debugEnabled = false;
    uint16_t nbinputs = 2;
    uint16_t nboutputs = 2;
    uint16_t offset = 0;
    std::vector<float> captBuf;
    std::vector<char> txBuf;
    char* txBuffer = nullptr;
    uint16_t txBufLen = 0;
    uint16_t txPacketDataLen = 0;
    uint16_t nPackets = 1;
    uint16_t pacNFrames = nframes;
    std::atomic<uint32_t> txip{0};
    std::atomic<uint32_t> rxip{0};
    std::atomic<uint16_t> txport{htons(6980)};
    std::atomic<uint16_t> rxport{htons(6980)};
    std::string streamnametx = "";
    std::string streamnamerx = "";
    std::string servername = "";
    VBanPacket txPacket{0};
    UDPSocket socket;
    uint8_t redtx = 1;
    uint8_t redrx = 2;
    std::atomic<bool> running{true};
    std::thread netThread;
    ringbuffer_t* ringbuffer = nullptr;
    VResampler* resampler = nullptr;
    int rbfill = 0;
    double rratio = 1;
    float integral_rr = 0;
    float error_rr = 0;
    float Kp = 0.00002;
    float Ki = 0.0000005;

    uint16_t srchTimer = 0;
    uint16_t srchTimerMax;
    std::atomic<uint16_t> rxResetTimer;
    uint16_t rxResetTimerMax;
    std::vector<uint32_t> ips;
    MedianFilterInt rbfillm(11);

    uint32_t flags = 0;
    #define RECEIVER 0x0001
    #define TRANSMITTER 0x0002
    #define RECEIVING 0x0004
    #define SENDING 0x0008

    if (argc > 1) for (int i = 1; i < argc; i++)
    {
        args+= std::string(argv[i]);
        if (i < (argc - 1)) args+= " ";
    }
    std::cerr << args << std::endl;
    if (args.size() != 0)
    {
        std::string optArg;
        netDev = getVal(args, "netdev");
        captDev = getVal(args, "captdev");
        playDev = getVal(args, "playdev");
        ctrlDev = getVal(args, "ctrldev");
        optArg.clear();
        optArg = getVal(args, "iptx");
        if (optArg!= "")
        {
            txip = inet_addr(optArg.c_str());
            uint32_t current_txip = txip.load();
            if (((current_txip & 0xFFFFFF) == 0) && ((current_txip & 0xFF000000) != 0)) txip = (current_txip >> 24);
            std::cerr << "TX IP Address: " << optArg << std::endl;
        }
        optArg.clear();
        optArg = getVal(args, "iprx");
        if (optArg!= "")
        {
            rxip = inet_addr(optArg.c_str());
            std::cerr << "RX IP Address: " << optArg << std::endl;
        }
        optArg.clear();
        optArg = getVal(args, "porttx");
        if (optArg!= "")
        {
            try { txport = htons(atoi(getVal(args, "porttx").c_str())); } catch (const std::exception& e) { txport = 6980; }
            std::cerr << "TX port: " << optArg << std::endl;
        }
        optArg.clear();
        optArg = getVal(args, "portrx");
        if (optArg!= "")
        {
            try { rxport = htons(atoi(getVal(args, "portrx").c_str())); } catch (const std::exception& e) { rxport = 6980; }
            std::cerr << "RX port: " << optArg << std::endl;
        }
        streamnametx = getVal(args, "streamnametx");
        streamnamerx = getVal(args, "streamnamerx");
        servername = getVal(args, "servername");
        if (getVal(args, "format").find("16") != std::string::npos) vbanFormat = VBAN_BITFMT_16_INT;
        if (getVal(args, "format").find("24") != std::string::npos) vbanFormat = VBAN_BITFMT_24_INT;
        try {std::string nbc = getVal(args, "nbinputs"); if (nbc!= "") nbinputs = atoi(nbc.c_str());} catch(const std::exception& e) {nbinputs = 2;}
        try {std::string nbc = getVal(args, "nboutputs"); if (nbc!= "") nboutputs = atoi(nbc.c_str());} catch(const std::exception& e) {nboutputs = 2;}
        try {std::string nbc = getVal(args, "offset"); if (nbc!= "") offset = atoi(nbc.c_str());} catch(const std::exception& e) {offset = 2;}
    }

    if (!netDev.empty()&&(txip > 0)&&(txip < 256))
    {
        ips = getIPAddresses(netDev);
        if (!ips.empty()) txip = (ips[0] & 0xFFFFFF) | (txip.load() << 24);
        else txip = 0;
    }
    else txip = 0;
    fprintf(stderr, "Dest IP: %d.%d.%d.%d\r\n", ((uint8_t*)&txip)[0], ((uint8_t*)&txip)[1], ((uint8_t*)&txip)[2], ((uint8_t*)&txip)[3]);

    if (!socket.init(rxip, rxport, txip, txport, 6, true))
    {
        std::cerr << "ERROR! Cannot init the socket" << std::endl;
        return -1;
    }

    if (nbinputs == 0)  captDev.clear();
    if (nboutputs == 0) playDev.clear();
    Alsa_pcmi *device = new Alsa_pcmi(playDev.data(), captDev.data(), ctrlDev.data(), samplerate, nframes, nperiods, (uint32_t)debugEnabled);

    if (device->state() == Alsa_pcmi::STATE_FAIL)
    {
        std::cerr << "ERROR! Cannot init audio device!" << std::endl;
        device->printinfo();
        delete device;
        return 1;
    }

    if (nbinputs!= 0) nbinputs = std::min(nbinputs, (uint16_t)device->ncapt());
    if (nboutputs!= 0) nboutputs = std::min(nboutputs, (uint16_t)device->nplay());

    std::cout << "Device successfully opened!" << std::endl;
    std::cout << "Inputs: " << nbinputs << std::endl;
    std::cout << "Outputs: " << nboutputs << std::endl;

    std::vector<std::vector<float>> inbuf(nbinputs, std::vector<float>(nframes, 0.0f));
    std::vector<std::vector<float>> outbuf(nboutputs, std::vector<float>(nframes, 0.0f));
    std::vector<float> outFrameBuf(nboutputs);

    srchTimerMax = uint16_t(roundf((float)samplerate/(float)nframes) - 1);
    rxResetTimerMax = uint16_t(roundf((float)samplerate/(10.0f * (float)nframes)) - 1);

    if (nbinputs > 0)
    {
        captBuf.resize(nbinputs * nframes, 0.0f);
        if (vbanFormat!= VBAN_BITFMT_32_FLOAT)
        {
            txBuf.resize(nbinputs * nframes * VBanBitResolutionSize[vbanFormat], 0);
            txBuffer = (char*)txBuf.data();
            txBufLen = nbinputs * nframes * VBanBitResolutionSize[vbanFormat];
        }
        else
        {
            txBuffer = (char*)captBuf.data();
            txBufLen = nbinputs * nframes * sizeof(float);
        }
        txPacketDataLen = txBufLen;
        nPackets = 1;
        pacNFrames = nframes;
        while((txPacketDataLen > VBAN_DATA_MAX_SIZE)||(pacNFrames > VBAN_SAMPLES_MAX_NB))
        {
            txPacketDataLen = txPacketDataLen / 2;
            pacNFrames = pacNFrames / 2;
            nPackets = nPackets * 2;
        }

        txPacket.header.vban = VBAN_HEADER_FOURC;
        for (txPacket.header.format_SR = 0; txPacket.header.format_SR < VBAN_SR_MAXNUMBER; txPacket.header.format_SR++)
            if (VBanSRList[txPacket.header.format_SR] == samplerate) break;
        if (txPacket.header.format_SR == VBAN_SR_MAXNUMBER)
        {
            std::cout << "ERROR! Unsupported samplerate!" << std::endl;
            return 1;
        }
        txPacket.header.format_bit = vbanFormat;
        txPacket.header.format_nbs = pacNFrames - 1;
        txPacket.header.format_nbc = nbinputs - 1;
        strncpy(txPacket.header.streamname, streamnametx.c_str(), streamnametx.size());

        flags|= TRANSMITTER;
    }

    if (nboutputs > 0)
    {
        //Kp = 123.0f / ((float)nframes * (float)samplerate);
        //Ki = (3.0f * (float)nframes) / (16000 * (float)samplerate);
        ringbuffer = ringbuffer_create(8 * nboutputs * nframes * sizeof(float)); //(2 + redrx)
        std::vector<char> zeros(4 * nboutputs * nframes * sizeof(float));
        ringbuffer_write(ringbuffer, zeros.data(), 4 * nboutputs * nframes * sizeof(float));

        flags|= RECEIVER;
    }

    netThread = std::thread([&]()
    {
        struct timespec tsOld{0};
        struct timespec tsNew{0};
        uint32_t resamplerInBufLen = 0;
        std::vector<float> resamplerInBuf{0};
        uint32_t resamplerOutBufLen = 0;
        std::vector<float> resamplerOutBuf{0};
        uint32_t rxNuFrame = 0;
        int framesize = nboutputs * sizeof(float);
        int sockFd = socket.getFd();
        pollfd pfd;
        pfd.fd = sockFd;
        pfd.events = POLLIN;

        while (running)
        {
            int ret = poll(&pfd, 1, 100);
            if (ret > 0 && (pfd.revents & POLLIN))
            {
                VBanPacket rxPacket{0};
                uint32_t from_ip;
                uint16_t from_port;
                int bytesRead = socket.receivePacket(rxPacket, from_ip, from_port);
                if (bytesRead > 0)
                {
                    clock_gettime(CLOCK_MONOTONIC_RAW, &tsNew);
                    if (rxPacket.header.vban == VBAN_HEADER_FOURC)
                    {
                        switch (rxPacket.header.format_SR&VBAN_PROTOCOL_MASK)
                        {
                            case VBAN_PROTOCOL_AUDIO:
                                if ((rxip == from_ip)&&(strcmp(rxPacket.header.streamname, streamnamerx.data()) == 0)&&(rxNuFrame != rxPacket.header.nuFrame)) // Stream presents // &&(rxPacket.header.format_nbc + 1 == nboutputs)
                                {
                                    if (resamplerInBufLen < (rxPacket.header.format_nbs + 1))
                                    {
                                        resamplerInBufLen = rxPacket.header.format_nbs + 1;
                                        resamplerInBuf.resize(resamplerInBufLen * nboutputs);
                                        resamplerOutBufLen = resamplerInBufLen * (double)samplerate/(double)VBanSRList[rxPacket.header.format_SR&VBAN_SR_MASK] + 2;
                                        resamplerOutBuf.resize(resamplerOutBufLen * nboutputs);
                                    }
                                    else if (resamplerInBufLen > (rxPacket.header.format_nbs + 1))
                                    {
                                        resamplerInBufLen = rxPacket.header.format_nbs + 1;
                                        resamplerOutBufLen = resamplerInBufLen * (double)samplerate/(double)VBanSRList[rxPacket.header.format_SR&VBAN_SR_MASK] + 2;
                                    }
                                    int nboutputsIn = rxPacket.header.format_nbc + 1;
                                    int pacFrameSize = VBanBitResolutionSize[rxPacket.header.format_bit] * (rxPacket.header.format_nbc + 1);
                                    int nbcMin = std::min(nboutputsIn, (int)nboutputs);
                                    int offsetSize = (offset > nboutputsIn ? 0 : VBanBitResolutionSize[rxPacket.header.format_bit] * offset);
                                    for (int frame = 0; frame < rxPacket.header.format_nbs + 1; frame++)
                                    {
                                        if (rxPacket.header.format_bit == VBAN_BITFMT_32_FLOAT)
                                            memcpy(&resamplerInBuf.data()[frame * nboutputs], rxPacket.data + frame * pacFrameSize + offsetSize, nbcMin * sizeof(float));
                                        else convertVbanToFloat(rxPacket.data + frame * pacFrameSize + offsetSize, &resamplerInBuf.data()[frame * nboutputs], nbcMin, rxPacket.header.format_bit);
                                    }
                                    // if (rxPacket.header.format_bit == VBAN_BITFMT_32_FLOAT) memcpy(resamplerInBuf.data(), rxPacket.data, (rxPacket.header.format_nbs + 1) * (rxPacket.header.format_nbc + 1) * sizeof(float));
                                    // else convertVbanToFloat(rxPacket.data, resamplerInBuf.data(), (rxPacket.header.format_nbs + 1) * (rxPacket.header.format_nbc + 1), rxPacket.header.format_bit);
                                    resampler->inp_count = resamplerInBufLen;
                                    resampler->inp_data = resamplerInBuf.data();
                                    resampler->out_count = resamplerOutBufLen;
                                    resampler->out_data = resamplerOutBuf.data();
                                    resampler->process();

                                    int frame;
                                    for (frame = 0; frame < (resamplerOutBufLen - resampler->out_count); frame++)
                                        if (ringbuffer_write_space(ringbuffer) >= framesize) ringbuffer_write(ringbuffer, (char*)&(resamplerOutBuf.data()[frame * nboutputs]), framesize);

                                    if (tsOld.tv_nsec!= 0 && tsOld.tv_sec!= 0)
                                    {
                                        uint64_t deltaT = (tsNew.tv_sec - tsOld.tv_sec)*1000000000 + tsNew.tv_nsec - tsOld.tv_nsec;
                                        if (deltaT > 100000) // NEW TRANSACTION
                                        {
                                            if (cycleFrames > nframes) rbfill = rbfillm.process(ringbuffer_read_space(ringbuffer)/framesize);
                                            cycleFrames = rxPacket.header.format_nbs + 1;
                                        }
                                        else cycleFrames += rxPacket.header.format_nbs + 1;
                                    }
                                    rxResetTimer = 0;
                                    rxNuFrame = rxPacket.header.nuFrame;
                                }
                                else if ((nboutputs)&&(rxip == 0)&&(strcmp(rxPacket.header.streamname, streamnamerx.data()) == 0))// INIT STREAM
                                {
                                    std::cerr << "CREATING STREAM" << std::endl;
                                    if (resampler != nullptr) delete resampler;
                                    resampler = new VResampler;
                                    double ratio = (double)samplerate/(double)VBanSRList[rxPacket.header.format_SR&VBAN_SR_MASK];
                                    if (resampler->setup(ratio, nboutputs, 64))
                                    {
                                        fprintf (stderr, "Resampler can't handle the ratio\r\n");
                                        exit(1);
                                    }
                                    resampler->inp_count = resampler->inpsize() - 1;
                                    resampler->inp_data = 0;
                                    resampler->out_count = 999999;
                                    resampler->out_data = 0;
                                    resampler->process();
                                    rxip = from_ip;
                                    rxNuFrame = rxPacket.header.nuFrame;
                                    flags|= RECEIVING;
                                }
                                break;
                            case VBAN_PROTOCOL_TXT:
                                if (strncmp(rxPacket.header.streamname, "INFO", 4) == 0)
                                {
                                    //if (bytesRead >= VBAN_HEADER_SIZE + 5)
                                    if (strncmp(rxPacket.data, "/info", 5) == 0 && !ipIsLocal(from_ip, ips.data(), ips.size()))
                                    {
                                        const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&from_ip);
                                        std::cerr << "Info request from: " << (int)ip_bytes[0] << "." << (int)ip_bytes[1] << "." << (int)ip_bytes[2] << "." << (int)ip_bytes[3] << std::endl;
                                        VBanPacket info{0};
                                        memset(&info, 0, sizeof(VBanPacket));
                                        info.header.vban = VBAN_HEADER_FOURC;
                                        info.header.format_SR = VBAN_PROTOCOL_TXT;
                                        strcat(info.header.streamname, "INFO");
                                        sprintf(info.data, "streamnamerx=%s streamnametx=%s samplerate=%d format=%d nbinputs=%d nboutputs=%d",
                                                streamnamerx.data(),
                                                streamnametx.data(),
                                                samplerate,
                                                vbanFormat,
                                                nbinputs,
                                                nboutputs);
                                        socket.setTxTarget(from_ip, from_port);
                                        socket.sendPacket(info, VBAN_HEADER_SIZE + strlen(info.data));
                                        socket.setTxTarget(txip, txport);
                                    }
                                    else if (txip == 0)
                                    {
                                        if ((servername.size() != 0)&&(streamnametx.size() != 0)&&(servername == getVal(rxPacket.data, "servername"))&&((from_ip&0xFF)!= 127))
                                        {
                                            //fprintf(stderr, "%s\r\n", rxPacket.data);
                                            fprintf(stderr, "Receptor found on %d.%d.%d.%d\r\n", ((uint8_t*)&from_ip)[0], ((uint8_t*)&from_ip)[1], ((uint8_t*)&from_ip)[2], ((uint8_t*)&from_ip)[3]);
                                            txip = from_ip;
                                            socket.setTxTarget(txip, txport);
                                            flags|= SENDING;
                                        }
                                        else if ((servername.size() == 0)&&(streamnametx.size() != 0)&&(streamnametx == getVal(rxPacket.data, "streamnamerx"))&&((from_ip&0xFF)!= 127))
                                        {
                                            //fprintf(stderr, "%s\r\n", rxPacket.data);
                                            fprintf(stderr, "Receptor found on %d.%d.%d.%d\r\n", ((uint8_t*)&from_ip)[0], ((uint8_t*)&from_ip)[1], ((uint8_t*)&from_ip)[2], ((uint8_t*)&from_ip)[3]);
                                            txip = from_ip;
                                            socket.setTxTarget(txip, txport);
                                            flags|= SENDING;
                                        }
                                    }
                                }
                                break;
                            default:
                                break;
                        }
                    }
                    tsOld = tsNew;
                }
            }
        }
    });

    //nframes = device->fsize();
    device->pcm_start();

    while (true)
    {
        snd_pcm_sframes_t frames = device->pcm_wait();
        if (frames < 0)
        {
            std::cerr << "XRUN!!!" << std::endl;
            continue;
        }
        if (device->state() == SND_PCM_STATE_XRUN)
        {
            std::cerr << "XRUN!!! Recovering..." << std::endl;
            continue;
        }

        if (nbinputs > 0)
        {
            device->capt_init(nframes);
            for (uint16_t channel = 0; channel < nbinputs; channel++)
                device->capt_chan(channel, inbuf[channel].data(), nframes);
            device->capt_done(nframes);

            for (uint16_t frame = 0; frame < nframes; frame++)
                for (uint16_t channel = 0; channel < nbinputs; channel++)
                    captBuf[frame * nbinputs + channel] = inbuf[channel][frame];
            if (vbanFormat!= VBAN_BITFMT_32_FLOAT) convertFloatToVban(captBuf.data(), txBuffer, nframes, vbanFormat);
            if (txip != 0 && streamnametx.data()[0]!= 0) for (uint32_t packet = 0; packet < nPackets; packet++)
            {
                memcpy(txPacket.data, txBuffer + packet * txPacketDataLen, txPacketDataLen);
                for (uint8_t r = 0; r < redtx; r++) socket.sendPacket(txPacket, txPacketDataLen);
                txPacket.header.nuFrame++;
            }
            else
            {
                if (srchTimer == srchTimerMax)
                {
                    if (streamnametx.data()[0]!= 0)
                    {
                        VBanPacket request{0};
                        request.header.vban = VBAN_HEADER_FOURC;
                        request.header.format_SR = VBAN_PROTOCOL_TXT;
                        strcat(request.header.streamname, "INFO");
                        strcat(request.data, "/info");
                        ips = getIPAddresses();
                        for (uint16_t i = 0; i < ips.size(); i++)
                        {
                            socket.setTxTarget(ips.at(i)|0xFF000000, txport);
                            socket.sendPacket(request, 5);
                        }
                    }
                    srchTimer = 0;
                }
                else srchTimer++;
            }
        }

        if (nboutputs > 0)
        {
            int framesize = nboutputs * sizeof(float);
            if (cycleFrames <= nframes) rbfill = rbfillm.process(ringbuffer_read_space(ringbuffer)/framesize);
            for (uint16_t frame = 0; frame < nframes; frame++)
            {
                if (ringbuffer_read_space(ringbuffer) >= framesize) ringbuffer_read(ringbuffer, (char*)outFrameBuf.data(), framesize);
                for (uint16_t channel = 0; channel < nboutputs; channel++)  outbuf[channel][frame] = outFrameBuf[channel];
            }

            // for (uint16_t frame = 0; frame < nframes; frame++)
            // {
            //     float mic_sample = (nbinputs > 0) ? inbuf[0][frame] : 0.0f;
            //     for (uint16_t channel = 0; channel < nboutputs; channel++)  outbuf[channel][frame] += mic_sample;
            // }

            device->play_init(nframes);
            for (uint16_t channel = 0; channel < nboutputs; channel++) device->play_chan(channel, outbuf[channel].data(), nframes);
            device->play_done(nframes);

            if (rxip != 0 && flags&RECEIVING)
            {
                if (rxResetTimer < rxResetTimerMax) rxResetTimer++;
                // else if (rxResetTimer == rxResetTimerMax)
                // {
                //     rxip = 0;
                //     rxResetTimer = 0;
                //     std::cerr << "Incoming stream stopped!" << std::endl;
                // }
                else if (rxResetTimer == rxResetTimerMax)
                {
                    rxip = 0;
                    rxResetTimer = 0;
                    std::cerr << "Incoming stream stopped!" << std::endl;// 1. Безопасно сносим ресемплер, чтобы при новом старте он создавался строго с нуля
                    if (resampler != nullptr)
                    {
                        delete resampler;
                        resampler = nullptr; // Сетевой поток увидит nullptr и корректно создаст новый
                    }
                    if (ringbuffer != nullptr)
                    {
                        int bytes_left = ringbuffer_read_space(ringbuffer);
                        if (bytes_left > 0)
                        {
                            std::vector<char> dummy(bytes_left);
                            ringbuffer_read(ringbuffer, dummy.data(), bytes_left);
                        }
                    }
                }
            }

            calculateRRatio(&rratio, &integral_rr, &error_rr, nframes + (nframes>>1) + 3, rbfill);//, Kp, Ki);
            if (resampler!= nullptr) resampler->set_rratio(rratio);
        }
    }

    running = false;
    if (netThread.joinable()) netThread.join();
    device->pcm_stop();

    delete device;
    return 0;
}
