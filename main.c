#include <stdio.h>
#include <stdlib.h>
#include "Node.h"
#include <unistd.h>
#include <sys/time.h>
int main(int argc, char**argv) {
    if (argc != 2){
        printf("Usage: thw [offset]");
        return 1;
    }
    int offset = atoi(argv[1]);

    Term_t * term = New_Node(offset);
    struct timeval time;
    gettimeofday(&time,NULL);

    srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
    sleep(10-offset);
    int i;
    for (i=10; i<9999; i++){
        uint64_t value;
        elect(term, (uint64_t)i, &value);
        usleep(rand() % 10000);
    }

    pthread_join(term->recvt, NULL);

    return 0;
}