CC = gcc
CFLAGS = -Wall -Werror -Wextra -Wpedantic
TARGET_SRV = out/l4lb_server
TARGET_CLI = out/l4lb_client
SRCS_SRV = src/server/server.c
SRCS_CLI = src/client/client.c
OBJS_SRV = $(SRCS_SRV:src/%.c=out/%.o)
OBJS_CLI = $(SRCS_CLI:src/%.c=out/%.o)

.PHONY: all clean

all: $(TARGET_SRV) $(TARGET_CLI)

$(TARGET_SRV): $(OBJS_SRV)
	$(CC) $(CFLAGS) -o $@ $(OBJS_SRV)

$(TARGET_CLI): $(OBJS_CLI)
	$(CC) $(CFLAGS) -o $@ $(OBJS_CLI)

out/%.o: src/%.c src/common.h | out
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

out:
	mkdir -p $@

clean:
	rm -f $(OBJS_CLI) $(OBJS_SRV) $(TARGET_SRV) $(TARGET_CLI)
