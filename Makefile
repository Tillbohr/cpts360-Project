# Requires GTK4 and SQLite3.
# On MSYS2/MinGW install with:
#   pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-sqlite3

CC     = gcc
CFLAGS = $(shell pkg-config --cflags gtk4)
LIBS   = $(shell pkg-config --libs gtk4) -lsqlite3
TARGET = scheduler
SRCS   = GUI.c scheduling.c db.c
HDRS   = scheduling.h db.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) $(TARGET).exe *.o scheduler.db
