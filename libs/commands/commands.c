#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>
#include "commands.h"
#include "../connection/connection.h"

/**
 * Les types possibles des commandes
 */
#define INVALID_CMD 0
#define USUAL_CMD 1
#define CUSTOM_CMD 2

/*
 * Variables externes
 */

extern char *optarg;
extern int optind, optopt;
extern int errno;

/**
 * Les différentes commandes disponibles
 */
static const char *COMMANDS[] = {
  // Valeur particulière pour le retour de is_command_available
  NULL,
  // Commandes usuelles
  "ls", "ps", "pwd", "rm", "exit",
  // Commandes personnalisées
  "help", "info", "ccp", "lsl", "uinfo"
};

/**
 * Type de COMMANDS[i] pour tout i allant de 0 à |COMMAND|.
 */
static const int TYPES[] = {
  INVALID_CMD,
  USUAL_CMD, USUAL_CMD, USUAL_CMD, USUAL_CMD, USUAL_CMD,
  CUSTOM_CMD, CUSTOM_CMD, CUSTOM_CMD, CUSTOM_CMD, CUSTOM_CMD
};

/*
 * Fonctions de traitement des commandes personnalisées.
 */

static int exec_help(shm_request *shm_req, size_t argc, const char **argv);
static int exec_info(shm_request *shm_req, size_t argc, const char **argv);
static int exec_ccp(shm_request *shm_req, size_t argc, const char **argv);
static int exec_lsl(shm_request *shm_req, size_t argc, const char **argv);

/**
 * Fonctions de COMMANDS[i] pour tout i allant de 0 à |COMMAND|.
 */
static int (* FUNCTIONS[])(shm_request *, size_t, const char **) = {
  NULL,
  NULL, NULL, NULL, NULL,
  exec_help, exec_info, exec_ccp, exec_lsl
};

void print_commands() {
  fprintf(stdout,
    "Liste des commandes usuelles disponibles :\n"
    "    - \033[0;36mls ...\033[0m : Toutes les variantes de ls.\n"
    "    - \033[0;36mps ...\033[0m : Toutes les variantes de ps.\n"
    "    - \033[0;36mpwd ... \033[0m : Toutes les variantes de pwd.\n"
    "    - \033[0;36mexit\033[0m : Permet de se déconnecter du "
      "serveur.\n"
    "Liste des commandes personnalisées disponibles :\n"
    "    - \033[0;36minfo <PID>\033[0m : Affiche sur la sortie standard les "
      "informations concernant le processus de numéro PID.\n"
    "    - \033[0;36mccp <src> <dest> -[v|a|b|e]\033[0m : Copie le fichier src "
      "dans le fichier dest. -v permet de vérifier si le fichier existe déjà, "
      "-a permet de copier en mode ajout, -b et -e permettent respectivement "
      "de définir un offset de début et de fin.\n"
    "    - \033[0;36mlsl\033[0m : Commande raccourcie de ls -ali.\n"
  );
}

int is_command_available(const char *cmd) {
  if (cmd == NULL) {
    return INVALID_POINTER_COMMANDS;
  }
  // On récupère la prefixe de la commande à exécuter
  char cmd_cpy[strlen(cmd) + 1];
  strcpy(cmd_cpy, cmd);
  char *prefix = strtok(cmd_cpy, " ");
  if (prefix == NULL) {
    return 0;
  }
  // On cherche si ce préfixe est valide
  for (size_t i = 1; i < sizeof(COMMANDS) / sizeof(char *); ++i) {
    if (strcmp(prefix, COMMANDS[i]) == 0) {
      return (int) i;
    }
  }

  return 0;
}

int exec_cmd(const char *cmd, shm_request *shm_req) {
  int cmd_id;
  if (!(cmd_id = is_command_available(cmd))) {
    return INVALID_COMMAND;
  }
  // Construit le tableau des arguments de la commande 
  char cmd_cpy[strlen(cmd) + 1];
  strcpy(cmd_cpy, cmd);
  char *cmd_p = cmd_cpy;
  char *tokens[strlen(cmd) + 1];
  char *token;
  size_t i = 0;
  while ((token = strtok_r(cmd_p, " ", &cmd_p))) {
    tokens[i] = token;
    ++i;
  }
  tokens[i] = NULL;
  if (TYPES[cmd_id] == USUAL_CMD) {
    // Utilise execvp en cas de commande usuelle
    switch (fork()) {
      case -1:
        return EXEC_ERROR;
      case 0:
        execvp(tokens[0], tokens);
        return EXEC_ERROR;
      case 1:
        wait(NULL);
    }
  } else {
    // Execute la fonction correspondante à la commande personnalisée
    return FUNCTIONS[cmd_id](shm_req, i, (const char **) tokens);
  }
        
  return 1;
}

// ---------- Commande : help ----------

static int exec_help(shm_request *shm_req, size_t argc, const char **argv) {
  if (shm_req && argc && argv[0]) { /* Enlève le warn à la compilation */ }
  print_commands();
  return 1;
}

// ---------- Commande : info ----------

#define PID_NB_NUMBER 7
#define LINE_MAX_LENGTH 255

static int exec_info(shm_request *shm_req, size_t argc, const char **argv) {
  char pid[PID_NB_NUMBER + 1];
  if (argc < 2) {
    snprintf(pid, PID_NB_NUMBER, "%d", shm_req->pid);
  } else {
    strncpy(pid, argv[1], PID_NB_NUMBER);
  }
	fprintf(stdout, "----- Caractéristiques du programme %s -----\n", pid);

	/*
	 * cmdline
	 */
	
	// Créé le nom de fichier à trouver pour cmdline
	char *str = malloc(sizeof("/proc//cmdline") + strlen(pid) * sizeof(char));
	if (str == NULL) {
		fprintf(stdout, "Pas assez d'espace mémoire\n");
		return EXEC_ERROR;
	}
	sprintf(str, "/proc/%s/cmdline", pid);
	// Lit le fichier
	FILE *cmd = fopen(str, "r");
	if (cmd == NULL) {
		free(str);
		fprintf(stdout, "Impossible d'ouvrir le fichier cmdline\n");
		return EXEC_ERROR;
	}
	char line[LINE_MAX_LENGTH + 1];
	if (fgets(line, LINE_MAX_LENGTH, cmd) == NULL) {
    fprintf(stdout, 
        "Une erreur est survenue lors de la lecture du fichier\n");
  }
	if (line == NULL) {
		free(str);
		fprintf(stdout, "Impossible de lire le fichier cmdline\n");
		return EXEC_ERROR;
	}
	// Affiche la commande ayant executé le fichier
	fprintf(stdout, "[%s] Command : %s\n", pid, line);
	fclose(cmd);
	free(str);
	
	/*
	 * status
	 */
	str = malloc(sizeof("/proc//status") + strlen(pid) * sizeof(char));
	if (str == NULL) {
		fprintf(stdout, "Pas assez d'espace mémoire\n");
		return EXEC_ERROR;
	}
	sprintf(str, "/proc/%s/status", pid);
	FILE *status = fopen(str, "r");
	if (status == NULL) {
		free(str);
		fprintf(stdout, "Impossible d'ouvrir le fichier status\n");
		return EXEC_ERROR;
	}
	for (size_t i = 0; i < 7; ++i) {
		if (fgets(line, LINE_MAX_LENGTH, status) == NULL) {
      fprintf(stdout, 
          "Une erreur est survenue lors de la lecture du fichier\n");
    }
		if (line == NULL) {
			free(str);
			fclose(status);
			fprintf(stdout, "Impossible de lire le fichier status\n");
			return EXEC_ERROR;
		}
		switch(i) {
			case 2:
			case 3:
			case 6:
				fprintf(stdout, "[%s] %s", pid, line);
				break;
			default:
				break;
		}
	}
	free(str);
	fclose(status);

  return 1;
}

// ---------- Commande : lsl ----------

// Macro utilisée dans la fonction print_file_info.
#define FILE_NOT_FOUND -1

// Macro utilisée dans la fonction print_file_info
#define PRINT_ERROR -2

// Taille de la chaine des droits obtenu par strmode.
#define MODE_STR_LENGTH 10

// Nombre de types de fichiers différents. Utilisé par strmode. 
#define NB_FILE_TYPES 7

// Nombre maximum de caractères pour la date de modification.
#define MAX_MODIF_STR_SIZE 50

// Taille d'un code couleur dans le terminal. Utilisé dans get_color.
#define COLOR_SIZE 9

/*
 * Ecrit les informations d'un fichier sur la sortie standard. La liste de ses
 * informations est donnée sur la question 1 du TP6. Renvoie 0 si tout
 * se passe bien et FILE_NOT_FOUND si le fichier est introuvable.
 */
static int print_file_info(const char *filepath);

/*
 * Ecrit la représentation textuelle du mode dans buffer. Ecrit au maximum
 * n caractères. Renvoie 0 en cas de succès, 1 si la totalité du mode n'a pas pu
 * être écrite. Renvoie -1 si n < 1.
 */
static int strmode(mode_t mode, char *buffer, size_t n);

/*
 * Met la couleur à utiliser pour le fichier de type t dans la chaine buffer
 * de longueur n. Ne remplit pas la chaine si il n'y a pas assez d'espace.
 */
static void get_color(char *buffer, size_t n, char t);

static int exec_lsl(shm_request *shm_req, size_t argc, const char **argv) {
  if (shm_req) { /* Enlève le warn à la compilation */ }
  char dir_path[PATH_MAX + 1];
  // Détermine le dossier sur lequel éxecuter 
  if (argc == 1) {
    strncpy(dir_path, ".", PATH_MAX);
  } else {
    if (argv[1][strlen(argv[1]) - 1] == '/') {
      strncpy(dir_path, argv[1], PATH_MAX + 1);
    } else {
      strncpy(dir_path, argv[1], PATH_MAX);
      strcat(dir_path, "/");
      // Vérifie que le dossier n'ai pas été tronqué
      if (dir_path[PATH_MAX] != '\0') {
        fprintf(stdout, "Erreur : Le chemin spécifié est trop long\n");
        return EXEC_ERROR;
      }
    }
  }
  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    perror("Impossible d'ouvrir le dossier ");
    return EXEC_ERROR;
  }
  errno = 0;
  struct dirent *entry;
  int r = 1;
  // Multiplie la longueur par 2 afin de ne pas avoir de seg fault
  // lors du strncat.
  char fullname[2 * PATH_MAX + 1];
  while ((entry = readdir(dir)) != NULL) {
    // Ignore les dossier . et ..
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      // Concatène le dossier au fichier afin d'avoir le nom complet
      strcpy(fullname, (argc > 1) ? dir_path : "");
      strncat(fullname, entry->d_name, PATH_MAX + 1);
      if (strlen(fullname) > PATH_MAX + 1) {
        fprintf(stdout, "Erreur : Chemin invalide\n");
        r = EXEC_ERROR;
        goto close;
      }
      if (print_file_info(fullname) == FILE_NOT_FOUND) {
        fprintf(stdout, "Erreur : Fichier innatendu %s\n", fullname);
        r = EXEC_ERROR;
        goto close;
      }
    }
  }
  if (errno != 0) {
    perror("Erreur lors de la lecture ");
    r = EXEC_ERROR;
    goto close;
  }
close:
  if (closedir(dir) == -1) {
    perror("Erreur lors de la fermeture du dossier ");
    return EXEC_ERROR;
  }

  return r;
}

static int print_file_info(const char *filepath) {
  struct stat stats;
  if (lstat(filepath, &stats) < 0) {
    return FILE_NOT_FOUND;
  }
  char mode_str[MODE_STR_LENGTH + 1];
  if (strmode(stats.st_mode, mode_str, MODE_STR_LENGTH + 1) != 0) {
    fprintf(stdout, "Warning : Mode tronqué\n");
  }
  // Infos utilisateur et groupe
  struct passwd *user_info = getpwuid(stats.st_uid);
  struct group *group_info = getgrgid(stats.st_gid);
  // Formate la date
  char last_modif[MAX_MODIF_STR_SIZE + 1];
  size_t modif_writed = strftime(
    last_modif, MAX_MODIF_STR_SIZE + 1, 
    "%b.  %d %R", gmtime(&stats.st_mtim.tv_sec)
  );
  if (modif_writed == 0) {
    fprintf(stdout, "Erreur : Impossible d'écrire la date de dernière "
        "modification\n");
    return PRINT_ERROR;
  }
  last_modif[0] = (char) tolower(last_modif[0]);
  // Détermine la couleur du fichier
  char color[COLOR_SIZE + 1];
  get_color(color, COLOR_SIZE + 1, mode_str[0]);
  // Affichage
  fprintf(
    stdout, 
    "%-8lu %s %-4lu %-8s %-8s %-10lu %s %s%s\033[0m\n",
    stats.st_ino, mode_str, stats.st_nlink, user_info->pw_name, 
    group_info->gr_name, stats.st_size, last_modif, color, filepath
  );
  
  return 0;
}

static int strmode(mode_t mode, char *buffer, size_t n) {
  if (n < 1) {
    return -1;
  }
  
  /*
   * Détermine le type de fichier
   */
  
  int filetype[NB_FILE_TYPES] = {
    S_ISREG(mode), S_ISDIR(mode), S_ISCHR(mode), S_ISBLK(mode),
    S_ISFIFO(mode), S_ISLNK(mode), S_ISSOCK(mode)
  };
  const char *filetypes_chars = "-dcbpls";
  for (size_t i = 0; i < NB_FILE_TYPES && i < n - 1; ++i) {
    if (filetype[i]) {
      buffer[0] = filetypes_chars[i];
      break;
    }
  }
  
  /*
   * Détermine les droits du fichier
   */
  
  // On retire 1 car le premier caractère de la chaine est le type du fichier
  int mode_masks[MODE_STR_LENGTH - 1] = {
    // Masques droits utilisateurs
    S_IRUSR, S_IWUSR, S_IXUSR,
    // Masques droits groupes
    S_IRGRP, S_IWGRP, S_IXGRP,
    // Masques droits autres
    S_IROTH, S_IWOTH, S_IXOTH
  };
  const char *mode_chars = "rwx";
  for (size_t i = 1; i < n && i < 9; ++i) {
    // Cas particulier : Le fichier est un lien symbolique. Si tel est le cas,
    // conformément  au man le lien doit avoir les permissions 777.
    // Voir : https://man7.org/linux/man-pages/man7/symlink.7.html
    // (D'où deuxième partie de la condition)
    if (((int) mode & mode_masks[i - 1]) > 0 || buffer[0] == 'l') {
      buffer[i] = mode_chars[(i - 1) % 3];
    } else {
      buffer[i] = '-';
    }
  }
  buffer[n - 1] = '\0';
  
  return (n < MODE_STR_LENGTH + 1) ? 1 : 0;
}

static void get_color(char *buffer, size_t n, char t) {
  if (n < COLOR_SIZE + 1) {
    return;
  }
  switch (t) {
    case 'd':
      strncpy(buffer, "\033[1;34m", COLOR_SIZE + 1);
      return;
    case 'l':
      strncpy(buffer, "\033[1;36m", COLOR_SIZE + 1);
      return;
    case 'b':
    case 'c':
    case 'p':
      strncpy(buffer, "\033[1;33m", COLOR_SIZE + 1);
      return;
    case 's':
      strncpy(buffer, "\033[1;45m", COLOR_SIZE + 1);
      return;
  }
  buffer[0] = '\0';
}

// ---------- Commande : ccp ----------

// Détermine la position courante du fichier lié au descripteur x.
#define TELL(x) (lseek(x, 0, SEEK_CUR))

// Détermine la taille du fichier lié au descripteur x
#define SIZE(x) (lseek(x, 0, SEEK_END));

// Replace le curseur du fichier lié au descripteur x au début
#define RESET(x) (lseek(x, 0, SEEK_CUR));

#define READ_ERROR -1
#define WRITE_ERROR -2

/*
 * Copie-colle le fichier de descripteur fd_src dans fd_dest jusqu'à max.
 * Renvoie 0 si tout s'est bien passé, READ_ERROR en cas d'erreur de lecture,
 * WRITE_ERROR en cas d'erreur d'écriture. Si max vaut -1 alors on copiera
 * tout le fichier.
 */
static int ccp(int fd_src, int fd_dest, long max);

static int exec_ccp(shm_request *shm_req, size_t argc, const char **argv) {
  if (shm_req) { /* Enlève le warn */ }
	if (argc == 1) {
		fprintf(stderr, 
        "Arguments manquants, tapez help pour plus d'information\n");
		return EXEC_ERROR;
	}
	// Analyse des arguments
	char *src_file;
	char *dest_file;
	int src_mode = 0;
	int dest_mode = O_TRUNC;
	long bvalue = 0;
	long evalue = -1;	
	char c;
	while (
    (c = (char) getopt((int) argc, (char *const *) argv, "f:d:vab:e:")) != -1
  ) {
		switch (c) {
			case 'f':
				src_file = optarg;
				break;
			case 'd':
				dest_file = optarg;
				break;
			case 'v':
				src_mode = O_EXCL;
				break;
			case 'a':
				dest_mode = O_APPEND;
				break;
			case 'b':
				bvalue = atol(optarg);
				if (bvalue < 0) {
					fprintf(stderr, "Value of -b must be positive.\n");
					return EXEC_ERROR;
				}
				break;
			case 'e':
				evalue = atol(optarg);
				if (evalue < bvalue) {
					fprintf(stderr, 
              "Value of -e must be positive and greater than -b.\n");
					return EXEC_ERROR;
				}
				break;
			case '?':
				if (optopt == 'f' || optopt == 'd' || optopt == 'b' || optopt == 'e') {
					fprintf(stderr, "Option -%c need value.\n", optopt);
				} else if (isprint(optopt)) {
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				} else {
					fprintf(stderr, "Unknown option, use -h for help");
				}
				return EXEC_ERROR;
				break;
			default:
				abort();
		}
	}
	// Ouvre le fichier source et seek
	int src_fd;
	if ((src_fd = open(src_file, O_CREAT | src_mode, S_IRWXU)) == -1) {
		fprintf(stderr, "Cannot open %s.\n", src_file);
		return EXEC_ERROR;
	}
	if (lseek(src_fd, (off_t) bvalue, SEEK_SET) == -1) {
		fprintf(stderr, "Cannot seek file to value %ld", bvalue);
		close(src_fd);
		return EXEC_ERROR;
	}
	// Ouvre le fichier de destination
	int dest_fd;
	if (
    (dest_fd = open(dest_file, O_CREAT | O_WRONLY | dest_mode, S_IRWXU)) == -1
  ) {
		fprintf(stderr, "Cannot open %s.\n", dest_file);
		close(src_fd);
		return EXEC_ERROR;
	}
	// Lance la copie
	int ccp_r = ccp(src_fd, dest_fd, (off_t) evalue);
	if (ccp_r != 0) {
		fprintf(stderr, 
		  (ccp_r == READ_ERROR) ? "Read error.\n" : "Write error.\n");
		close(src_fd);
		close(dest_fd);
		return EXEC_ERROR;
	}
	
	return 1;
}

static int ccp(int fd_src, int fd_dest, off_t max) {
	char buffer[1];
	ssize_t readed;
	while (max == -1 || TELL(fd_dest) != max) {
		if ((readed = read(fd_src, buffer, 1)) == -1) {
			return READ_ERROR;
		} else if (readed == 0) {
			break;
		}
		if (write(fd_dest, buffer, 1) == -1) {
			return WRITE_ERROR;
		}
	}
	
	return 0;
}

// ---------- Commande : uinfo ----------

