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


enum state_t{
    STATE_EMPTY = 0,
    //propose
    STATE_PREPARE_SENT = 1,
    STATE_CONFIRM_SENT = 2,
    STATE_ELECTED = 3,
    //follower
    STATE_PREPARED = 4,
    STATE_CONFIRMED = 5
};


struct instance_t{
    enum state_t state;
    uint64_t max_rand;
    char addr[20];
    char **prepared_addr;
    int prepared_addr_count;
    char **confirmed_addr;
    int confirmed_addr_count;
};

typedef struct instance_t instance_t;


struct Term_t{
    uint64_t start_block;
    uint64_t len;
    uint64_t cur_block;
    struct sockaddr_in *members;
    uint64_t member_count;
    //Protocol state.
    instance_t *instances; //size is len;'
    //
    char my_addr[20];

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
    term->instances = (instance_t *) malloc(term->len * sizeof(instance_t));
    int i;
    for (i = 0; i < term->len; i++){
        term->instances[i].max_rand = 0;
        term->instances[i].state = STATE_EMPTY;
        term->instances[i].confirmed_addr_count = 0;
        term->instances[i].prepared_addr_count = 0;
    }




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

void handle_prepare(const Message *msg, int socket, const struct sockaddr_in *si_other){
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];

    if (instance->state == STATE_EMPTY || instance->state == STATE_PREPARED || instance->state == STATE_PREPARE_SENT) {
        if (msg->rand > instance->max_rand) {
            //receive
            instance->max_rand = msg->rand;
            memcpy(instance->addr, msg->addr, 20);
            instance->state = STATE_PREPARED;
            Message resp;
            resp.rand = msg->rand;
            resp.blockNum = msg->blockNum;
            resp.message_type = ELEC_PREPARED;
            memcpy(resp.addr, term->my_addr, 20);
            char *output = serialize(&resp);
            if (sendto(socket, output, MSG_LEN, 0, (struct sockaddr *) &si_other, sizeof(si_other)) == -1) {
                printf("Failed to send resp");
            }
        }
    }
}


int insert_addr(char **addr_array, char *addr,  int *count){
    int i;
    for (i = 0; i<*count; i++){
        if (memcmp(addr_array[i], addr, 20) == 0){
            return 1; //already in the list.
        }
    }
    memcpy(addr_array[*count], addr, 20);
    *count = *count +1;
    return 0;
}

int broadcast(const Message *msg, int socket){
    int i;
    ssize_t ret;
    char *buf = serialize(msg);
    for (i = 0; i<term->member_count; i++){
        ret = sendto(socket, buf, MSG_LEN, 0, (struct sockaddr*)&term->members[i], sizeof(struct sockaddr_in));
        if (ret == -1){
            printf("Failed to send message");
            return -1;
        }
    }
    return 0;
}

void handle_prepared(const Message *msg, int socket, const struct sockaddr_in *si_other) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    if (instance->state == STATE_PREPARE_SENT){
        int ret = insert_addr(instance->prepared_addr, msg->addr, &instance->prepared_addr_count);
        if (ret != 0){
            return;
        }
        if (instance->prepared_addr_count > term->member_count  / 2) {
            Message resp;
            memcpy(resp.addr, term->my_addr, 20);
            resp.message_type = ELEC_CONFIRM;
            resp.blockNum =  msg->blockNum;
            resp.rand = msg->rand;
            ret = broadcast(&resp, socket);
            if (ret != 0){
                printf("failed to broadcast Confirm message");
            }
        }
        instance->state = STATE_CONFIRM_SENT;
    }

}



void handle_confirm(const Message *msg, int socket, const struct sockaddr_in *si_other) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    if (instance-> max_rand > msg->rand){
        //already prepared a higher.
        return;
    }
    if (instance-> state == STATE_CONFIRM_SENT || instance ->state == STATE_CONFIRMED) {
        return;
    }
    Message resp;
    memcpy(resp.addr, term->my_addr, 20);
    resp.rand = msg->rand;
    resp.blockNum = msg->blockNum;
    resp.message_type = ELEC_CONFIRMED;
    char *output = serialize(&resp);
    if (sendto(socket, output, MSG_LEN, 0, (struct sockaddr *) &si_other, sizeof(si_other)) == -1) {
        printf("Failed to send resp");
    }
    instance->state = STATE_CONFIRMED;
    return;
}

void handle_confirmed(const Message *msg, int socket, const struct sockaddr_in *si_other) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    if (instance->state == STATE_CONFIRM_SENT) {
        int ret = insert_addr(instance->confirmed_addr, msg->addr, &instance->confirmed_addr_count);
        if (ret != 0) {
            return;
        }
        if (instance->confirmed_addr_count > term->member_count / 2) {
            instance->state = STATE_ELECTED;
        }
    }
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
                handle_prepare(&msg, s, &si_other);
                break;
            case ELEC_PREPARED :
                handle_prepared(&msg, s, &si_other);
                break;
            case ELEC_CONFIRM :
                handle_confirm(&msg, s, &si_other);
                break;
            case ELEC_CONFIRMED :
                handle_confirmed(&msg, s, &si_other);
                break;
        }

    }

}
