#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "libs/connection/connection.h"

enum args {
  PROG,
  CMD,
  NB_ARGS
};

int main(int argc, char **argv) {
  if (argc < NB_ARGS) {
    fprintf(stderr, "Syntaxe : %s \"command\"\n", argv[PROG]);
    return EXIT_FAILURE;
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
  // Envoie de la requête de connexion au serveur
  if (send_shm_request(server_q, request_pipe, response_pipe) < 0) {
    perror("Impossible d'envoyer une requête à la file de connexion ");
    return EXIT_FAILURE;
  }
  // Une fois connecté envoie la requête à exécuter
  if (send_request(request_pipe, argv[CMD]) < 0) {
    perror("Impossible d'envoyer la requête");
  }
  char res_buffer[MAX_RESPONSE_LENGTH + 1];
  // Ecoute la réponse du serveur
  if (listen_response(response_pipe, res_buffer) < 0) {
    perror("Impossible de recevoir la réponse du serveur ");
  } else {
    fprintf(stdout, "%s\n", res_buffer);
  }
  // Libère les ressources en se déconnectant
  if (disconnect(server_q) < 0) {
    fprintf(stderr, "Une erreur est survenue lors de la déconnexion\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
