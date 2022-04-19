#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    int r, fd1 = -1, fd2 = -1;
    fd1 = open("./testsymlink/a", O_CREAT | O_RDWR);
  r = symlink("./testsymlink/a", "./testsymlink/b");
}