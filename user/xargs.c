#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    char *argLine[MAXARG];
    char ch;
    int arg_offset;

    for (int i = 0; i < argc; ++i)
    {
        // printf("original argv: %s\n", argv[i+1]);

        argLine[i] = argv[i + 1];
        // copy the content
    }

    int flag;
    do
    {
        arg_offset = argc - 1;
        char buf[512];
        int buf_pt = 0;
        
        while ((flag = read(0, &ch, 1)) != 0)
        {
            if (ch == '\n')
            {
                break;
            }
            buf[buf_pt++] = ch;
        }
        
        if (flag == 0) {
            break;
        }
        // finish read line

        // buf[buf_pt] = '\0';
        // printf("buf: %s\n", buf);
        // break;

        buf[buf_pt] = ' ';
        char *end_point = buf + buf_pt;
        // place ' '

        char *start = buf;
        char *p = buf;

        while (p < end_point)
        {
            while (p < end_point && *p != ' ')
            {
                ++p;
                // find the first ' '
            }
            *p++ = '\0';
            argLine[arg_offset++] = start;
            // printf("add str: %s\n", start);
            while (p < end_point && *p == ' ')
            {
                ++p;
                // find the first not ' ' character
            }
            start = p;
        }
        argLine[arg_offset] = 0;
        // finish the argv array

//        printf("args_offset: %d\n", arg_offset);
        // for (int st = 0; st < arg_offset; ++st) {
        //     printf("argv content: %s\n", argLine[st]);
        // }

        if (fork() == 0) {
            // printf("go here\n");

            exec(argLine[0], argLine);
            // child process, execute process
        }
        wait(0);
    } while(flag != 0);

    exit(0);
}