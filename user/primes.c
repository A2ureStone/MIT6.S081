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
    for (int i = 2; i < 130; i++)
    {
        char *num = (char *)&i;
        write(p[1], num, 4);
        // initialize pipe, write 2 to 35
    }

    int prime;
    int num;
    close(mainread[1]);
    while (1) {

        // read from the pipe success, else just return
        if (read(mainread[0], &prime, 4) == 0) {
            exit(0);
        }

        printf("prime %d\n", prime);
        pipe(p);
        if (fork() == 0) {
            // do nothing except for change the mainread, in the next round,
            // child process become parent process
            close(mainread[0]);
            mainread[0] = p[0];
            close(p[1]);
        } else {
            // close read side of the new pipe
            close(p[0]);
            // get and write to the pipe
            ;
            while (read(mainread[0], &num, 4) > 0) {
                if (num % prime != 0) {
                    write(p[1], &num, 4);
                }
            }
            close(p[1]);
            close(mainread[0]);

            // wait for the child
            while(wait(0) != -1) {
                ;
            }
            exit(0);
            // finish write, wait for child
        }
    }
}