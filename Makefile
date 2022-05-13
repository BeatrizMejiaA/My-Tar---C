CC = gcc
CFLAGS =  -Wall -Wextra -Werror
LDFLAGS = -lm
OBJFILES = my_tarlib.o mytar.o
TARGET = mytar
 
all: $(TARGET)
 
$(TARGET): $(OBJFILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJFILES) $(LDFLAGS)
 
clean:
	rm -rf $(OBJFILES) $(TARGET) *~