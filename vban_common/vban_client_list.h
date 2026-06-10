#ifndef VBAN_CLIENT_LIST_H_
#define VBAN_CLIENT_LIST_H_

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vban.h"


typedef struct client_id_t
{
    union
    {
        uint32_t ip;
        uint8_t ip_bytes[4];
    };
    uint16_t port;
    VBanHeader header;
    char pipename[36];
    int pipedesc;
    int txpipedesc;
    FILE* process;
    FILE* pipe;
    pid_t pid;
    pid_t txpid;
    uint8_t timer;
    client_id_t* next = NULL;
} __attribute__((packed)) client_id_t;


int create_list_head(client_id_t* list);
int append_to_list(client_id_t* list);
int push(client_id_t** list);
int pop(client_id_t ** list);
int remove_last(client_id_t* list);
int remove_by_index(client_id_t** list, int n);

#endif
