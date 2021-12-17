CC = gcc
CFLAGS = -std=c18 \
  -Wall -Wconversion -Werror -Wextra -Wfatal-errors -Wpedantic -Wwrite-strings \
  -O2 -pthread
LDFLAGS = -pthread -lrt
LIBS = libs
CONNECTION = $(LIBS)/connection/connection.o
COMMANDS = $(LIBS)/commands/commands.o
objects_server = server.o $(CONNECTION) $(COMMANDS)
objects_client = client.o $(CONNECTION) $(COMMANDS)
executable_server = server
executable_client = client

all: $(executable_server) $(executable_client)

clean:
	$(RM) $(objects_server) $(objects_client) $(executable_server) $(executable_client)

$(executable_server): $(objects_server)
	$(CC) $(objects_server) $(LDFLAGS) -o $(executable_server)

$(executable_client): $(objects_client)
	$(CC) $(objects_client) $(LDFLAGS) -o $(executable_client)

connection.o: connection.c
commands.o: commands.c
server.o: server.c
client.o: client.c