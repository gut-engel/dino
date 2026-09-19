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

# Extension packaging — version/name come from vscode-dino/package.json
VSCE_DIR = vscode-dino
VSIX_NAME := $(shell sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' $(VSCE_DIR)/package.json | head -n1)
VSIX_VERSION := $(shell sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' $(VSCE_DIR)/package.json | head -n1)
VSIX := $(VSIX_NAME)-$(VSIX_VERSION).vsix

VSIX_SRC = \
	$(VSCE_DIR)/package.json \
	$(VSCE_DIR)/language-configuration.json \
	$(VSCE_DIR)/README.md \
	$(VSCE_DIR)/CHANGELOG.md \
	$(VSCE_DIR)/syntaxes/dino.tmLanguage.json \
	$(VSCE_DIR)/snippets/dino.code-snippets \
	$(VSCE_DIR)/vsix/extension.vsixmanifest \
	$(VSCE_DIR)/vsix/content-types.xml \
	$(VSCE_DIR)/vsix/build_vsix.py

all: $(TARGET) $(VSIX)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) -o $(TARGET) $(SRC)

$(VSIX): $(VSIX_SRC)
	python3 $(VSCE_DIR)/vsix/build_vsix.py

clean:
	rm -f $(TARGET)
	rm -rf CCode
	rm -f $(VSIX)

.PHONY: all clean