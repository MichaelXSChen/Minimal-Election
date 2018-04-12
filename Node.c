//
// Created by Michael Xusheng Chen on 11/4/2018.
//
#include "Node.h"
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>



#define BUFLEN 1024
#define MSG_LEN 37

#define ELEC_PREPARE 0
#define ELEC_PREPARED 1
#define ELEC_CONFIRM 2
#define ELEC_CONFIRMED 3;


struct prepared_state{
    uint64_t max_rand;
    char addr[20];
    int is_me;
};

typedef struct prepared_state prepared_state;


struct Term_t{
    uint64_t start_block;
    uint64_t len;
    uint64_t cur_block;
    struct sockaddr_in *members;
    uint64_t member_count;
    //Protocol state.
    prepared_state *prepared; //size is len;
    uint64_t *Confirmed;
};

typedef struct Term_t Term_t;

Term_t *term; //global variable term


struct Message{
    uint64_t rand;
    uint64_t blockNum;
    uint8_t message_type;
    char addr[20];
};

typedef struct Message Message;

char* serialize(const Message *msg){
    char *output = malloc(MSG_LEN * sizeof(char));
    memcpy(&output[0], &msg->rand, 8);
    memcpy(&output[8], &msg->blockNum, 8);
    memcpy(&output[16], &msg->message_type, 1);
    memcpy(&output[17], msg->addr, 20);
    return output;
}

Message deserialize(char *input){
    Message msg;
    memcpy(&msg.rand, &input[0], 8);
    memcpy(&msg.blockNum, &input[8], 8);
    memcpy(&msg.message_type, &input[16], 1);
    memcpy(&msg.addr, &input[17], 20);
    return msg;
}






int New_Node(int member_size){
    term = (Term_t*)malloc(sizeof(Term_t));
    //hard_coded;
    term->start_block = 1;
    term->len = 1000;
    term->member_count = 3;



    term->cur_block = term->start_block - 1;
    term->prepared = (prepared_state *) malloc(term->len * sizeof(prepared_state));
    




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

void handle_prepare(const Message *msg){
    uint64_t offset = msg->blockNum - term->start_block;

}



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

    char buffer[1024];
    int recv_len;
    struct sockaddr_in si_other;
    while(1){
        recv_len = recvfrom(s, buffer, BUFLEN, 0, (struct sockaddr *)&si_other, sizeof(si_other));
        if (recv_len != MSG_LEN){
            printf("Wrong Message Format");
        }
        Message msg = deserialize(buffer);
        switch(msg.message_type){
            case ELEC_PREPARE :
                handle_prepare()
                break;


        }
        
    }

}

int elect(uint64_t round){
    srand(time(NULL));

}