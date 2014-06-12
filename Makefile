SRC_DIR := src# Source code files
OBJ_DIR := build# Build files
STATIC_PROTOBUF_C := 0# Link static protobuf-c
# Libiirc version
LIBIIRC_MAJOR_VER := 1
LIBIIRC_MINOR_VER := 0.1
################################################################################

DAEMON_DIR := $(SRC_DIR)/daemon
DAEMON_SRC := $(wildcard $(DAEMON_DIR)/*.c)
DAEMON_OBJ := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(DAEMON_SRC))

ATTACH_DIR := $(SRC_DIR)/attach
ATTACH_SRC := $(wildcard $(ATTACH_DIR)/*.c)
ATTACH_OBJ := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(ATTACH_SRC))

CLIENT_DIR := $(SRC_DIR)/client
CLIENT_SRC := $(wildcard $(CLIENT_DIR)/*.c)
CLIENT_OBJ := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(CLIENT_SRC))

OBJ = $(DAEMON_OBJ) $(ATTACH_OBJ) $(CLIENT_OBJ)

COMMON_DIR := $(SRC_DIR)/common
PROTO_SRC := $(SRC_DIR)/common/iirc.pb-c.c
COMMON_SRC := $(wildcard $(COMMON_DIR)/*.c)
ifeq ("$(wildcard $(PROTO_SRC))","")
	COMMON_SRC += $(PROTO_SRC)
endif
COMMON_OBJ := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(COMMON_SRC))

LIBIIRC := libiirc.so
LIBIIRC_FQN = $(LIBIIRC).$(LIBIIRC_MAJOR_VER)
LIBIIRC_LATEST = $(LIBIIRC_FQN).$(LIBIIRC_MINOR_VER)

DEPS := $(COMMON_OBJ:.o=.d) $(DAEMON_OBJ:.o=.d) $(ATTACH_OBJ:.o=.d)
DEPS += $(CLIENT_OBJ:.o=.d)

TARGETS := iirc iircd iirc-attach

W := -Wno-unused-parameter -Wall -Wextra -Werror

CFLAGS := $(shell pkg-config --cflags ncurses)
CFLAGS += -Isrc -D_POSIX_SOURCE -D_GNU_SOURCE
CFLAGS += -g -pedantic -std=c99

LDFLAGS := -L./ -Wl,-rpath=$(PREFIX)/lib
ifeq ($(STATIC_PROTOBUF_C),1)
	LDFLAGS += -Wl,-Bstatic -lprotobuf-c
	LDFLAGS += -Wl,-Bdynamic
else
	LDFLAGS += -lprotobuf-c
endif
LDFLAGS += $(shell pkg-config --libs ncurses) -liirc

CUR_SRC = $(patsubst $(OBJ_DIR)/%, $(SRC_DIR)/%.c, $*)

.PHONY: all clean libiirc install

all: $(TARGETS)

.SECONDARY:
$(SRC_DIR)/%.pb-c.h $(SRC_DIR)/%.pb-c.c: $(SRC_DIR)/%.proto
	@echo "PROCOC $@ <- $<"
	@cd src; \
	 protoc-c --c_out=./ common/iirc.proto

$(OBJ): $(DAEMON_SRC) $(ATTACH_SRC) $(COMMON_SRC)
	@mkdir -p $(@D)
	@echo CC $@
	@$(CC) $(CFLAGS) $(W) -MMD -MP -c $(CUR_SRC) -o $@

$(LIBIIRC): libiirc($(COMMON_OBJ))
	@echo ld $@
	@$(CC) -shared -Wl,-soname,libiirc.so.$(LIBIIRC_MAJOR_VER) -o $(LIBIIRC) $^

libiirc($(COMMON_OBJ)): $(COMMON_SRC)
	@mkdir -p $(dir $*)
	@echo CC $%
	@$(CC) $(CFLAGS) $(W) -fPIC -MMD -MP -c $(CUR_SRC) -o $%

libiirc: $(LIBIIRC)

iircd: $(DAEMON_OBJ) $(LIBIIRC)
	@echo CC $@
	@$(CC) $(DAEMON_OBJ) -o $@ $(LDFLAGS)

iirc-attach: $(ATTACH_OBJ) $(LIBIIRC)
	@echo CC $@
	@$(CC) $(ATTACH_OBJ) -o $@ $(LDFLAGS)

iirc: $(CLIENT_OBJ) $(LIBIIRC)
	@echo CC $@
	@$(CC) $(CLIENT_OBJ) -o $@ $(LDFLAGS)

install: $(TARGETS)
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/bin
	install -m 0755 $(LIBIIRC) $(PREFIX)/lib
	install -m 0755 $(TARGETS) $(PREFIX)/bin
	ldconfig

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(TARGETS)
	rm -rf $(LIBIIRC)
	rm -rf $(PROTO_SRC)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPS)
endif
