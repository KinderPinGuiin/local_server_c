CC = gcc
CFLAGS = -std=c18 \
  -Wall -Wconversion -Werror -Wextra -Wfatal-errors -Wpedantic -Wwrite-strings \
  -O2 -pthread -c -fPIC
LDFLAGS = -pthread -lrt
LIBS = libs
CONNECTION = $(LIBS)/connection/connection.o
LIBCONNECTION = $(LIBS)/connection/libconnection.so
COMMANDS = $(LIBS)/commands/commands.o
LIST = $(LIBS)/list/list.o
YML = $(LIBS)/yml_parser/yml_parser.o
objects_server = server.o $(COMMANDS) $(LIST) $(YML) $(LIBCONNECTION)
objects_client = client.o $(COMMANDS) $(YML) $(LIBCONNECTION)
executable_server = server
executable_client = client

all: $(executable_server) $(executable_client)

clean:
	$(RM) $(objects_server) $(objects_client) $(executable_server) \
	$(executable_client)

$(executable_server): $(objects_server)
	$(CC) -L$(LIBS)/connection $(objects_server) $(LDFLAGS) -lconnection -o $(executable_server)
	$(RM) server.o

$(executable_client): $(objects_client)
	$(CC) -L$(LIBS)/connection $(objects_client) $(LDFLAGS) -lconnection -o $(executable_client)
	$(RM) client.o

$(LIBCONNECTION): $(CONNECTION)
	$(CC) $(CONNECTION) -shared -o $(LIBCONNECTION)
	$(RM) $(CONNECTION)
$(CONNECTION): $(LIBS)/connection/connection.c
$(COMMANDS): $(LIBS)/commands/commands.c
$(LIST): $(LIBS)/list/list.c
$(YML): $(LIBS)/yml_parser/yml_parser.c
server.o: server.c
client.o: client.c