#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
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

#define MIN(x, y) (x < y ? x : y)

/*
 * Manipulation de la queue de connexion au serveur
 */

struct server_queue {
  int shm_fd;
  size_t nb_slots;
  sem_t mutex;
  sem_t empty;
  sem_t full;
  size_t length; // Le nombre d'éléments dans le tampon
  size_t head;   // Position d'ajout dans le tampon
  size_t tail;   // Position de suppression dans le tampon
  shm_request buffer[];
};


server_queue *init_server_queue(size_t max_slot) {
  // Création du SHM
  int shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL,
      S_IRUSR | S_IWUSR);
  if (shm_fd < 0) {
    return NULL;
  }
  if (ftruncate(shm_fd, 
      (off_t) (sizeof(server_queue) + sizeof(shm_request) * max_slot)) < 0) {
    goto err;
  }
  server_queue *server_q = mmap(NULL, 
      sizeof(server_queue) + sizeof(shm_request) * max_slot, 
      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (server_q == MAP_FAILED) {
    goto err;
  }
  // Remplissage de la mémoire
  server_q->shm_fd = shm_fd;
  if (sem_init(&server_q->mutex, 1, 1) == -1) {
    return NULL;
  }
  if (sem_init(&server_q->empty, 1, (unsigned int) max_slot) == -1) {
    return NULL;
  }
  if (sem_init(&server_q->full, 1, 0) == -1) {
    return NULL;
  }
  server_q->nb_slots = max_slot;
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
  // Effectue une première projection afin de connaître le nombre de slots
  server_queue *server_q = mmap(NULL, sizeof(server_queue), 
      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (server_q == MAP_FAILED) {
    if (close(shm_fd) < 0) {
      return NULL;
    }

    return NULL;
  }
  size_t nb_slots = server_q->nb_slots;
  if (munmap(server_q, sizeof(server_queue)) < 0) {
    return NULL;
  }
  // Effectue la projection complète
  server_q = mmap(NULL, sizeof(server_queue) + sizeof(shm_request) * nb_slots, 
      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (server_q == MAP_FAILED) {
    if (close(shm_fd) < 0) {
      return NULL;
    }

    return NULL;
  }
  // Ferme le descripteur car il ne sera plus utile après
  if (close(shm_fd) < 0) {
    return NULL;
  }

  return server_q;
}

int disconnect(server_queue *queue_p) {
  // Libère la projection mémoire
  if (munmap(queue_p, 
      sizeof(server_queue) + sizeof(shm_request) * queue_p->nb_slots) < 0) {
    return SHM_ERROR;
  }

  return 1;
}

int free_server_queue(server_queue *queue_p) {
  // Attend la fin des modifications
  if (sem_wait(&queue_p->mutex) == -1) {
    return SEMAPHORE_ERROR;
  }
  // Ferme le fichier
  if (close(queue_p->shm_fd) < 0) {
    return SHM_ERROR;
  }
  // Libère la projection mémoire
  if (munmap(queue_p, 
      sizeof(server_queue) + sizeof(shm_request) * queue_p->nb_slots) < 0) {
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
    .response_pipe = "",
    .pid = getpid(),
    .uid = getuid()
  };
  // Remplit la requête en copiant les informations passées en paramètre
  strncpy(request.request_pipe, request_pipe_name, NAME_MAX);
  strncpy(request.response_pipe, response_pipe_name, NAME_MAX);
  // Ajoute la requête à la file des connexions entrantes
  server_q->buffer[server_q->head] = request;
  server_q->head = (server_q->head + 1) % server_q->nb_slots;
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
  server_q->tail = (server_q->tail + 1) % server_q->nb_slots;
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

struct request_fifo {
  char id[NAME_MAX + 1];
};

typedef struct request {
  char cmd[MAX_COMMAND_LENGTH + 1];
} request;

request_fifo *init_request_fifo(const char *id) {
  request_fifo *req = malloc(sizeof *req);
  if (req == NULL) {
    return NULL;
  }
  // Créé le tube
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    return NULL;
  }
  strncpy(req->id, id, NAME_MAX);

  return req;
}

int send_request(request_fifo *req_fifo, const char *cmd) {
  if (req_fifo == NULL || cmd == NULL) {
    return INVALID_POINTER;
  }
  // Créé la requête
  request req = { .cmd = "" };
  strncpy(req.cmd, cmd, MAX_COMMAND_LENGTH);
  // Ouvre le tube du réseau
  int pipe_fd;
  if ((pipe_fd = open(req_fifo->id, O_WRONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Envoie la requête
  if (write(pipe_fd, &req, sizeof(request)) < 0) {
    return PIPE_ERROR;
  }
  // Ferme le tube
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}

int listen_request(const char *id, char *buffer) {
  if (id == NULL || buffer == NULL) {
    return INVALID_POINTER;
  }
  // Ouvre le tube
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
  // Ferme le tube
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }
  
  return 1;
}

int close_request_fifo(request_fifo *req) {
  if (unlink(req->id) < 0) {
    return PIPE_ERROR;
  }
  free(req);

  return 1;
}

/*
 * Manipulation de la réponse à écrire sur le tube.
 */

struct response_fifo {
  char id[NAME_MAX + 1];
};

typedef struct response {
  size_t size;
  char msg[];
} response;

response_fifo *init_response_fifo(const char *id) {
  response_fifo *res = malloc(sizeof *res);
  if (res == NULL) {
    return NULL;
  }
  // Créé le tube
  if (mkfifo(id, S_IRUSR | S_IWUSR) < 0) {
    return NULL;
  }
  strncpy(res->id, id, NAME_MAX);

  return res;
}

int send_response(const char *id, const char *msg, ssize_t max_size) {
  if (id == NULL || msg == NULL) {
    return INVALID_POINTER;
  }
  // Ouvre le tube en écriture
  int pipe_fd;
  if ((pipe_fd = open(id, O_WRONLY)) < 0) {
    return PIPE_ERROR;
  }
  // Crée la réponse
  size_t size = strlen(msg) + 1;
  if (max_size >= 0) {
    size = MIN(size, (size_t) max_size);
  }
  response *res = malloc(sizeof(*res) + size);
  if (res == NULL) {
    return MEMORY_ERROR;
  }
  res->size = size;
  for (size_t i = 0; i < res->size; ++i) {
    res->msg[i] = msg[i];
  }
  // Envoi la réponse
  ssize_t n;
  size_t total = 0;
  if ((n = write(pipe_fd, res, sizeof(size_t))) < 0) {
    free(res);
    return PIPE_ERROR;
  }
  sleep(6);
  while (
    (max_size < 0 || total < (size_t) max_size) &&
    (n = write(pipe_fd, &res->msg[total], res->size - total)) > 0
  ) {
    total += (size_t) n;
  }
  free(res);
  if (n < 0) {
    return PIPE_ERROR;
  }
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}

int listen_response(response_fifo *res_fifo, char **buffer, time_t timeout) {
  if (res_fifo == NULL) {
    return INVALID_POINTER;
  }
  // Ouvre le tube de réponse
  int pipe_fd;
  if ((pipe_fd = open(res_fifo->id, O_RDONLY | O_NONBLOCK)) < 0) {
    return PIPE_ERROR;
  }
  // Attend que le serveur ait écrit
  fd_set set;
  struct timeval tv;
  tv.tv_sec = timeout;
  FD_ZERO(&set);
  FD_SET(pipe_fd, &set);
  int ret = select(pipe_fd + 1, &set, NULL, NULL, &tv);
  if (ret < 0) {
    fprintf(stderr, "Select error\n");
    return PIPE_ERROR;
  } else if (ret == 0) {
    return 0;
  }
  // Lit la taille de la réponse
  size_t size;
  ssize_t n;
  if ((n = read(pipe_fd, &size, sizeof(size_t))) < 0) {
    return PIPE_ERROR;
  }
  response *res = malloc(sizeof(*res) + size);
  if (res == NULL) {
    return MEMORY_ERROR;
  }
  res->size = size;
  // Lit le contenu de la réponse
  size_t total = 0;
  while (total < size) {
    FD_ZERO(&set);
    FD_SET(pipe_fd, &set);
    ret = select(pipe_fd + 1, &set, NULL, NULL, &tv);
    if (ret < 0) {
      fprintf(stderr, "Select error\n");
      return PIPE_ERROR;
    } else if (ret == 0) {
      return 0;
    }
    if ((n = read(pipe_fd, &res->msg[total], size - total)) < 0) {
      free(res);
      return PIPE_ERROR;
    }
    total += (size_t) n;
  }
  // Copie le résultat de la commande dans le buffer
  *buffer = malloc(size);
  if (*buffer == NULL) {
    return MEMORY_ERROR;
  }
  strncpy(*buffer, res->msg, res->size);
  free(res);
  // Ferme et supprime le tube
  if (close(pipe_fd) < 0) {
    return PIPE_ERROR;
  }

  return 1;
}

int close_response_fifo(response_fifo *res) {
  if (unlink(res->id) < 0) {
    return PIPE_ERROR;
  }
  free(res);

  return 1;
}