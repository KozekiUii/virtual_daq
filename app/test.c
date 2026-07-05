
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/vdaq0", O_RDONLY);

    printf("opened, pid = %d\n", getpid());
    sleep(30);

    return 0;
}