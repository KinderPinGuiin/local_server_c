CC = gcc
CFLAGS = -std=c18 \
  -Wall -Wconversion -Werror -Wextra -Wfatal-errors -Wpedantic -Wwrite-strings \
  -O2 -pthread
LDFLAGS = -pthread -lrt
objects_server = server.o libs/connection/connection.o
objects_client = client.o libs/connection/connection.o
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
server.o: server.c
client.o: client.c