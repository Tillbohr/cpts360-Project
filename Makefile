CC     = gcc
CFLAGS = $(shell pkg-config --cflags gtk4)
LIBS   = $(shell pkg-config --libs gtk4)
TARGET = scheduler
SRCS   = GUI.c scheduling.c
HDRS   = scheduling.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) $(TARGET).exe *.o
