CC = gcc
CFLAGS = -Wall
# ADD -lm HERE:
LIBS_SERVER = -lncurses -lm
LIBS_DRONE = -lm
DEPS = common.h

all: server input drone obstacle target

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

server: pro_B.c $(DEPS)
	$(CC) $(CFLAGS) pro_B.c -o server $(LIBS_SERVER)

input: pro_I.c $(DEPS)
	$(CC) $(CFLAGS) pro_I.c -o input

drone: pro_D.c $(DEPS)
	$(CC) $(CFLAGS) pro_D.c -o drone $(LIBS_DRONE)

obstacle: pro_O.c $(DEPS)
	$(CC) $(CFLAGS) pro_O.c -o obstacle

target: pro_T.c $(DEPS)
	$(CC) $(CFLAGS) pro_T.c -o target

run: all
	./server

clean:
	rm -f server input drone obstacle target *.o
