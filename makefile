CC = gcc

CFLAGS = -g -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SERVER = $(BIN_DIR)/server
CLIENT = $(BIN_DIR)/client

# -------------------------
# Source files
# -------------------------

SERVER_SRCS = $(wildcard $(SRC_DIR)/server/*.c) $(wildcard $(SRC_DIR)/server/lib/*.c) 
CLIENT_SRCS = $(wildcard $(SRC_DIR)/client/*.c)
SHARED_SRCS = $(wildcard $(SRC_DIR)/shared/*.c)

# -------------------------
# Object files
# -------------------------

SERVER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLIENT_SRCS))
SHARED_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SHARED_SRCS))

# -------------------------
# Targets
# -------------------------

.PHONY: all server client clean

all: $(SERVER) $(CLIENT)

server: $(SERVER)

client: $(CLIENT)

# -------------------------
# Linking
# -------------------------

$(SERVER): $(SERVER_OBJS) $(SHARED_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(CLIENT): $(CLIENT_OBJS) $(SHARED_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

# -------------------------
# Compilation
# -------------------------

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------
# Clean
# -------------------------

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
