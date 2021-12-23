#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libs/connection/connection.h"
#include "libs/commands/commands.h"
#include "libs/list/list.h"

/*
 * Codes d'erreur
 */

#define NOT_ENOUGH_MEMORY -1
#define THREAD_ERROR -2

/*
 * Variables externes
 */

extern int errno;

/*
 * Fonctions
 */

/**
 * Fonction run du thread traitant la requête request.
 * 
 * @param {void *} La requête à traiter.
 */
void *handle_request(void *request);

/**
 * Créé le thread permettant de traiter la requête request.
 * 
 * @param {shm_request *} La requête à traiter.
 * @return {int} 1 en cas de succès et une valeur négative en cas d'erreur.
 *               Celle-ci peut-être récupérée via perror.
 */
int allocate_request_ressources(shm_request *request);

/**
 * Libère les ressources du serveur en cas d'interruption par un signal.
 */
void sig_free(int signum);

/**
 * Libère les clients connectés au serveur en envoyant une réponse de 
 * terminaison.
 * 
 * @param {char *} La pipe où envoyer la réponse.
 * @param {int} L'accumulateur.
 * @return {int} 1 si tous les clients ont été libérés et -1 sinon.
 */
int free_online_clients(pid_t *pid, int acc);

/*
 * Variables globales
 */

// File de requêtes de connexion au serveur
server_queue *server_q;
// Liste contenant les clients actuellement connectés au serveur ayant un
// thread alloué.
list *client_list;

int main(void) {  
  // Création de la liste des clients où l'on stockera les pipes de réponse
  client_list = init_list((int (*)(void *, void *)) strcmp);
  if (client_list == NULL) {
    fprintf(stderr, "Impossible d'intialiser la liste des clients\n");
    return EXIT_FAILURE;
  }
  // Gestion des signaux
  struct sigaction action;
  action.sa_handler = sig_free;
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
  if (sigaction(SIGPIPE, &action, NULL) == -1) {
    perror("Erreur lors de l'association d'une action aux signaux ");
    return EXIT_FAILURE;
  }

  // Mise en place de la mémoire partagée et la remplit avec une file
  server_q = init_server_queue();
  if (server_q == NULL) {
    perror("Une erreur est survenue lors du chargement du SHM ");
    return EXIT_FAILURE;
  }

  // Lancement du serveur
  fprintf(stdout, "File des requêtes initialisée. "
      "Ecoute des requêtes en cours :\n----------\n");
  while (1) {
    // Dès qu'une connexion entre on la traite
    if (fetch_shm_request(server_q, allocate_request_ressources) < 0) {
      fprintf(stderr, "Impossible de traiter la requête\n");
      return EXIT_FAILURE;
    }
    fprintf(stdout, "Une connexion a été établie avec un client\n");
  }

  return EXIT_SUCCESS;
}

int allocate_request_ressources(shm_request *request) {
  // Duplique la requête pour pas qu'elle ne soit perdue lors de son traitement
  shm_request *request_cpy = malloc(sizeof(shm_request));
  if (request_cpy == NULL) {
    return NOT_ENOUGH_MEMORY;
  }
  memcpy(request_cpy, request, sizeof(shm_request));
  // Ajoute le client à la liste
  int r = list_add(client_list, &request_cpy->pid, sizeof(pid_t));
  if (r < 0) {
    return NOT_ENOUGH_MEMORY;
  }
  // Créer le thread et passe la requête dupliquée en paramètre et le détache
  pthread_t request_thread;
  if (pthread_create(&request_thread, NULL, handle_request, request_cpy) != 0) {
    return THREAD_ERROR;
  }
  if (pthread_detach(request_thread) != 0) {
    return THREAD_ERROR;
  }

  return 1;
}

void *handle_request(void *request) {
  shm_request *req = (shm_request *) request;
  // Ecoute la requête
  char req_buffer[MAX_COMMAND_LENGTH + 1];
  if (listen_request(req->request_pipe, req_buffer) < 0) {
    perror("Erreur lors de la lecture d'une requete ");
    send_response(req->response_pipe, "Erreur lors de la récéption de la "
        "requête\n");
  }
  int tube[2];
  while (strcmp(req_buffer, "exit") != 0) {
    ssize_t n;
    char res_buffer[MAX_RESPONSE_LENGTH + 1];
    if (pipe(tube) < 0) {
      perror("pipe ");
      fprintf(stderr, "Impossible de relier la commande et la réponse\n");
      return NULL;
    }
    switch (fork()) {
      case -1:
        perror("fork ");
        send_response(req->response_pipe, "Erreur lors de l'exécution de la "
            "commande\n");
        return NULL;
      case 0:
        if (dup2(tube[1], STDOUT_FILENO) < 0) {
          perror("dup2 ");
          fprintf(stderr, "Impossible de relier la commande et la réponse\n");
          return NULL;
        }
        if (dup2(tube[1], STDERR_FILENO) < 0) {
          perror("dup2 ");
          fprintf(stderr, "Impossible de relier la commande et la réponse\n");
          return NULL;
        }
        if (close(tube[0]) < 0) {
          perror("close ");
          send_response(req->response_pipe, "Erreur lors de l'exécution "
              "de la commande\n");
          return NULL;
        }
        if (exec_cmd(req_buffer) < 0) {
          send_response(req->response_pipe, "Erreur lors de l'exécution "
              "de la commande\n");
        }
        return NULL;
      default:
        if (close(tube[1]) < 0) {
          perror("close ");
          send_response(req->response_pipe, "Erreur lors de l'exécution "
              "de la commande\n");
        }
        // Attend la mort du processus enfant
        wait(NULL);
        if ((n = read(tube[0], res_buffer, MAX_RESPONSE_LENGTH)) < 0) {
          perror("read ");
          send_response(req->response_pipe, "Erreur lors de la liaison "
              "entre la commande et la réponse\n");
        } else {
          res_buffer[n] = '\0';
        }
        if (close(tube[0]) < 0) {
          perror("Impossible de fermer tube 0 : ");
          return NULL;
        }
        if (send_response(req->response_pipe, res_buffer) < 0) {
          perror("Impossible d'envoyer la réponse au client");
        }
    }
    if (listen_request(req->request_pipe, req_buffer) < 0) {
      perror("Erreur lors de la lecture d'une requete ");
      send_response(req->response_pipe, "Erreur lors de la récéption de la "
          "requête\n");
    }
  }
  if (list_remove(client_list, &req->pid) < 0) {
    fprintf(stderr, 
        "Impossible d'enlever le client %d de la liste des clients\n", 
        req->pid);
  }
  if (send_response(req->response_pipe, "Déconnexion du serveur...\n") < 0) {
    perror("Impossible d'envoyer la réponse au client ");
  }
  // Libère les ressources allouées par la requête
  free(req);
  return NULL;
}

void sig_free(int signum) {
  if (signum == SIGINT || signum == SIGQUIT || signum == SIGTERM) {
    fprintf(stderr, "\nInterruption du serveur suite à un signal émit.\n");
    free_server_queue(server_q);
    int r = list_apply(client_list, (int (*)(void *, int)) free_online_clients);
    if (r < 0) {
      fprintf(stderr, "Tous les clients n'ont pas pu être libérés\n");
    }
  } else {
    fprintf(stderr, 
        "Interruption du serveur suite à un signal innatendu : %d\n", signum);
    free_server_queue(server_q);
  }

  exit(EXIT_SUCCESS);
}

int free_online_clients(pid_t *pid, int acc) {
  if (kill(*pid, SIGUSR1) < 0) {
    acc = -1;
  }

  return acc < 0 ? -1 : 1;
}
