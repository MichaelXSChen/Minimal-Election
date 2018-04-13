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
#include <unistd.h>



#define BUFLEN 1024
#define MSG_LEN 37

#define ELEC_PREPARE 0
#define ELEC_PREPARED 1
#define ELEC_CONFIRM 2
#define ELEC_CONFIRMED 3
#define ELEC_ANNOUNCE 4  //only for debug usage. 




struct Message{
    uint64_t rand;
    uint64_t blockNum;
    uint8_t message_type;
    char addr[20];
};
typedef struct Message Message;


static void *RecvFunc(void *opaque);
static char* serialize(const Message *msg);
static Message deserialize(char *input);
static int broadcast(const Message *msg,Term_t *term);
static int insert_addr(char **addr_array, const char *addr,  int *count);

Term_t* New_Node(int offset){ //currently for hardcoded message.
    Term_t* term;
    term = (Term_t*)malloc(sizeof(Term_t));
    //hard_coded;
    term->start_block = 1;
    term->len = 1000;
    term->member_count = 3;
    term->members = (struct sockaddr_in *)malloc(3 * sizeof(struct sockaddr_in));
    uint16_t x;
    for (x =0; x<3; x++){
        memset(&term->members[x], 9, sizeof(struct sockaddr_in));
        term->members[x].sin_family = AF_INET;
        term->members[x].sin_port = htons(11110+x);
        term->members[x].sin_addr.s_addr = htonl(INADDR_ANY);
    }
    term->my_addr[0] = 'A' + offset;


    int i;
    term->cur_block = term->start_block - 1;
    term->instances = (instance_t *) malloc(term->len * sizeof(instance_t));
    for (i = 0; i < term->len; i++){
        instance_t *instance = &term->instances[i];

        pthread_spin_init(&instance->lock, 0);
        instance->max_rand = 0;
        instance->state = STATE_EMPTY;
        instance->confirmed_addr_count = 0;
        instance->prepared_addr_count = 0;


        instance->prepared_addr=(char**)malloc(term->member_count * sizeof(char*));
        instance->confirmed_addr=(char**)malloc(term->member_count * sizeof(char*));
        int j = 0;
        for (j = 0; j<term->member_count; j++){
            instance->prepared_addr[j] = (char *)malloc(20 * sizeof(char));
            instance->confirmed_addr[j] = (char *)malloc(20 * sizeof(char));
        }
    }

    term->sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (term->sock < 0){
        printf("Failed to create socket");
        pthread_exit(NULL);
    }
    if (bind(term->sock, (struct sockaddr*)&term->my_addr, sizeof(term->my_addr)) == -1){
        printf("Failed to bind to socket");
        pthread_exit(NULL);
    }

    pthread_t recvt;
    int ret;
    ret = pthread_create(&recvt, NULL, RecvFunc, (void *)term);
    if (ret != 0){
        printf("Failed to create thread");
    }



    printf("New Node created");
    return term; 
}


void handle_prepare(const Message *msg, const struct sockaddr_in *si_other, Term_t *term){
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_spin_lock(&instance->lock);
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
    pthread_spin_unlock(&instance->lock);
}


void handle_prepared(const Message *msg, const struct sockaddr_in *si_other, Term_t *term) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_spin_lock(&instance->lock);
    if (instance->state == STATE_PREPARE_SENT){
        int ret = insert_addr(instance->prepared_addr, msg->addr, &instance->prepared_addr_count);
        if (ret != 0){
            pthread_spin_unlock(&instance->lock);
            return;
        }
        if (instance->prepared_addr_count > term->member_count  / 2) {
            Message resp;
            memcpy(resp.addr, term->my_addr, 20);
            resp.message_type = ELEC_CONFIRM;
            resp.blockNum =  msg->blockNum;
            resp.rand = msg->rand;
            ret = broadcast(&resp, term);
            if (ret != 0){
                printf("failed to broadcast Confirm message");
            }
        }
        instance->state = STATE_CONFIRM_SENT;
    }
    pthread_spin_unlock(&instance->lock);
}



void handle_confirm(const Message *msg, const struct sockaddr_in *si_other, Term_t *term) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_spin_lock(&instance->lock);
    if (instance-> max_rand > msg->rand){
        pthread_spin_unlock(&instance->lock);
        //already prepared a higher.
        return;
    }
    if (instance-> state == STATE_CONFIRM_SENT || instance ->state == STATE_CONFIRMED) {
        pthread_spin_unlock(&instance->lock);
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
    pthread_spin_unlock(&instance->lock);
    return;
}

void handle_confirmed(const Message *msg, const struct sockaddr_in *si_other, Term_t *term) {
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_spin_lock(&instance->lock);         
    if (instance->state == STATE_CONFIRM_SENT) {
        int ret = insert_addr(instance->confirmed_addr, msg->addr, &instance->confirmed_addr_count);
        if (ret != 0) {
            pthread_spin_unlock(&instance->lock);
            return;
        }
        if (instance->confirmed_addr_count > term->member_count / 2) {
            Message resp;
            memcpy(resp.addr, term->my_addr, 20);
            resp.message_type = ELEC_ANNOUNCE;
            resp.blockNum =  msg->blockNum;
            resp.rand = msg->rand;
            ret = broadcast(&resp, term);
            if (ret != 0){
                printf("failed to broadcast Confirm message");
            }
            instance->state = STATE_ELECTED;
        }
    }
    pthread_spin_unlock(&instance->lock);
}

static void *RecvFunc(void *opaque){
    Term_t *term = (Term_t *)opaque;
    int s = term->sock;

    char buffer[1024];
    int recv_len;
    struct sockaddr_in si_other;
    socklen_t si_len = sizeof(si_other);
    while(1){
        recv_len = recvfrom(s, buffer, BUFLEN, 0, (struct sockaddr *)&si_other, &si_len);
        if (recv_len != MSG_LEN){
            printf("Wrong Message Format");
        }
        Message msg = deserialize(buffer);
        switch(msg.message_type){
            case ELEC_PREPARE :
                handle_prepare(&msg, &si_other, term);
                break;
            case ELEC_PREPARED :
                handle_prepared(&msg, &si_other, term);
                break;
            case ELEC_CONFIRM :
                handle_confirm(&msg, &si_other, term);
                break;
            case ELEC_CONFIRMED :
                handle_confirmed(&msg, &si_other, term);
                break;
            case ELEC_ANNOUNCE :
                printf("Leader elected for block %lu", msg.blockNum);
                break;

        }

    }

}

int elect(Term_t *term, uint64_t blk, uint64_t *value){
    uint64_t offset = blk - term->start_block;
    instance_t *instance = &term->instances[offset];

    srand((unsigned)time(NULL));
    uint64_t r = (uint64_t)rand();
    pthread_spin_lock(&instance->lock);
    if (r > instance->max_rand){
        Message msg;
        msg.blockNum = blk;
        msg.message_type = ELEC_PREPARE;
        msg.rand = r;
        memcpy(msg.addr, term->my_addr, 20);
        char *out = serialize(&msg);
        broadcast(&msg, term);
    }
    pthread_spin_unlock(&instance->lock);
    sleep(1);

}
//helper functions.

static char* serialize(const Message *msg){
    char *output = malloc(MSG_LEN * sizeof(char));
    memcpy(&output[0], &msg->rand, 8);
    memcpy(&output[8], &msg->blockNum, 8);
    memcpy(&output[16], &msg->message_type, 1);
    memcpy(&output[17], msg->addr, 20);
    return output;
}

static Message deserialize(char *input){
    Message msg;
    memcpy(&msg.rand, &input[0], 8);
    memcpy(&msg.blockNum, &input[8], 8);
    memcpy(&msg.message_type, &input[16], 1);
    memcpy(&msg.addr, &input[17], 20);
    return msg;
}

static int insert_addr(char **addr_array, const char *addr,  int *count){
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

static int broadcast(const Message *msg, Term_t *term){
    int socket = term->sock;
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