#include <stdio.h>
#include <stdlib.h>
#include "Node.h"
#include <unistd.h>

int main(int argc, char**argv) {
    if (argc != 2){
        printf("Usage: thw [offset]");
        return 1;
    }
    int offset = atoi(argv[1]);

    Term_t * term = New_Node(offset);


    sleep(10);
    int i;
    for (i=10; i<500; i++){
        uint64_t value;
        elect(term, (uint64_t)i, &value);

    }

    pthread_join(term->recvt, NULL);

    return 0;
}