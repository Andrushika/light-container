CC       := gcc
CFLAGS   := -Wextra -Wall -pedantic -std=c11

SRC_DIR   := src
BUILD_DIR := build
LIB_LOG   := lib/log

SRC_FILES  := $(wildcard $(SRC_DIR)/*.c)
OBJECTS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
OBJECTS    += $(BUILD_DIR)/log.o

LIB_LOG_SRC := $(LIB_LOG)/log.c
LIB_LOG_FLAGS := -DLOG_USE_COLOR

EXECUTABLE := app

INCLUDES := -I$(LIB_LOG)

.PHONY: all clean

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/log.o: $(LIB_LOG_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIB_LOG_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(EXECUTABLE)