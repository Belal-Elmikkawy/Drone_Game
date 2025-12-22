CC = gcc
CFLAGS = -Wall -I./include -D_GNU_SOURCE

# Libraries
LIBS_SERVER = -lncurses -lm
LIBS_DRONE = -lm
LIBS_WATCHDOG = -lncurses

# Executable Paths (Inside src folders)
EXEC_SERVER = src/server/server
EXEC_INPUT = src/input/input
EXEC_DRONE = src/drone/drone
EXEC_OBSTACLE = src/obstacle/obstacle
EXEC_TARGET = src/target/target
EXEC_WATCHDOG = src/watchdog/watchdog

TARGETS = $(EXEC_SERVER) $(EXEC_INPUT) $(EXEC_DRONE) $(EXEC_OBSTACLE) $(EXEC_TARGET) $(EXEC_WATCHDOG)

all: $(TARGETS)

# --- Compilation Rules ---

$(EXEC_SERVER): src/server/pro_B.c
	$(CC) $(CFLAGS) src/server/pro_B.c -o $(EXEC_SERVER) $(LIBS_SERVER)

$(EXEC_INPUT): src/input/pro_I.c
	$(CC) $(CFLAGS) src/input/pro_I.c -o $(EXEC_INPUT)

$(EXEC_DRONE): src/drone/pro_D.c
	$(CC) $(CFLAGS) src/drone/pro_D.c -o $(EXEC_DRONE) $(LIBS_DRONE)

$(EXEC_OBSTACLE): src/obstacle/pro_O.c
	$(CC) $(CFLAGS) src/obstacle/pro_O.c -o $(EXEC_OBSTACLE)

$(EXEC_TARGET): src/target/pro_T.c
	$(CC) $(CFLAGS) src/target/pro_T.c -o $(EXEC_TARGET)

$(EXEC_WATCHDOG): src/watchdog/pro_W.c
	$(CC) $(CFLAGS) src/watchdog/pro_W.c -o $(EXEC_WATCHDOG) $(LIBS_WATCHDOG)

# --- Run ---
run: all
	./$(EXEC_SERVER)

clean:
	rm -f $(TARGETS) *.o *.log pid_registry.txt
