#include "vdaq_uapi.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s <command>\n", argv[0]);
    printf("Commands:\n");
    printf("  get_status - Get device status\n");
    printf("  stop - Stop device\n");
    printf("  start - Start device\n");
    printf("  clear - Clear buffer\n");
    printf("  rate - Set sample rate\n");
    return 1;
  }

  int fd = open("/dev/vdaq0", O_RDWR);
  if (fd < 0) {
    perror("Failed to open /dev/vdaq0");
    return 1;
  }

  if (strcmp(argv[1], "get_status") == 0) {
    struct vdaq_stats stats;
    if (ioctl(fd, VDAQ_IOCTL_GET_STATUS, &stats) < 0) {
      perror("Failed to get stats");
      close(fd);
      return 1;
    }
    printf("Generated Samples: %lu\n", stats.generated_samples);
    printf("Read Samples: %lu\n", stats.read_samples);
    printf("Dropped Samples: %lu\n", stats.dropped_samples);
    printf("Buffer Overflows: %lu\n", stats.buffer_overflows);
    printf("Current Sequence: %u\n", stats.current_sequence);
    printf("Buffer Head: %u\n", stats.buffer_head);
    printf("Buffer Tail: %u\n", stats.buffer_tail);
    printf("Rate: %u\n",stats.rate);
    printf("Running: %u\n", stats.running);
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
  } else if (strcmp(argv[1], "clear") == 0) {
    if (ioctl(fd, VDAQ_IOCTL_CLEAR_BUFFER) < 0) {
      perror("Failed to clear buffer");
      close(fd);
      return 1;
    }
    printf("Buffer cleared successfully\n");
  } else if (strcmp(argv[1], "rate") == 0) {
    if (argc < 3) {
      printf("Usage: %s rate <sample_rate>\n", argv[0]);
      close(fd);
      return 1;
    }
    unsigned int sample_rate = atoi(argv[2]);
    if (ioctl(fd, VDAQ_IOCTL_SET_RATE, &sample_rate) < 0) {
      perror("Failed to set sample rate");
      close(fd);
      return 1;
    }
    printf("Sample rate set to %u\n", sample_rate);
  } else {
    printf("Unknown command: %s\n", argv[1]);
  }

  close(fd);
  return 0;
}