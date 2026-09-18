CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
TARGET = dino
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean