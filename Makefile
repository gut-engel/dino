CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
TARGET = dino
SRC = \
	src/main.c \
	src/common.c \
	src/lexer/token.c \
	src/lexer/lexer.c \
	src/ast/ast.c \
	src/parser/parser.c \
	src/codegen/codegen.c
INC = -Isrc

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
	rm -rf CCode

.PHONY: all clean