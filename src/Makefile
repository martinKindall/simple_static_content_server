CC = gcc

TARGET = server

SRCS = server.c utils.c
HDRS = utils.h

# Compilation Flags
# -std=c23: The latest C standard
# -Wall -Wextra: Enable common warning flags
# -g: Include debug information 
# -fsanitize=address,undefined: Enable ASan and UBSan 
CFLAGS = -std=gnu23 -Wall -Wextra -g -fsanitize=address,undefined

LDFLAGS = -fsanitize=address,undefined

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

.PHONY: all clean

clean:
	rm -f $(TARGET)

