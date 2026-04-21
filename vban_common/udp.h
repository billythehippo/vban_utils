#pragma once

#ifndef UDP_H_
#define UDP_H_

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/errqueue.h>
#include <linux/icmp.h>
#include <linux/net_tstamp.h>
#endif

//#include "../vban_common/vban_functions.h"

typedef struct
{
    int fd;
    struct sockaddr_in c_addr;
} udpc_t;


int getipaddresses(uint32_t* ips, uint32_t* ipnum);
in_addr get_ip_by_name(char* __restrict ifname);
udpc_t* udp_init(uint16_t rx_port, uint16_t tx_port, char* __restrict rx_ip, char* __restrict tx_ip, suseconds_t t, uint8_t priority, int broadcast);
#ifdef __linux__
int set_recverr(int fd);
#endif
void udp_free(udpc_t* c);


inline int udp_send(udpc_t* c, uint16_t txport, char* data, size_t n, in_addr_t txip = 0)
{
    in_addr_t ip = c->c_addr.sin_addr.s_addr;
    if(txport!= 0) c->c_addr.sin_port = htons(txport);
    if(txip!= 0) c->c_addr.sin_addr.s_addr = txip;
    int ret = sendto(c->fd, data, n, 0, (struct sockaddr*)&(c->c_addr), sizeof(struct sockaddr_in));
    c->c_addr.sin_addr.s_addr = ip;
    if(ret < 0) return 0;
    return ret;
}


inline int udp_recv(udpc_t* c, void* data, size_t n)
{
	unsigned int c_addr_size = sizeof(struct sockaddr_in);
    int ret = recvfrom(c->fd, data, n, 0, (struct sockaddr*)&(c->c_addr), &c_addr_size);
	if(ret < 0) return 0;
	return ret;
}


#ifdef __linux__
inline int udp_recv_m(udpc_t* c, void* data, size_t n, struct timespec *timestamps = NULL)
{
    struct msghdr msg;
    char control_buf[CMSG_SPACE(sizeof(struct sock_extended_err))];
    struct iovec iov;
    iov.iov_base = (char*)data;
    iov.iov_len = n;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);
    msg.msg_flags = 0;

    ssize_t ret = recvmsg(c->fd, &msg, 0); //MSG_ERRQUEUE | MSG_PEEK
    if (ret < 0) return -1;

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        // if(cmsg && cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR)
        // {
        //     struct sock_extended_err *err = (struct sock_extended_err*)CMSG_DATA(cmsg);
        //     if(err->ee_origin == SO_EE_ORIGIN_ICMP || err->ee_origin == SO_EE_ORIGIN_ICMP6)
        //     {
        //         if(err->ee_type == ICMP_DEST_UNREACH) return err->ee_errno;
        //         if (err->ee_errno == ENOMSG && err->ee_origin == SO_EE_ORIGIN_TIMESTAMPING)
        //         {
        //             ;//printf("Packet ID (OPT_ID): %u ", serr->ee_data);
        //         }
        //     }
        // }

        // Get timestamps - 0 software, 2 hardware
        if (timestamps!= NULL && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING)
        {
            *timestamps = ((struct scm_timestamping*)CMSG_DATA(cmsg))->ts[0];
        }
    }
    return ret;
}
#endif


inline uint32_t udp_get_sender_ip(udpc_t* c)
{
    return htonl(c->c_addr.sin_addr.s_addr);
}


inline uint16_t udp_get_sender_port(udpc_t* c)
{
    return htons(c->c_addr.sin_port);
}


#ifdef __linux__
inline int check_send_status(int sockfd, struct timespec *timestamps = NULL)
{
    //struct scm_timestamping* ts;
    struct msghdr msg;
    char control_buf[CMSG_SPACE(sizeof(struct sock_extended_err))];
    struct iovec iov;
    char data_buf[1];
    iov.iov_base = data_buf;
    iov.iov_len = sizeof(data_buf);

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);
    msg.msg_flags = 0;

    ssize_t ret = recvmsg(sockfd, &msg, MSG_ERRQUEUE | MSG_PEEK);
    if(ret < 0) return -1;

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if(cmsg && cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR)
        {
            struct sock_extended_err *err = (struct sock_extended_err*)CMSG_DATA(cmsg);
            if(err->ee_origin == SO_EE_ORIGIN_ICMP || err->ee_origin == SO_EE_ORIGIN_ICMP6)
            {
                if(err->ee_type == ICMP_DEST_UNREACH) return err->ee_errno;
                if (err->ee_errno == ENOMSG && err->ee_origin == SO_EE_ORIGIN_TIMESTAMPING)
                {
                    ;//printf("Packet ID (OPT_ID): %u ", serr->ee_data);
                }
            }
        }
        // Get timestamps - 0 software, 2 hardware
        if (timestamps!= NULL && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING)
        {
            //ts = (struct scm_timestamping*)CMSG_DATA(cmsg);
            *timestamps = ((struct scm_timestamping*)CMSG_DATA(cmsg))->ts[0];
            //fprintf(stderr, "TX SW Timestamp: %ld.%09ld \r", ts->ts[0].tv_sec, ts->ts[0].tv_nsec);
        }
    }

    return 0;
}
#endif

#endif
