#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "vdaq_uapi.h"

#define VDAQ_DEVICE_PATH "/dev/vdaq0"
#define MAX_EVENTS       4
#define EPOLL_TIMEOUT_MS 3000

static int handle_vdaq_event(int fd)
{
    struct vdaq_sample sample;
    ssize_t ret;

    ret = read(fd, &sample, sizeof(sample));
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;

        perror("read vdaq");
        return -1;
    }

    if (ret != sizeof(sample)) {
        fprintf(stderr,
                "unexpected read size: %zd, expected: %zu\n",
                ret, sizeof(sample));
        return -1;
    }

    printf("sequence=%u timestamp=%llu "
           "ch=[%d, %d, %d, %d] status=%u\n",
           sample.sequence,
           (unsigned long long)sample.timestamp_ns,
           sample.channel[0],
           sample.channel[1],
           sample.channel[2],
           sample.channel[3],
           sample.status);

    return 0;
}

int main(void)
{
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];
    int device_fd;
    int epoll_fd;
    int event_count;
    int i;
    int running = 1;

    /*
     * epoll通知与真正read之间存在竞态窗口，
     * 因此设备采用非阻塞方式打开。
     */
    device_fd = open(VDAQ_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (device_fd < 0) {
        perror("open /dev/vdaq0");
        return 1;
    }

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(device_fd);
        return 1;
    }

    /*
     * 注册vDAQ设备。
     * 当前采用水平触发LT，不添加EPOLLET。
     * 只要缓冲区仍有数据，epoll就会持续通知。
     */
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    event.data.fd = device_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  device_fd, &event) < 0) {
        perror("epoll_ctl device");
        close(epoll_fd);
        close(device_fd);
        return 1;
    }

    /*
     * 同时监听标准输入。
     * 用户输入q即可退出程序。
     */
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                  STDIN_FILENO, &event) < 0) {
        perror("epoll_ctl stdin");
        close(epoll_fd);
        close(device_fd);
        return 1;
    }

    printf("monitoring %s, enter q to quit\n",
           VDAQ_DEVICE_PATH);

    while (running) {
        event_count = epoll_wait(epoll_fd,
                                 events,
                                 MAX_EVENTS,
                                 EPOLL_TIMEOUT_MS);

        if (event_count < 0) {
            if (errno == EINTR)
                continue;

            perror("epoll_wait");
            break;
        }

        if (event_count == 0) {
            printf("epoll timeout: no event within %d ms\n",
                   EPOLL_TIMEOUT_MS);
            continue;
        }

        for (i = 0; i < event_count; ++i) {
            uint32_t revents = events[i].events;
            int fd = events[i].data.fd;

            if (fd == device_fd) {
                /*
                 * 先处理可读数据，再处理HUP。
                 * 设备停止时缓冲区内可能仍有旧数据。
                 */
                if (revents & EPOLLIN) {
                    if (handle_vdaq_event(device_fd) < 0) {
                        running = 0;
                        break;
                    }
                }

                if (revents & EPOLLHUP) {
                    printf("vdaq stopped and no buffered data remains\n");
                    running = 0;
                    break;
                }

                if (revents & EPOLLERR) {
                    fprintf(stderr, "vdaq reported EPOLLERR\n");
                    running = 0;
                    break;
                }
            } else if (fd == STDIN_FILENO) {
                char input[32];
                ssize_t len;

                len = read(STDIN_FILENO,
                           input,
                           sizeof(input) - 1);
                if (len < 0) {
                    perror("read stdin");
                    running = 0;
                    break;
                }

                input[len] = '\0';

                if (input[0] == 'q' || input[0] == 'Q')
                    running = 0;
            }
        }
    }

    close(epoll_fd);
    close(device_fd);

    return 0;
}