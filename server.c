#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libs/connection/connection.h"
#include "libs/commands/commands.h"
#include "libs/list/list.h"
#include "libs/yml_parser/yml_parser.h"

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
 * Démarre un démon.
 */
int skeleton_dameon();

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
 * Compare 2 requêtes.
 */
int request_cmp(shm_request *a, shm_request *b);

/**
 * Libère les ressources du serveur en cas d'interruption par un signal.
 */
void sig_free(int signum);

/**
 * Libère les clients connectés au serveur en envoyant une réponse de 
 * terminaison.
 * 
 * @param {shm_request *} La requête du client à libérer.
 * @param {int} L'accumulateur.
 * @return {int} 1 si tous les clients ont été libérés et -1 sinon.
 */
int free_online_clients(shm_request *req, int acc);

/*
 * Variables globales
 */

// File de requêtes de connexion au serveur
server_queue *server_q;
// Liste contenant les clients actuellement connectés au serveur ayant un
// thread alloué.
list *client_list = NULL;
// Parseur de fichier yml pour la configuration du serveur
yml_parser *config = NULL;
// Indique si le serveur est un démon
int daemon = 0;
// Timeout de réponse du serveur
int res_timeout = 0;

int main(void) {
  // Création de la liste des clients où l'on stockera les pipes de réponse
  client_list = init_list((int (*)(void *, void *)) request_cmp);
  if (client_list == NULL) {
    fprintf(stderr, "Impossible d'intialiser la liste des clients\n");
    return EXIT_FAILURE;
  }
  // Chargement de la configuration
  config = init_yml_parser("./conf/server.yml", NULL);
  if (config == NULL) {
    fprintf(stderr, "Impossible de créer le parseur yml\n");
    list_dispose(client_list);
    return EXIT_FAILURE;
  }
  if (exec_parser(config) < 0) {
    fprintf(stderr, "Impossible de charger la configuration\n");
    list_dispose(client_list);
    free_parser(config);
    return EXIT_FAILURE;
  }
  get(config, "res_timeout", &res_timeout);
  get(config, "daemon", &daemon);
  if (daemon != 0) {
    skeleton_dameon();
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
  int nb_slots;
  get(config, "slots", &nb_slots);
  server_q = init_server_queue((size_t) nb_slots);
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

int skeleton_dameon() {
  switch (fork()) {
    case -1:
      fprintf(stderr, "Impossible de créer le démon\n");
      return -1;
    case 0:
      if (setsid() < 0) {
        fprintf(stderr, "Impossible de créer le démon\n");
        return -1;
      }
      switch (fork()) {
        case -1:
          fprintf(stderr, "Impossible de créer le démon\n");
          return -1;
        case 0:
          umask(0);
          for (int i = 0; i < sysconf(_SC_OPEN_MAX); ++i) {
            close(i);
          }
          int my_stdout = open("/dev/null", O_RDWR);
          int my_stderr = open("/dev/null", O_RDWR);
          if (dup2(my_stdout, STDOUT_FILENO) < 0) {
            return -1;
          }
          if (dup2(my_stderr, STDERR_FILENO) < 0) {
            return -1;
          }
          return 1;
        default:
          exit(EXIT_SUCCESS);
      }
    default:
      exit(EXIT_SUCCESS);
  }
}

int allocate_request_ressources(shm_request *request) {
  // Ajoute la requête du client à la liste
  shm_request *r = list_add(client_list, request, sizeof(*request));
  if (r == NULL) {
    return NOT_ENOUGH_MEMORY;
  }
  // Créer le thread et passe la requête dupliquée en paramètre et le détache
  pthread_t request_thread;
  if (pthread_create(&request_thread, NULL, handle_request, r) != 0) {
    return THREAD_ERROR;
  }
  if (pthread_detach(request_thread) != 0) {
    return THREAD_ERROR;
  }

  return 1;
}

void *handle_request(void *request) {
  shm_request *req = (shm_request *) request;
  // Récupère la taille maximale des requêtes dans la configuration
  int res_max = -1;
  get(config, "response_limit", &res_max);
  // Ecoute la requête
  char req_buffer[MAX_COMMAND_LENGTH + 1];
  if (listen_request(req->request_pipe, req_buffer) < 0) {
    perror("Erreur lors de la lecture d'une requete ");
    send_response(req->response_pipe, "Erreur lors de la récéption de la "
        "requête\n", (ssize_t) res_max, (time_t) res_timeout);
    goto remove;
  }
  int tube[2];
  while (strcmp(req_buffer, "exit") != 0) {
    if (pipe(tube) < 0) {
      perror("pipe ");
      fprintf(stderr, "Impossible de relier la commande et la réponse\n");
      goto remove;
    }
    char *res_buffer = NULL;
    ssize_t n = 0;
    size_t total = 0;
    switch (fork()) {
      case -1:
        perror("fork ");
        send_response(req->response_pipe, "Erreur lors de l'exécution de la "
            "commande\n", (ssize_t) res_max, (time_t) res_timeout);
        goto remove;
      case 0:
        fflush(stdout);
        fflush(stderr);
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
          fprintf(stderr, "Erreur lors de l'exécution de la commande.\n");
          return NULL;
        }
        if (exec_cmd(req_buffer, req) < 0) {
          fprintf(stderr, "Erreur lors de l'exécution de la commande.\n");
        }
        return NULL;
      default:
        if (close(tube[1]) < 0) {
          perror("close ");
          send_response(req->response_pipe, "Erreur lors de l'exécution "
              "de la commande\n", (ssize_t) res_max, (time_t) res_timeout);
        }
        // Attend la mort du processus enfant
        wait(NULL);
        do {
          total += (size_t) n;
          res_buffer = realloc(res_buffer, total + PIPE_BUF + 1);
          if (res_buffer == NULL) {
            send_response(req->response_pipe, "Erreur lors de l'exécution "
                "de la commande\n", (ssize_t) res_max, (time_t) res_timeout);
            goto remove;
          }
        } while ((n = read(tube[0], res_buffer + total, PIPE_BUF)) > 0);
        if (n == -1) {
          perror("read ");
          send_response(req->response_pipe, "Erreur lors de la liaison "
              "entre la commande et la réponse\n", (ssize_t) res_max, (time_t) res_timeout);
        }
        res_buffer[total] = '\0';
        if (close(tube[0]) < 0) {
          perror("Impossible de fermer tube 0 : ");
          goto remove;
        }
        int r = send_response(req->response_pipe, res_buffer, (ssize_t) res_max,
            (time_t) res_timeout);
        if (r < 0) {
          perror("Impossible d'envoyer la réponse au client");
          free(res_buffer);
          goto remove;
        } else if (r == 0) {
          fprintf(stderr, "Un client a été timeout.\n");
          kill(req->pid, SIGUSR2);
          goto remove;
        }
    }
    if (listen_request(req->request_pipe, req_buffer) < 0) {
      perror("Erreur lors de la lecture d'une requete ");
      send_response(req->response_pipe, "Erreur lors de la récéption de la "
          "requête\n", (ssize_t) res_max, (time_t) res_timeout);
    }
    free(res_buffer);
  }
  if (send_response(req->response_pipe, "Déconnexion du serveur...\n", (ssize_t) res_max, (time_t) res_timeout) < 0) {
    perror("Impossible d'envoyer la réponse au client ");
  }
remove:
  if (list_remove(client_list, req) <= 0) {
    fprintf(stderr, 
        "Impossible d'enlever le client %d de la liste des clients\n", 
        req->pid);
  }
  return NULL;
}

int request_cmp(shm_request *a, shm_request *b) {
  if (a->pid > b->pid) {
    return 1;
  }
  return a->pid == b->pid ? 0 : -1;
}

void sig_free(int signum) {
  int status = EXIT_SUCCESS;
  if (signum == SIGINT || signum == SIGQUIT || signum == SIGTERM) {
    fprintf(stderr, "\nInterruption du serveur suite à un signal émis.\n");
  } else if (signum == SIGPIPE) {
    return;
  } else {
    fprintf(stderr, 
        "Interruption du serveur suite à un signal innatendu : %d\n", signum);
  }
  int r = list_apply(client_list, (int (*)(void *, int)) free_online_clients);
  if (r < 0) {
    fprintf(stderr, "Tous les clients n'ont pas pu être libérés\n");
    status = EXIT_FAILURE;
  }
  if (list_dispose(client_list) < 0) {
    fprintf(stderr, "Impossible de libérer la liste des clients\n");
    status = EXIT_FAILURE;
  }
  if (free_parser(config) < 0) {
    fprintf(stderr, "Impossible de free le parseur\n");
    status = EXIT_FAILURE;
  }
  if (free_server_queue(server_q) < 0) {
    perror("Impossible de libérer la SHM ");
    status = EXIT_FAILURE;
  }

  exit(status);
}

int free_online_clients(shm_request *req, int acc) {
  if (kill(req->pid, SIGUSR1) < 0) {
    acc = -1;
  }

  return acc < 0 ? -1 : 1;
}
