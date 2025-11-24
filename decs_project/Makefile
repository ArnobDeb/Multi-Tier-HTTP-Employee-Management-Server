CC = gcc
CFLAGS = -std=gnu11 -O2 -Wall -Wextra -pthread

# CivetWeb paths (local)
CIVET_INC = ./civetweb/include
CIVET_LIB_PATH = ./civetweb

# MySQL include & lib paths
MYSQL_INC = /usr/include/mysql
MYSQL_LIB = /usr/lib/x86_64-linux-gnu

INCLUDES = -I$(MYSQL_INC) -I$(CIVET_INC)
LIBS = -L$(MYSQL_LIB) -L$(CIVET_LIB_PATH) -lcivetweb -lmysqlclient -lpthread -lm

SRCS = server.c cache.c db.c
OBJS = $(SRCS:.c=.o)
TARGET = empserver

# New loadgen target
LOADGEN = loadgen
LOADGEN_SRC = loadgen.c

all: $(TARGET) $(LOADGEN)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LOADGEN): $(LOADGEN_SRC)
	$(CC) $(CFLAGS) -o $(LOADGEN) $(LOADGEN_SRC) -lm -pthread

clean:
	rm -f $(OBJS) $(TARGET) $(LOADGEN)
