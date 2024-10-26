CC = gcc
CFLAGS = -lm -O3 -march=native

SRC = src/main.c
TARGET = pfs

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
