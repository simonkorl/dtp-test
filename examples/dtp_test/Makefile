OS := $(shell uname)

ROOT_DIR = ../..
SOURCE_DIR = $(ROOT_DIR)/src
AITRANS_DIR = $(SOURCE_DIR)/aitrans
AITRANS_LIB_DIR = $(SOURCE_DIR)/aitrans
BUILD_DIR = $(CURDIR)/build
LIB_DIR = $(BUILD_DIR)/release
INCLUDE_DIR = $(ROOT_DIR)/include

INCS = -I$(INCLUDE_DIR)
CFLAGS = -I. -Wall -pedantic -fsanitize=address -O1 #-g

ifeq ($(OS), Darwin)
CFLAGS += -framework Security
endif

LIBCRYPTO_DIR = $(dir $(shell find ${BUILD_DIR} -name libcrypto.a))
LIBSSL_DIR = $(dir $(shell find ${BUILD_DIR} -name libssl.a))

LDFLAGS = -L$(LIBCRYPTO_DIR) -L$(LIBSSL_DIR) -L$(LIB_DIR) -L../src/aitrans/libtorch/lib

LIBS = $(LIB_DIR)/libquiche.a -lev -ldl -pthread -lm
CPPFLAGS =  -lstdc++

PORT = 5557

all: server client

all_test: server_test client_test

server_test: server_test.c $(INCLUDE_DIR)/quiche.h $(LIB_DIR)/libquiche.a dtp_config.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(INCS) $(LIBS) $(CPPFLAGS)

client_test: client_test.c $(INCLUDE_DIR)/quiche.h $(LIB_DIR)/libquiche.a dtp_config.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(INCS) $(LIBS) $(CPPFLAGS)

server: server.c $(INCLUDE_DIR)/quiche.h $(LIB_DIR)/libquiche.a dtp_config.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(INCS) $(LIBS) $(CPPFLAGS)

client: client.c $(INCLUDE_DIR)/quiche.h $(LIB_DIR)/libquiche.a dtp_config.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(INCS) $(LIBS) $(CPPFLAGS)

$(LIB_DIR)/libquiche.a: $(shell find $(SOURCE_DIR) -type f -name '*.rs') $(BUILD_DIR)
	cd .. && cargo build --release --target-dir $(BUILD_DIR)

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

test: server client
	./server 127.0.0.1 $(PORT) trace/block_trace/aitrans_block.txt &> server_err.log & \
	./client 127.0.0.1 $(PORT) &> client_err.log & wait

kill:
	@kill `lsof -i:5557 | awk '/server/ {print$$2}'`
    # @kill `lsof -i:5557 | awk '/client/ {print$$2}'`

clean:
	@$(RM) -rf server client build/

.PHONY: clean
