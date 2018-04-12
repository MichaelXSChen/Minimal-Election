//
// Created by Michael Xusheng Chen on 11/4/2018.
//
#include "Node.h"
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include<sys/socket.h>
#include<arpa/inet.h>

struct Term{
    uint64_t start_block;
    uint64_t len;
    uint64_t cur_block;
    struct sockaddr_in *members;
    uint64_t member_count;
};

typedef struct Term Term;

const MSG_PREPARE = 0;
const MSG_PREPARED = 1;
const MSG_CONFIRM = 2;
const MSG_CONFIRMED = 3;

struct Message{
    uint64_t rand;
    uint64_t blockNum;
    uint8_t message_type;
    char addr[20];
};



int New_Node(int member_size){
    pthread_t recvt;
    int ret;
    ret = pthread_create(&recvt, NULL, RecvFunc, NULL);
    if (ret != 0){
        printf("Failed to create thread");
    }

}

struct RecvFuncParam{
    struct sockaddr_in me;


};


void *RecvFunc(void *opaque){
    int s;
    s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0){
        printf("Failed to create socket");
        pthread_exit(NULL);
    }
    struct RecvFuncParam *param = (struct RecvFuncParam *)opaque;
    if (bind(s, (struct sockaddr*)&param->me, sizeof(param->me)) == -1){
        printf("Failed to bind to socket");
        pthread_exit(NULL);
    }
    while(1){
        
    }

}

int elect(uint64_t round){
    srand(time(NULL));

}