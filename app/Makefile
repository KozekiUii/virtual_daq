CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g
TARGET  := reader test
SRCS    := reader.c test.c

.PHONY: all clean run

all: $(TARGET)

reader: reader.c
	$(CC) $(CFLAGS) -o $@ $<

test: test.c
	$(CC) $(CFLAGS) -o $@ $<
	
run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)