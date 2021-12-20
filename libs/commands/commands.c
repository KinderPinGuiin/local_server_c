#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <wait.h>
#include "commands.h"

/**
 * Les types possibles des commandes
 */
#define INVALID_CMD 0
#define USUAL_CMD 1
#define CUSTOM_CMD 2

/**
 * Les différentes commandes disponibles
 */
static const char *COMMANDS[] = {
  // Valeur particulière pour le retour de is_command_available
  NULL,
  // Commandes usuelles
  "ls", "ps", "pwd", "exit",
  // Commandes personnalisées
  "info", "ccp", "lsl"
};

/**
 * Type de COMMANDS[i] pour tout i allant de 0 à |COMMAND|.
 */
static const int TYPES[] = {
  INVALID_CMD,
  USUAL_CMD, USUAL_CMD, USUAL_CMD, USUAL_CMD,
  CUSTOM_CMD, CUSTOM_CMD, CUSTOM_CMD
};

/*
 * Fonctions de traitement des commandes personnalisées.
 */

static int exec_info(const char **argv);
static int exec_ccp(const char **argv);
static int exec_lsl(const char **argv);

/**
 * Fonctions de COMMANDS[i] pour tout i allant de 0 à |COMMAND|.
 */
static int (* FUNCTIONS[])(const char **) = {
  NULL,
  NULL, NULL, NULL, NULL,
  exec_info, exec_ccp, exec_lsl
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
    "    - \033[0;36mlsl\033[0m : Commande raccourcie de ls -li.\n"
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
  // On cherche si ce préfixe est valide
  for (size_t i = 1; i < sizeof(COMMANDS) / sizeof(char *); ++i) {
    if (strcmp(prefix, COMMANDS[i]) == 0) {
      return (int) i;
    }
  }

  return 0;
}

int exec_cmd(const char *cmd) {
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
    return FUNCTIONS[cmd_id]((const char **) tokens);
  }
        
  return 1;
}

static int exec_info(const char **argv) {
  return fprintf(stdout, "Commande custom : %s\n", argv[0]);
}

static int exec_lsl(const char **argv) {
  return fprintf(stdout, "Commande custom : %s\n", argv[0]);
}

static int exec_ccp(const char **argv) {
  return fprintf(stdout, "Commande custom : %s\n", argv[0]);
}