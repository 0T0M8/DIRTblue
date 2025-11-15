CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDLIBS = -lsqlite3

SOURCES = server.c db.c util.c static.c router.c
TARGET = server_modular

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o mydb.db access.log

.PHONY: all clean
