#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *
getName(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    *(buf + strlen(p)) = 0; 
    return buf;
}

int find(char *path, char *name)
{
    int fd;
    struct stat st;
    struct dirent de;

    if ((fd = open(path, 0)) < 0)
    {
        // printf("cannot open %s\n", path);
        return 0;
    }

    if ((fstat(fd, &st)) < 0)
    {
        printf("cannot stat %s\n", path);
        close(fd);
        return 0;
    }

    // printf("enter: %s\n", path);
    // printf("getName: %s\n", getName(path));
    if (!strcmp(getName(path), name))
    {
        printf("%s\n", path);
    }

    if (st.type == T_DIR)
    {
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if(de.inum == 0)
                continue;
            // printf("direct content: %s\n", de.name);
            if (strcmp(de.name, ".") && strcmp(de.name, ".."))
            {
                // printf("direct content: %s\n", de.name);
                char buf[512];
                char *p;

                if (strlen(path) + 1 + strlen(de.name) + 1 > 512)
                {
                    printf("buf size too small\n");
                    return 0;
                }

                memmove(buf, path, strlen(path));
                p = buf + strlen(path);
                *p = '/';
                ++p;
                memmove(p, de.name, strlen(de.name));
                p += strlen(de.name);
                *p = 0;
                // modify path
                // printf("origin: %s\n", de.name);
                // printf("go to: %s\n", buf);
                find(buf, name);
            }
        }
    }

    close(fd);
    return 0; 
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("argument error\n");
    }
    find(argv[1], argv[2]);
    exit(0);
}