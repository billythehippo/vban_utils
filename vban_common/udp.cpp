#include "udp.h"


int getipaddresses(uint32_t* ips, uint32_t* ipnum)
{
    struct ifaddrs *ifaddr = nullptr, *ifa = nullptr;
    int s;
    char host[NI_MAXHOST];
    uint32_t num = 0;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if(ifa->ifa_addr->sa_family==AF_INET)//((strcmp(ifa->ifa_name,"wlan0")==0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                fprintf(stderr, "getnameinfo() failed: %s\n", gai_strerror(s));
                return 1;
            }
            //            printf("\tInterface : <%s>\n",ifa->ifa_name );
            //            printf("\t  Address : <%s>\n", host);
            ips[num] = inet_addr(host);
            num++;
        }
    }

    freeifaddrs(ifaddr);
    *ipnum = num;
    return 0;
}


in_addr get_ip_by_name(char* __restrict ifname)
{
    in_addr addr;
    struct ifaddrs *ifaddr = nullptr, *ifa = nullptr;
    int s;
    char host[NI_MAXHOST];
    memset(&addr, 0, sizeof(in_addr));

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        if(ifa->ifa_addr->sa_family==AF_INET)//((strcmp(ifa->ifa_name,"wlan0")==0)&&(ifa->ifa_addr->sa_family==AF_INET))
        {
            if (s != 0)
            {
                fprintf(stderr, "getnameinfo() failed: %s\n", gai_strerror(s));
                freeifaddrs(ifaddr);
                return addr;
            }
            if (strcmp(ifa->ifa_name, ifname)==0)
            {
                addr.s_addr = inet_addr(host);
                freeifaddrs(ifaddr);
                return addr;
            }
        }
    }
    freeifaddrs(ifaddr);
    fprintf(stderr, "Local IP address on %s: %s\r\n", ifname, inet_ntoa(addr));
    return addr;
}


udpc_t* udp_init(uint16_t rx_port, uint16_t tx_port, char* __restrict rx_ip, char* __restrict tx_ip, suseconds_t t, uint8_t priority, int broadcast)
{
    uint8_t prio = priority;
    int bcast = broadcast;

    udpc_t* c = (udpc_t*)malloc(sizeof(udpc_t));
    if(!c)
    {
        fprintf(stderr, "error rx_port=%04x", rx_port);
        return 0;
    }
    memset(c, 0, sizeof(udpc_t));
    //
    c->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(c->fd < 0)
    {
        fprintf(stderr, "error rx_port=%04x", rx_port);
        free(c);
        return 0;
    }

    if (bcast)
    {
        if(setsockopt(c->fd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast))<0)
        {
            fprintf(stderr, "error rx_port=%04x", rx_port);
            free(c);
            return 0;
        }
        fprintf(stderr, "Broadcast mode successfully set!\n");
    }
    //
#ifdef __linux__
    if(t != 0)
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = t;
        setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    }
    //
    if (priority>7) fprintf(stderr, "Wrong Priority value, usind default\n");
    else
    {
        setsockopt(c->fd, SOL_SOCKET, SO_PRIORITY, (const char*)&prio, sizeof(prio));
        fprintf(stderr, "Socket Priority is %d\n", prio);
    }

    int tsflags = SOF_TIMESTAMPING_TX_SOFTWARE |
                SOF_TIMESTAMPING_TX_HARDWARE |
                SOF_TIMESTAMPING_SOFTWARE |
                SOF_TIMESTAMPING_RAW_HARDWARE |
                SOF_TIMESTAMPING_OPT_ID;
    if (setsockopt(c->fd, SOL_SOCKET, SO_TIMESTAMPING, &tsflags, sizeof(tsflags)) < 0)
    {
        fprintf(stderr, "Can't switch timestamping on SO_TIMESTAMPING\r\n");
    }
#endif
    //
    if(rx_port!= 0)
    {
        struct sockaddr_in s_addr;
        memset(&s_addr, 0, sizeof(struct sockaddr_in));
        s_addr.sin_family = AF_INET;
        if (rx_ip == NULL) s_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        else s_addr.sin_addr.s_addr = inet_addr(rx_ip);//htonl(INADDR_LOOPBACK);
        s_addr.sin_port = htons(rx_port);
        int ret = bind(c->fd, (struct sockaddr*)&s_addr, sizeof(struct sockaddr_in));
        if(ret < 0)
        {
            fprintf(stderr, "error rx_port=%04x\r\n", rx_port);
            free(c);
            return 0;
        }
    }
    //
    if(tx_port != 0)
    {
        // int optval = 1;
        // if (setsockopt(c->fd, SOL_IP, IP_RECVERR, &optval, sizeof(optval)))
        // {
        //     perror("Error setting IP_RECVERR option");
        //     close(c->fd);
        //     exit(EXIT_FAILURE);
        // }

        c->c_addr.sin_family = AF_INET;
        c->c_addr.sin_addr.s_addr = inet_addr(tx_ip);//htonl(INADDR_LOOPBACK);
        c->c_addr.sin_port = htons(tx_port);
    }
    //
    return c;
}


#ifdef __linux__
int set_recverr(int fd)
{
    int optval = 1;
    if (setsockopt(fd, SOL_IP, IP_RECVERR, &optval, sizeof(optval)))
    {
        perror("Error setting IP_RECVERR option");
        return -1;
    }
    return 0;
}
#endif


void udp_free(udpc_t* c)
{
    close(c->fd);
    free(c);
    c = NULL;
}

