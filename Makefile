CC = gcc
CFLAGS = -Wall -Werror -Wextra -Wpedantic
TARGET_SRV = out/l4lb_server
SRCS = src/main.c src/server.c
OBJS = $(SRCS:src/%.c=out/%.o)

.PHONY: all clean

all: $(TARGET_SRV)

$(TARGET_SRV): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

out/%.o: src/%.c src/common.h | out
	$(CC) $(CFLAGS) -c $< -o $@

out:
	mkdir -p $@

clean:
	rm -f $(OBJS) $(TARGET_SRV)
