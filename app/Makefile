CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -I../include
TARGET  := reader test vdaq_ctl
SRCS    := reader.c test.c vdaq_ctl.c


.PHONY: all clean run

all: $(TARGET)

reader: reader.c
	$(CC) $(CFLAGS) -o $@ $<

test: test.c
	$(CC) $(CFLAGS) -o $@ $<

vdaq_ctl: vdaq_ctl.c
	$(CC) $(CFLAGS) -o $@ $<
	
run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)