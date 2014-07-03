TARGET = qemu-compatibility.so
SOURCE = compatibility.c

all: $(TARGET)

$(TARGET): $(SOURCE) Makefile
	gcc -shared -fPIC -o $@ $< `pkg-config --cflags --libs glib-2.0`
