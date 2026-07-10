#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "vdaq_uapi.h"


int main() {
  struct vdaq_sample sample;

  int fd = open("/dev/vdaq0", O_RDONLY);
  if (fd < 0) {
    printf("open is failed");
    return EOF;
  }
  int ret = read(fd, &sample, sizeof(sample));
  if (ret != sizeof(sample)) {
    printf("read is failed");
    return EOF;
  }

  printf("sequence = %u\n", sample.sequence);
  printf("ch0 = %d\n", sample.channel[0]);
  printf("ch1 = %d\n", sample.channel[1]);
  printf("ch2 = %d\n", sample.channel[2]);
  printf("ch3 = %d\n", sample.channel[3]);
  printf("status = %u\n", sample.status);
  if (close(fd) < 0) {
    printf("close is failed\n");
    return EOF;
  }
  return 0;
}