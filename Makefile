CC       := gcc
CFLAGS   := -Wextra -Wall -pedantic -std=c11

SRC_DIR   := src
BUILD_DIR := build
LIB_LOG   := lib/log
LIB_ARGTABLE := lib/argtable
INCLUDES_DIR := include

SRC_FILES  := $(wildcard $(SRC_DIR)/*.c)
OBJECTS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
OBJECTS    += $(BUILD_DIR)/log.o $(BUILD_DIR)/argtable3.o

LIB_LOG_SRC := $(LIB_LOG)/log.c
LIB_LOG_FLAGS := -DLOG_USE_COLOR

LIB_ARGTABLE_SRC := $(LIB_ARGTABLE)/argtable3.c

EXECUTABLE := light-container

INCLUDES := -I$(SRC_DIR) -I$(LIB_LOG) -I$(LIB_ARGTABLE) -I$(INCLUDES_DIR)

.PHONY: all clean

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/log.o: $(LIB_LOG_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIB_LOG) $(LIB_LOG_FLAGS) -c $< -o $@

$(BUILD_DIR)/argtable3.o: $(LIB_ARGTABLE_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIB_ARGTABLE) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(EXECUTABLE)