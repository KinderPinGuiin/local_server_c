#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"

/**
 * Les différentes commandes disponibles
 */
static const char *COMMANDS[] = {
  // Commandes usuelles
  "ls", "ps", "pwd", "exit",
  // Commandes personnalisées
  "info", "ccp", "lsl"
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
  for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(char *); ++i) {
    if (strcmp(prefix, COMMANDS[i]) == 0) {
      return 1;
    }
  }

  return 0;
}