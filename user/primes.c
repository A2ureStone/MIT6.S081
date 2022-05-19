#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int mainread[2];
    int p[2];
    pipe(p);
    mainread[1] = p[1];
    mainread[0] = p[0];
    for (int i = 2; i < 36; i++)
    {
        char *num = (char *)&i;
        write(p[1], num, 4);
        // initialize pipe, write 2 to 35
    }

    int prime;
    while (1)
    {
        close(mainread[1]);
        // close write end
        if (read(mainread[0], &prime, 4) > 0)
        {
            pipe(p);
            if (fork() == 0)
            {
                int num;
                printf("prime %d\n", prime);
                while (read(mainread[0], &num, 4) > 0) {
                    if (num % prime != 0) {
                        write(p[1], &num, 4);
                        // if not divive, write to new pipe
                    }
                }
                close(p[0]);
                close(p[1]);
                close(mainread[0]);
                exit(0);
            }
            close(mainread[0]);
            // close main process pipeline
            mainread[1] = p[1];
            mainread[0] = p[0];
            // change mainread to the new open pipe
        } else {
            close(mainread[0]);
            break;
        }
    }
    while (wait(0) != -1) {
        ;
        // make sure all child exit
    }
    exit(0);
}