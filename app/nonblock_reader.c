#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "vdaq_uapi.h"

int main(void)
{
    struct vdaq_sample sample;
    ssize_t ret;
    int fd;

    fd = open("/dev/vdaq0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    ret = read(fd, &sample, sizeof(sample));
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            printf("no data available\n");
        else
            perror("read");
    } else {
        printf("sequence=%u\n", sample.sequence);
    }

    close(fd);
    return 0;
}