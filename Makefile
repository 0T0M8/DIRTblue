CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDLIBS = -lsqlite3

SOURCES = server.c db.c http.c
TARGET = server_sqlite_mod

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o mydb.db access.log

.PHONY: all clean
