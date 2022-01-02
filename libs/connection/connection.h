/**
 * Interface de connexion permettant de créer et de se connecter à un serveur
 * thread safe utilisant de la mémoire partagée.
 * 
 * @author Jordan ELIE
 */

#ifndef CONNECTION_H
#define CONNECTION_H

#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

// Nom du SHM pour se mettre en liste d'attente sur le serveur
#define SHM_NAME "/shm_server_963852741"

// Taille maximale d'une commande
#define MAX_COMMAND_LENGTH 256

// Taille maximale d'un message de réponse
#define MAX_RESPONSE_LENGTH 4000

/*
 * Codes d'erreurs
 */

#define INVALID_POINTER -1
#define SHM_ERROR -2
#define SEMAPHORE_ERROR -3
#define SERVER_IS_FULL -4
#define PIPE_ERROR -5
#define MEMORY_ERROR -6

/*
 * Manipulation de la queue de connexion au serveur
 */

typedef struct server_queue server_queue;

/**
 * Alloue en mémoire partagée la file de connexion du serveur et la renvoie.
 * La file contiendra max_slot slots.
 * 
 * @param {size_t} Le nombre de slots de la file.
 * @return {server_queue *} Le pointeur vers la file du serveur ou NULL en cas
 *                          d'erreur. L'erreur peut-être récupérée avec perror.
 */
server_queue *init_server_queue(size_t max_slot);

/**
 * Etablit un lien avec la file de connexion du serveur shm_name et renvoie
 * un pointeur vers celle-ci.
 * 
 * @param {char *} Le nom du serveur auquel on souhaite se connecter.
 * @return {server_queue *} Le pointeur vers la file de connexion au serveur 
 *                          ou NULL en cas d'erreur. L'erreur peut-être
 *                          récupérée via perror.
 */
server_queue *connect(const char *shm_name);

/**
 * Déconnecte le client de la file pointée par queue_p en libérant les 
 * ressources mémoires occupés par celui-ci
 * 
 * @param {server_queue *} La file à libérer.
 * @return {int} 1 si tout se passe bien et un nombre négatif en cas d'erreur
 *               récupérable par perror.
 */
int disconnect(server_queue *queue_p);

/**
 * Libère toutes les ressources associées à la file pointée par server_queue.
 * 
 * @param {server_queue *} La file à libérer.
 * @return {int} 1 si tout se passe bien et un nombre négatif en cas d'erreur
 *               récupérable par perror.
 */
int free_server_queue(server_queue *queue_p);

/*
 * Manipulation de la requête de connexion au serveur.
 */

typedef struct shm_request {
  char request_pipe[NAME_MAX + 1];
  char response_pipe[NAME_MAX + 1];
  pid_t pid;
  uid_t uid;
} shm_request;

/**
 * Créé et envoie une requête au serveur pointé par server_q. Cette requête
 * contiendra les noms des pipes sur lesquels doit s'effectuer la 
 * requête / réponse.
 * 
 * @param {char[]} Le nom du tube de requête.
 * @param {char[]} Le nom du tube de réponse.
 * @return {int} 1 si tout se passe bien et une valeur négative sinon.
 *               Cette erreur peut-être récupérée via perror.
 */
int send_shm_request(server_queue *server_q, const char request_pipe_name[], 
    const char response_pipe_name[]);

/**
 * Récupère la requête la plus ancienne de la file des requêtes et execute la
 * fonction apply en passant cette requête en paramètre. À la fin de 
 * l'exécution de apply, la requête la plus ancienne est supprimée de la file.
 * 
 * @param {server_queue *} La file sur laquelle récupérer la requête.
 * @param {int (*apply)} La fonction à appliquer sur la requête récupérée.
 * @return {int} Le retour de apply si tout se passe bien et une valeur 
 *               négative sinon. L'erreur peut-être récupérée via perror.
 */
int fetch_shm_request(server_queue *server_q, int (*apply)(shm_request *));

/*
 * Manipulation de la requête à écrire sur le tube.
 */

typedef struct request_fifo request_fifo;

/**
 * Créé le réseau de requête de nom unique id et le renvoie
 * 
 * @param {char *} Le nom unique du réseau à créer.
 * @return {request_fifo *} Un pointeur vers la requête ou NULL en cas 
 *                          d'erreur. L'erreur peut être consultée via 
 *                          perror.
 */
request_fifo *init_request_fifo(const char *id);

/**
 * Créé une requête contenant la commande cmd, qui sera envoyée sur le réseau
 * de requête req.
 * 
 * @param {request_fifo *} Le réseau de requête.
 * @param {char *} La commande que doit éxecuter le serveur.
 * @return {int} 1 en cas de succès et une valeur négative en cas d'erreur.
 *               Cette erreur pourra être récupérée via perror.
 */
int send_request(request_fifo *req_fifo, const char *cmd);

/**
 * Ecoute la requête envoyée par le client et stock la commande à exécuter dans
 * buffer.
 * 
 * @param {char *} L'identifiant du réseau de requêtes.
 * @param {char *} Une chaîne où stocker la commande à exécuter.
 * @return {int} 1 en cas de succès et une valeur négative en cas d'erreur.
 *               Cette erreur pourra être récupérée via perror.
 */
int listen_request(const char *id, char *buffer);

/**
 * Ferme la file de requêtes associée à *req. Renvoie 1 en cas de succès
 * et un nombre négatif en cas d'échec. L'erreur peut être consultée via 
 * perror.
 *
 * @param {request_fifo *} Le réseau à fermer.
 * @return {int} 1 en cas de succès et un nombre négatif en cas d'échec.
 */
int close_request_fifo(request_fifo *req);

/*
 * Manipulation de la réponse à écrire sur le tube.
 */

typedef struct response_fifo response_fifo;

/**
 * Créé le tube de réponse de nom unique id et le renvoie.
 * 
 * @param {char *} Le nom unique du tube à créer.
 * @return {response_fifo *} Un pointeur vers la file de réponse ou NULL en cas
 *                           d'erreur. L'erreur peut être consultée via 
 *                           perror.
 */
response_fifo *init_response_fifo(const char *id);

/**
 * Créé une réponse qui sera envoyée au client après avoir établit le lien
 * avec celui-ci.
 * 
 * @param {char *} L'identifiant unique de la réponse.
 * @param {char *} Le message à envoyer.
 * @return {int} 1 en cas de succès et une valeur négative en cas d'erreur.
 *               Cette erreur pourra être récupérée via perror.
 */
int send_response(const char *id, const char *msg, ssize_t max_size);

/**
 * Ecoute la réponse envoyée par le serveur et stock son contenu dans buffer.
 * 
 * @param {char *} L'identifiant unique de la requête à écouter.
 * @param {char *} Une chaîne où stocker la commande à exécuter.
 * @param {time_t} Un timeout en cas de non réponse.
 * @return {int} 1 en cas de succès et une valeur négative en cas d'erreur.
 *               Cette erreur pourra être récupérée via perror.
 */
int listen_response(response_fifo *res_fifo, char **buffer, time_t timeout);

/**
 * Ferme la file de réponse associée à *req. Renvoie 1 en cas de succès
 * et un nombre négatif en cas d'échec. L'erreur peut être consultée via 
 * perror.
 *
 * @param {response_fifo *} La file à fermer.
 * @return {int} 1 en cas de succès et un nombre négatif en cas d'échec.
 */
int close_response_fifo(response_fifo *res);

#endif