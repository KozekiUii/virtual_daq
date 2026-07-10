#include "vdaq_uapi.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s <command>\n", argv[0]);
    printf("Commands:\n");
    printf("  get_stats - Get device statistics\n");
    printf("  reset_stats - Reset device statistics\n");
    printf("  stop - Stop device\n");
    printf("  start - Start device\n");
    return 1;
  }

  int fd = open("/dev/vdaq0", O_RDWR);
  if (fd < 0) {
    perror("Failed to open /dev/vdaq0");
    return 1;
  }

  if (strcmp(argv[1], "get_stats") == 0) {
    struct vdaq_stats stats;
    if (ioctl(fd, VDAQ_IOCTL_GET_STATS, &stats) < 0) {
      perror("Failed to get stats");
      close(fd);
      return 1;
    }
    printf("Generated Samples: %llu\n", stats.generated_samples);
    printf("Read Samples: %llu\n", stats.read_samples);
    printf("Dropped Samples: %llu\n", stats.dropped_samples);
    printf("Buffer Overflows: %llu\n", stats.buffer_overflows);
    printf("Current Sequence: %u\n", stats.current_sequence);
    printf("Buffer Head: %u\n", stats.buffer_head);
    printf("Buffer Tail: %u\n", stats.buffer_tail);
  } else if (strcmp(argv[1], "stop") == 0) {
    if (ioctl(fd, VDAQ_IOCTL_STOP) < 0) {
      perror("Failed to stop device");
      close(fd);
      return 1;
    }
    printf("Device stopped successfully\n");
  } else if (strcmp(argv[1], "start") == 0) {
    if (ioctl(fd, VDAQ_IOCTL_START) < 0) {
      perror("Failed to start device");
      close(fd);
      return 1;
    }
    printf("Device started successfully\n");
  } else {
    printf("Unknown command: %s\n", argv[1]);
  }

  close(fd);
  return 0;
}