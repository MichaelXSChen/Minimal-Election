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
#include <sys/time.h>

#define DEBUG 1
#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define INFO 1
#define info_print(fmt, ...) \
            do { if (INFO) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define BUFLEN 1024
#define MSG_LEN 41

#define ELEC_PREPARE 1
#define ELEC_PREPARED 2
#define ELEC_CONFIRM 3
#define ELEC_CONFIRMED 4
#define ELEC_ANNOUNCE 5  //only for debug usage.
#define ELEC_NOTIFY 6 //Notify a proposer about the history before.



struct Message{
    uint64_t rand;
    uint64_t blockNum;
    uint8_t message_type;
    uint32_t owner_idx;    //The msg is for whom.
    char addr[20];    //Sender of the msg.
};
typedef struct Message Message;


static void *RecvFunc(void *opaque);
static char* serialize(const Message *msg);
static Message deserialize(char *input);
static int broadcast(const Message *msg,Term_t *term);
static int insert_addr(char **addr_array, const char *addr,  int *count);
static void init_instance(Term_t *term, instance_t *instance);
static int send_to_member(int index, const Message* msg, Term_t *term);

Term_t* New_Node(int offset){ //currently for hardcoded message.
    Term_t* term;
    term = (Term_t*)malloc(sizeof(Term_t));
    term->my_idx = (uint32_t)offset;
    //hard_coded;
    term->start_block = 1;
    term->len = 10000;
    term->member_count = 3;
    term->members = (struct sockaddr_in *)malloc(3 * sizeof(struct sockaddr_in));
    uint16_t x;
    for (x =0; x<3; x++){
        memset(&term->members[x], 0, sizeof(struct sockaddr_in));
        term->members[x].sin_family = AF_INET;
        term->members[x].sin_port = htons(10000+x);
        term->members[x].sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    term->my_account[0] = 'A' + offset;


    int i;
    term->cur_block = term->start_block - 1;
    term->instances = (instance_t *) malloc(term->len * sizeof(instance_t));
    for (i = 0; i < term->len; i++){
        init_instance(term, &term->instances[i]);
    }

    term->sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (term->sock < 0){
        perror("Failed to create socket\n");
        pthread_exit(NULL);
    }
    if (bind(term->sock, (struct sockaddr*)&term->members[offset], sizeof(term->members[offset])) == -1){
        perror("Failed to bind to socket\n");
        pthread_exit(NULL);
    }


    int ret;
    ret = pthread_create(&term->recvt, NULL, RecvFunc, (void *)term);
    if (ret != 0){
        perror("Failed to create thread");
    }

    return term;
}


void handle_prepare(const Message *msg, const struct sockaddr_in *si_other, Term_t *term, socklen_t si_len){
    debug_print("Received Prepare message, blk = %ld, from = %s, rand = %lu\n", msg->blockNum, msg->addr, msg->rand);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_mutex_lock(&instance->state_lock);
    if (instance->state == STATE_EMPTY || instance->state == STATE_PREPARED || instance->state == STATE_PREPARE_SENT) {
        if (msg->rand > instance->max_rand) {

            memcpy(instance->addr, msg->addr, 20);
            instance->max_rand = msg->rand;
            instance->max_member_idx = msg->owner_idx;
            //Prepared for a proposal with higher ballot.
            //If the current node is electing for this instance,
            //it should have failed.
            //Notify the electing thread with the news.

            instance->state = STATE_PREPARED;
            pthread_cond_broadcast(&instance->cond);



            Message resp;
            resp.rand = msg->rand;
            resp.blockNum = msg->blockNum;
            resp.message_type = ELEC_PREPARED;
            resp.owner_idx = msg->owner_idx;
            memcpy(resp.addr, term->my_account, 20);
            char *output = serialize(&resp);
            debug_print("Sending PrepareED to [%s], rand = %lu\n", msg->addr, msg->rand);
            if (sendto(socket, output, MSG_LEN, 0, (struct sockaddr *)si_other, si_len) == -1) {
                perror("Failed to send resp");
            }
            free(output);
        }
    }
    if (instance->state == STATE_ELECTED){
        Message resp;
        resp.rand = instance->max_rand;
        resp.message_type = ELEC_NOTIFY;
        resp.owner_idx = term->my_idx;
        memcpy(resp.addr, term->my_account, 20);
        char *output = serialize(&resp);
        debug_print("Sending Notify to [%s], rand = %lu\n", msg->addr, msg->rand);
        if (sendto(socket, output, MSG_LEN, 0, (struct sockaddr *)si_other, si_len) == -1) {
            perror("Failed to send resp");
        }
        free(output);
    }

    pthread_mutex_unlock(&instance->state_lock);
}


void handle_prepared(const Message *msg, const struct sockaddr_in *si_other, Term_t *term) {
    debug_print("Received PrepareD message, blk = %ld\n", msg->blockNum);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_mutex_lock(&instance->state_lock);
    if (instance->state == STATE_PREPARE_SENT) {
        int ret = insert_addr(instance->prepared_addr, msg->addr, &instance->prepared_addr_count);
        debug_print("prepared count = %d\n", instance->prepared_addr_count);
        if (ret != 0) {
            pthread_mutex_unlock(&instance->state_lock);
            return;
        }
        if (instance->prepared_addr_count > term->member_count / 2) {
            instance->confirmed_addr[0] = term->my_account;
            instance->confirmed_addr_count = 1;
            Message resp;
            memcpy(resp.addr, term->my_account, 20);
            resp.message_type = ELEC_CONFIRM;
            resp.blockNum = msg->blockNum;
            resp.rand = msg->rand;
            resp.owner_idx = term->my_idx;
            ret = broadcast(&resp, term);
            if (ret != 0) {
                perror("failed to broadcast Confirm message");
            }
        }
        //The node is still potentially ``in-control'', No need to notify.
        instance->state = STATE_CONFIRM_SENT;
    }
    pthread_mutex_unlock(&instance->state_lock);
}


void handle_confirm(const Message *msg, const struct sockaddr_in *si_other, Term_t *term, socklen_t si_len) {
    debug_print("Received Confirm Msg, blk = %lu\n", msg->blockNum);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_mutex_lock(&instance->state_lock);
    if (instance-> max_rand > msg->rand){
        debug_print("Already prepared to larger rand, Not answering confirm, blk =%lu\n", msg->blockNum);
        pthread_mutex_unlock(&instance->state_lock);
        //already prepared a higher.
        return;
    }
    if (instance-> state == STATE_CONFIRM_SENT || instance ->state == STATE_CONFIRMED) {
        pthread_mutex_unlock(&instance->state_lock);
        return;
    }
    Message resp;
    memcpy(resp.addr, term->my_account, 20);
    resp.rand = msg->rand;
    resp.blockNum = msg->blockNum;
    resp.message_type = ELEC_CONFIRMED;
    resp.owner_idx = msg->owner_idx;
    char *output = serialize(&resp);
    if (sendto(socket, output, MSG_LEN, 0, (struct sockaddr *)si_other, si_len) == -1){
        perror("Failed to send resp");
    }

    /*
     * Same as before, answering prepared message, should have failed.
     */
    instance->state = STATE_CONFIRMED;
    pthread_cond_broadcast(&instance->cond);

    pthread_mutex_unlock(&instance->state_lock);
    return;
}

void handle_confirmed(const Message *msg, const struct sockaddr_in *si_other, Term_t *term) {
    debug_print("Received ConfirmED Msg, blk = %lu\n", msg->blockNum);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    int socket = term->sock;
    pthread_mutex_lock(&instance->state_lock);
    if (instance->state == STATE_CONFIRM_SENT) {
        int ret = insert_addr(instance->confirmed_addr, msg->addr, &instance->confirmed_addr_count);
        if (ret != 0) {
            pthread_mutex_unlock(&instance->state_lock);
            return;
        }
        if (instance->confirmed_addr_count > term->member_count / 2) {
            Message resp;
            memcpy(resp.addr, term->my_account, 20);
            resp.message_type = ELEC_ANNOUNCE;
            resp.blockNum =  msg->blockNum;
            resp.rand = msg->rand;
            resp.owner_idx = msg->owner_idx;
            ret = broadcast(&resp, term);
            if (ret != 0){
                perror("failed to broadcast Confirm message");
            }
            instance->state = STATE_ELECTED;
            pthread_cond_broadcast(&instance->cond);

            }
    }
    pthread_mutex_unlock(&instance->state_lock);
}

void handle_notify(const Message *msg, const struct sockaddr_in *si_other, Term_t *term){
    debug_print("Received Notify Msg, blk = %lu\n", msg->blockNum);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    pthread_mutex_lock(&instance->state_lock);
    instance->max_rand = msg->rand;
    instance->max_member_idx = msg->owner_idx;

    instance->state = STATE_CONFIRMED;

    pthread_cond_broadcast(&instance->cond);
    pthread_mutex_unlock(&instance->state_lock);
}

void handle_announce(const Message *msg, const struct sockaddr_in *si_other, Term_t *term){
    fprintf(stderr, "Leader elected for block %lu, leader = %s\n", msg->blockNum, msg->addr);
    uint64_t offset = msg->blockNum - term->start_block;
    instance_t *instance = &term->instances[offset];
    pthread_mutex_lock(&instance->state_lock);
    instance->max_rand = msg->rand;
    instance->max_member_idx = msg->owner_idx;

    instance->state = STATE_CONFIRMED;

    pthread_cond_broadcast(&instance->cond);
    pthread_mutex_unlock(&instance->state_lock);
}



static void *RecvFunc(void *opaque){

    Term_t *term = (Term_t *)opaque;
    int s = term->sock;


    /*
     * debug
     */
    struct sockaddr_in foo;
    int len = sizeof(struct sockaddr_in);
    getsockname(s,  (struct sockaddr *) &foo, &len);
    fprintf(stderr, "Thread Receving network packets, listening on %s:%d\n",inet_ntoa(foo.sin_addr),
            ntohs(foo.sin_port) );



    char buffer[1024];
    int recv_len;
    struct sockaddr_in si_other;
    socklen_t si_len = sizeof(si_other);
    while(1){
        recv_len = recvfrom(s, buffer, BUFLEN, 0, (struct sockaddr *)&si_other, &si_len);
        if (recv_len != MSG_LEN){
            fprintf(stderr, "Wrong Message Format\n");
        }
        Message msg = deserialize(buffer);
        switch(msg.message_type){
            case ELEC_PREPARE :
                handle_prepare(&msg, &si_other, term, si_len);
                break;
            case ELEC_PREPARED :
                handle_prepared(&msg, &si_other, term);
                break;
            case ELEC_CONFIRM :
                handle_confirm(&msg, &si_other, term, si_len);
                break;
            case ELEC_CONFIRMED :
                handle_confirmed(&msg, &si_other, term);
                break;
            case ELEC_ANNOUNCE :
                handle_announce(&msg, &si_other, term);
                break;
            case ELEC_NOTIFY:
                handle_notify(&msg, &si_other, term);
                break;

        }

    }

}

int elect(Term_t *term, uint64_t blk, uint64_t *value){
    info_print("Electing Block %lu\n", blk);
    uint64_t offset = blk - term->start_block;
    instance_t *instance = &term->instances[offset];
    if (instance->state == STATE_CONFIRMED || instance->state == STATE_PREPARE_SENT || instance->state == STATE_CONFIRM_SENT){
        info_print("[Election] for block %lu failed\n", blk);
        return 0;
    }


    uint64_t r = (uint64_t)rand();
    pthread_mutex_lock(&instance->state_lock);
    if (r > instance->max_rand) {
        instance->max_rand = r;
        instance->prepared_addr[0] = term->my_account;
        instance->prepared_addr_count = 1;


        Message msg;
        msg.blockNum = blk;
        msg.message_type = ELEC_PREPARE;
        msg.rand = r;
        msg.owner_idx = term->my_idx;
        memcpy(msg.addr, term->my_account, 20);
        char *out = serialize(&msg);
        broadcast(&msg, term);
        instance->state = STATE_PREPARE_SENT;

        pthread_cond_wait(&instance->cond, &instance->state_lock);

        pthread_mutex_unlock(&instance->state_lock);

        if (instance->state == STATE_ELECTED) {
            info_print("[Election] as leader for block %lu\n", blk);
            return 1;
        }
    }
    pthread_mutex_unlock(&instance->state_lock);
    info_print("[Election] for block %lu failed\n", blk);
    return 0;

}
//helper functions.

static char* serialize(const Message *msg){
    char *output = malloc(MSG_LEN * sizeof(char));
    memcpy(&output[0], &msg->rand, 8);
    memcpy(&output[8], &msg->blockNum, 8);
    memcpy(&output[16], &msg->message_type, 1);
    memcpy(&output[17], &msg->owner_idx, 4);
    memcpy(&output[21], msg->addr, 20);
    return output;
}

static Message deserialize(char *input){
    Message msg;
    memcpy(&msg.rand, &input[0], 8);
    memcpy(&msg.blockNum, &input[8], 8);
    memcpy(&msg.message_type, &input[16], 1);
    memcpy(&msg.owner_idx, &input[17], 4);
    memcpy(&msg.addr, &input[21], 20);
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
            fprintf(stderr, "Failed to broadcast message\n");
            return -1;
        }
    }
    return 0;
}

static void init_instance(Term_t *term, instance_t *instance){
    pthread_cond_init(&instance->cond, NULL);
    pthread_mutex_init(&instance->state_lock, NULL);

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

static int send_to_member(int index, const Message* msg, Term_t *term){
    char *buf = (char *) malloc(MSG_LEN * sizeof(char));
    int ret = sendto(term->sock, buf, MSG_LEN, 0, (struct sockaddr*)&term->members[index], sizeof(struct sockaddr_in));
    if (ret != 0){
        perror("Failed to send to member");
    }
    free(buf);
    return ret;
}
