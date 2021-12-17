#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <limits.h>
#include <string.h>
#include "connection.h"

extern int errno;

#ifndef NAME_MAX

#define NAME_MAX 255

#endif

/*
 * Manipulation de la queue de connexion au serveur
 */

struct server_queue {
  int shm_fd;
  sem_t mutex;
  sem_t empty;
  sem_t full;
  size_t length; // Le nombre d'éléments dans le tampon
  size_t head;   // Position d'ajout dans le tampon
  size_t tail;   // Position de suppression dans le tampon
  shm_request buffer[MAX_SLOT];
};

server_queue *init_server_queue() {
  // Création du SHM
  int shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL,
      S_IRUSR | S_IWUSR);
  if (shm_fd < 0) {
    return NULL;
  }
  if (ftruncate(shm_fd, sizeof(server_queue)) < 0) {
    goto err;
  }
  server_queue *server_q = mmap(NULL, sizeof(server_queue), 
    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (server_q == MAP_FAILED) {
    goto err;
  }
  // Remplissage de la mémoire
  server_q->shm_fd = shm_fd;
  if (sem_init(&server_q->mutex, 1, 1) == -1) {
    return NULL;
  }
  if (sem_init(&server_q->empty, 1, MAX_SLOT) == -1) {
    return NULL;
  }
  if (sem_init(&server_q->full, 1, 0) == -1) {
    return NULL;
  }
  server_q->length = 0;
  server_q->head = 0;
  server_q->tail = 0;

  return server_q;

err:
  if (close(shm_fd) < 0) {
    fprintf(stderr, "init_server_queue: Impossible de fermer le fichier shm");
    perror(" ");
    errno = 0;
  }
  if (shm_unlink(SHM_NAME) < 0) {
    fprintf(stderr, 
        "init_server_queue: Impossible de supprimer le fichier /dev/shm%s",
        SHM_NAME);
    perror(" ");
    errno = 0;
  }

  return NULL;
}

server_queue *connect(const char *shm_name) {
  // Connexion au SHM
  int shm_fd = shm_open(shm_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (shm_fd < 0) {
    return NULL;
  }
  server_queue *server_q = mmap(NULL, sizeof(server_queue), 
    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (server_q == MAP_FAILED) {
    if (close(shm_fd) < 0) {
      return NULL;
    }
  }
  // Ferme le descripteur car il ne sera plus utile après
  if (close(shm_fd) < 0) {
    return NULL;
  }

  return server_q;
}

int wait_while_queue_is_empty(server_queue *queue_p) {
  if (queue_p == NULL) {
    return INVALID_POINTER;
  }
  if (sem_wait(&queue_p->full) == -1) {
    return SEMAPHORE_ERROR;
  }
  if (sem_post(&queue_p->full) == -1) {
    return SEMAPHORE_ERROR;
  }
  return 1;
}

int disconnect(server_queue *queue_p) {
  // Libère la projection mémoire
  if (munmap(queue_p, sizeof(server_queue)) < 0) {
    return SHM_ERROR;
  }

  return 1;
}

int free_server_queue(server_queue *queue_p) {
  // Ferme le fichier
  if (close(queue_p->shm_fd) < 0) {
    return SHM_ERROR;
  }
  // Libère la projection mémoire
  if (munmap(queue_p, sizeof(server_queue)) < 0) {
    return SHM_ERROR;
  }
  // Supprime le fichier shm
  if (shm_unlink(SHM_NAME) < 0) {
    return SHM_ERROR;
  }

  return 1;
}

/*
 * Manipulation de la requête de connexion au serveur.
 */

int send_shm_request(server_queue *server_q, const char request_pipe_name[], 
    const char response_pipe_name[]) {
  if (server_q == NULL) {
    return INVALID_POINTER;
  }
  // Si le serveur est plein on annule la requête
  if (server_q->length == MAX_SLOT) {
    return SERVER_IS_FULL;
  }
  // Attend au cas où la file soit en train d'être modifiée.
  if (sem_wait(&server_q->empty) == -1) {
    return SEMAPHORE_ERROR;
  }
  if (sem_wait(&server_q->mutex) == -1) {
    return SEMAPHORE_ERROR;
  }
  // Créé la requête à la volée
  shm_request request = {
    .request_pipe = "",
    .response_pipe = ""
  };
  // Remplit la requête en copiant les informations passées en paramètre
  strncpy(request.request_pipe, request_pipe_name, NAME_MAX);
  strncpy(request.response_pipe, response_pipe_name, NAME_MAX);
  // Ajoute la requête à la file des connexions entrantes
  server_q->buffer[server_q->head] = request;
  server_q->head = (server_q->head + 1) % MAX_SLOT;
  server_q->length += 1;
  // Donne le feu vert aux autres processus
  if (sem_post(&server_q->mutex) == -1) {
    return SEMAPHORE_ERROR;
  }
  if (sem_post(&server_q->full) == -1) {
    return SEMAPHORE_ERROR;
  }

  return 1;
}

int fetch_shm_request(server_queue *server_q, int (*apply)(shm_request *)) {
  if (server_q == NULL || apply == NULL) {
    return INVALID_POINTER;
  }
  // Attend que les autres processus finissent le traitement d'une réponse
  if (sem_wait(&server_q->full) == -1) {
    return SEMAPHORE_ERROR;
  }
  if (sem_wait(&server_q->mutex) == -1) {
    return SEMAPHORE_ERROR;
  }
  // Applique la fonction apply sur la requête la plus ancienne
  int r = apply(&server_q->buffer[server_q->tail]);
  // Change le pointeur de queue de la file
  server_q->tail = (server_q->tail + 1) % MAX_SLOT;
  server_q->length -= 1;
  // Donne le feu vert aux autres processus
  if (sem_post(&server_q->mutex) == -1) {
    return SEMAPHORE_ERROR;
  }
  if (sem_post(&server_q->empty) == -1) {
    return SEMAPHORE_ERROR;
  }

  return r;
}

/*
 * Manipulation de la requête à écrire sur le tube.
 */

struct request {
  char cmd[MAX_COMMAND_LENGTH + 1]; 
};

int send_request(const char *id, const char *cmd) {
  if (id == NULL || cmd == NULL) {
    return INVALID_POINTER;
  }
  // Créé le tube s'il n'existe pas déjà et ignore l'erreur si le 
  // fichier existe
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    if (errno != EEXIST) {
      return PIPE_ERROR;
    }
    errno = 0;
  }
  // Ouvre le tube en écriture
  int pipe_fd;
  if ((pipe_fd = open(id, O_WRONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Créé la requête
  request req = { .cmd = "" };
  strncpy(req.cmd, cmd, MAX_COMMAND_LENGTH);
  // Envoie la requête
  if (write(pipe_fd, &req, sizeof(request)) < 0) {
    perror("write ");
    return PIPE_ERROR;
  }
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}

int listen_request(const char *id, char *buffer) {
  if (id == NULL || buffer == NULL) {
    return INVALID_POINTER;
  }
  // Créé le tube s'il n'existe pas déjà et ignore l'erreur si le 
  // fichier existe
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    if (errno != EEXIST) {
      return PIPE_ERROR;
    }
    errno = 0;
  }
  // Ouvre le tube de requête
  int pipe_fd;
  if ((pipe_fd = open(id, O_RDONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Lit la requête
  request req;
  ssize_t n;
  if ((n = read(pipe_fd, &req, sizeof(request))) < 0) {
    return PIPE_ERROR;
  }
  // Copie la commande à éxecuter dans le buffer
  strncpy(buffer, req.cmd, MAX_COMMAND_LENGTH + 1);
  // Ferme et supprime le tube
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }
  if (unlink(id) < 0) {
    return PIPE_ERROR;
  }
  
  return 1;
}

/*
 * Manipulation de la réponse à écrire sur le tube.
 */

struct response {
  char msg[MAX_RESPONSE_LENGTH + 1];
};

int send_response(const char *id, const char *msg) {
  if (id == NULL || msg == NULL) {
    return INVALID_POINTER;
  }
  // Créé le tube s'il n'existe pas déjà et ignore l'erreur si le 
  // fichier existe
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    if (errno != EEXIST) {
      return PIPE_ERROR;
    }
    errno = 0;
  }
  // Ouvre le tube en écriture
  int pipe_fd;
  if ((pipe_fd = open(id, O_WRONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Créé la réponse
  response res = { .msg = "" };
  strncpy(res.msg, msg, MAX_RESPONSE_LENGTH);
  // Envoie la réponse
  if (write(pipe_fd, &res, sizeof(response)) < 0) {
    perror("write ");
    return PIPE_ERROR;
  }
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}

int listen_response(const char *id, char *buffer) {
  if (id == NULL || buffer == NULL) {
    return INVALID_POINTER;
  }
  // Créé le tube s'il n'existe pas déjà et ignore l'erreur si le 
  // fichier existe
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    if (errno != EEXIST) {
      return PIPE_ERROR;
    }
    errno = 0;
  }
  // Ouvre le tube de réponse
  int pipe_fd;
  if ((pipe_fd = open(id, O_RDONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Lit la réponse
  response res;
  ssize_t n;
  if ((n = read(pipe_fd, &res, sizeof(response))) < 0) {
    return PIPE_ERROR;
  }
  // Copie le résultat de la commande dans le buffer
  strncpy(buffer, res.msg, MAX_RESPONSE_LENGTH + 1);
  // Ferme et supprime le tube
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }
  if (unlink(id) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}
