#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "libs/connection/connection.h"
#include "libs/commands/commands.h"

enum {
  PROG,
  PRINT_HELP,
  NB_ARGS
};

int main(int argc, char **argv) {
  if (argc >= NB_ARGS) {
    if (
      strcmp(argv[PRINT_HELP], "--help") == 0 
      || strcmp(argv[PRINT_HELP], "-h") == 0
    ) {
      // Affiche les commandes disponibles
      print_commands();
      return EXIT_SUCCESS;
    }
  }
  // Connexion à la file de requêtes
  server_queue *server_q = connect(SHM_NAME);
  if (server_q == NULL) {
    perror("Impossible d'établir un lien avec la file de connexion ");
    return EXIT_FAILURE;
  }
  // Création des noms des pipes
  char request_pipe[NAME_MAX + 1];
  sprintf(request_pipe, "./tmp/pipe_requete_%ld", (long) getpid());
  char response_pipe[NAME_MAX + 1];
  sprintf(response_pipe, "./tmp/pipe_reponse_%ld", (long) getpid());
  // Création de la pipe de requête
  request_fifo *req_fifo;
  if ((req_fifo = init_request_fifo(request_pipe)) == NULL) {
    perror("Impossible d'initialiser le réseau de requête ");
    return EXIT_FAILURE;
  }
  // Création de la pipe de réponse
  response_fifo *res_fifo;
  if ((res_fifo = init_response_fifo(response_pipe)) == NULL) {
    perror("Impossible d'initialiser le réseau de requête ");
    return EXIT_FAILURE;
  }
  // Envoie de la requête de connexion au serveur
  int r = EXIT_SUCCESS;
  if (send_shm_request(server_q, request_pipe, response_pipe) < 0) {
    perror("Impossible d'envoyer une requête à la file de connexion ");
    r = EXIT_FAILURE;
    goto free;
  }
  char s[MAX_COMMAND_LENGTH + 1];
  do {
    fprintf(stdout, "> ");
    if (fgets(s, MAX_COMMAND_LENGTH, stdin) == NULL) {
      fprintf(stderr, "Erreur lors de la lecture de la commande\n");
      exit(EXIT_FAILURE);
    }
    // Enlève le \n à la fin de la commande
    s[strlen(s) - 1] = '\0';
    // Une fois connecté envoie la requête à exécuter
    if (send_request(req_fifo, s) < 0) {
      perror("Impossible d'envoyer la requête");
    }
    char res_buffer[MAX_RESPONSE_LENGTH + 1];
    // Ecoute la réponse du serveur
    if (listen_response(res_fifo, res_buffer) < 0) {
      perror("Impossible de recevoir la réponse du serveur ");
    } else {
      fprintf(stdout, "%s\n", res_buffer);
    }
  } while (strcmp(s, "exit") != 0);
  // Libère les ressources en se déconnectant
free:
  if (disconnect(server_q) < 0) {
    fprintf(stderr, "Une erreur est survenue lors de la déconnexion\n");
    r = EXIT_FAILURE;
  }
  if (close_request_fifo(req_fifo) < 0) {
    perror("Impossible de fermer la pipe de requête ");
    r = EXIT_FAILURE;
  }
  if (close_response_fifo(res_fifo) < 0) {
    perror("Impossible de fermer la pipe de réponse ");
    r = EXIT_FAILURE;
  }

  return r;
}
