TARGET = priv/id3as_codecs
CC = gcc
CFLAGS = -g -Wall -I../id3as_common_c/c_src -Ideps/id3as_common_c/c_src -I /usr/local/include
LDFLAGS = -L../id3as_common_c/priv -Ldeps/id3as_common_c/priv -L /usr/local/lib -lid3as_common

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
	CFLAGS += -I /usr/local/lib/erlang/lib/erl_interface-3.7.13/include
	LDFLAGS += $(FFMPEG_STATIC_LIBS) $(FFMPEG_DYN_LIBS) -lz -lm -lpthread -framework CoreFoundation -framework VideoDecodeAcceleration -framework CoreVideo
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

clean:
	-rm -f *.o
	-rm -f $(TARGET)
