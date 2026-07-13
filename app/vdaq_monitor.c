#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vdaq_uapi.h"

#define VDAQ_DEVICE_PATH "/dev/vdaq0"
#define POLL_TIMEOUT_MS  3000

int main(void)
{
    struct pollfd pfd;
    struct vdaq_sample sample;
    ssize_t read_size;
    int fd;
    int ret;

    fd = open(VDAQ_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/vdaq0");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (1) {
        printf("waiting for vdaq data...\n");
        // poll() 本身不会直接读取数据，它只是询问：这个设备现在能不能读？
        ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (ret < 0) {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        if (ret == 0) {
            printf("poll timeout: no data within %d ms\n",
                   POLL_TIMEOUT_MS);
            continue;
        }

        if (pfd.revents & POLLIN) {
            read_size = read(fd, &sample, sizeof(sample));

            if (read_size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("data was consumed, waiting again...\n");
                    continue;
                }

                perror("read");
                break;
            }

            if (read_size != sizeof(sample)) {
                fprintf(stderr,
                        "unexpected read size: %zd, expected: %zu\n",
                        read_size, sizeof(sample));
                break;
            }

            printf("timestamp = %llu ns\n",
                   (unsigned long long)sample.timestamp_ns);
            printf("sequence  = %u\n", sample.sequence);
            printf("ch0       = %d\n", sample.channel[0]);
            printf("ch1       = %d\n", sample.channel[1]);
            printf("ch2       = %d\n", sample.channel[2]);
            printf("ch3       = %d\n", sample.channel[3]);
            printf("status    = %u\n\n", sample.status);
        }

        if (pfd.revents & POLLHUP) {
          printf("device stopped and no buffered data remains\n");
          break;
        }

        if (pfd.revents & POLLERR) {
          fprintf(stderr, "device reported POLLERR\n");
          break;
        }

        if (pfd.revents & POLLNVAL) {
          fprintf(stderr, "invalid file descriptor\n");
          break;
        }

        pfd.revents = 0;
    }

    close(fd);
    return 0;
}