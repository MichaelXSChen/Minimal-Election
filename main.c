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
    sleep(10);
    Term_t * term = New_Node(offset);
    if (offset == 1){
        int i;
        for (i=0; i<1000; i++){
            uint64_t value;
            elect(term, (uint64_t)i, &value);
        }
    }

    return 0;
}