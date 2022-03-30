#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main (int argc, char* argv[]) {
    int tickNum;
    if (argc != 2) {
        fprintf(2, "pass an argument\n");
        exit(1);
    }
    tickNum = atoi(argv[1]);
    sleep(tickNum);
    exit(0);
}

