CC = gcc
CFLAGS = -Wall -Werror -Wextra -Wpedantic
TARGET = l4lb
SRCS = src/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(TARGET)
