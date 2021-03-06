TARGET = priv/id3as_codecs
CC = gcc
CFLAGS = -g -Wall -I../id3as_common_c/c_src -Ideps/id3as_common_c/c_src -I /usr/local/include -std=c99
LDFLAGS = -L../id3as_common_c/priv -Ldeps/id3as_common_c/priv -L /usr/local/lib -lid3as_common -lei

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
	ERL_DIR = $(shell erl -noshell -eval 'io:format("~p~n", [code:lib_dir(erl_interface)])' -eval 'init:stop()')
	CFLAGS += -I $(ERL_DIR)/include
	LDFLAGS += -L$(ERL_DIR)/lib $(FFMPEG_STATIC_LIBS) $(FFMPEG_DYN_LIBS) -lz -lm -lpthread -framework CoreFoundation -framework VideoDecodeAcceleration -framework CoreVideo
endif
ifeq ($(UNAME), Linux)
	ERL_DIR = $(shell erl -noshell -eval 'io:format("~p~n", [code:lib_dir(erl_interface)])' -eval 'init:stop()')
	CFLAGS += -I $(ERL_DIR)/include
	LDFLAGS += -L$(ERL_DIR)/lib -Wl,-Bstatic $(FFMPEG_STATIC_LIBS) -Wl,-Bdynamic -lz $(FFMPEG_DYN_LIBS) -lm -lpthread
endif

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard c_src/*.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	mkdir -p priv
	$(CC) $(OBJECTS) -Wall $(LDFLAGS) -o $@
	$(MAKE) -f erlang.mk app

clean:
	-rm -f *.o
	-rm -f $(TARGET)
	$(MAKE) -f erlang.mk clean
