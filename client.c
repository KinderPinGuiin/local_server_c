#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "libs/connection/connection.h"
#include "libs/commands/commands.h"

/**
 * Arguments possible du programme client.
 */
enum {
  PROG,
  PRINT_HELP,
  NB_ARGS
};

/**
 * Libère les ressources du client et le déconnecte en cas d'interruption par
 * un signal.
 */
void sig_disconnect(int signum);

/*
 * Variables globales nécessaires au signaux.
 */

request_fifo *req_fifo;
response_fifo *res_fifo;
server_queue *server_q;

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
  // Gestion des signaux
  struct sigaction action;
  action.sa_handler = sig_disconnect;
  action.sa_flags = 0;
  if (sigfillset(&action.sa_mask) == -1) {
    perror("Erreur lors de la création du masque des signaux bloqués ");
    return EXIT_FAILURE;
  }
  // On associe l'action à différents signaux
  if (sigaction(SIGINT, &action, NULL) == -1) {
    perror("Erreur lors de l'association d'une action aux signaux ");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGQUIT, &action, NULL) == -1) {
    perror("Erreur lors de l'association d'une action aux signaux ");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGTERM, &action, NULL) == -1) {
    perror("Erreur lors de l'association d'une action aux signaux ");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGUSR1, &action, NULL) == -1) {
    perror("Erreur lors de l'association d'une action aux signaux ");
    return EXIT_FAILURE;
  }
  // Connexion à la file de requêtes
  server_q = connect(SHM_NAME);
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
  if ((req_fifo = init_request_fifo(request_pipe)) == NULL) {
    perror("Impossible d'initialiser le réseau de requête ");
    return EXIT_FAILURE;
  }
  // Création de la pipe de réponse
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
      if (send_request(req_fifo, "exit") < 0 
          || listen_response(res_fifo, s) < 0) {
        fprintf(stderr, "Impossible d'échanger une requête de fin de "
            "transmission avec le serveur");
      } else {
        fprintf(stdout, "%s\n", s);
      }
      r = EXIT_FAILURE;
      goto free;
    }
    // Enlève le \n à la fin de la commande
    s[strlen(s) - 1] = '\0';
    // Si la commande est vide on n'affiche pas de message d'erreur
    if (strcmp(s, "") == 0) {
      continue;
    }
    // Si la commande est invalide on affiche une erreur
    if (!is_command_available(s)) {
      fprintf(stderr, "Commande invalide : %s\n", s);
      continue;
    }
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

void sig_disconnect(int signum) {
  int r = EXIT_SUCCESS;  
  if (signum == SIGINT || signum == SIGQUIT || signum == SIGTERM) {
    fprintf(stdout, "\nInterruption de la connexion au serveur (Signal)...\n");
    if (send_request(req_fifo, "exit") < 0) {
      fprintf(stderr, "Impossible de transmettre une dernière requête "
          "de terminaison au serveur");
      r = EXIT_FAILURE;
    }
    char s[MAX_RESPONSE_LENGTH + 1];
    if (send_request(req_fifo, "exit") < 0 
        || listen_response(res_fifo, s) < 0) {
      fprintf(stderr, "Impossible d'échanger une requête de fin de "
          "transmission avec le serveur");
      r = EXIT_FAILURE;
    } else {
      fprintf(stdout, "%s\n", s);
    }
  } else if (signum == SIGUSR1) {
    fprintf(stderr, 
        "Interruption subite du serveur, vous avez été déconnecté\n");
  }
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

  exit(r);
}