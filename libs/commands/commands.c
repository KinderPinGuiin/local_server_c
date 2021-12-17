#include <stdio.h>
#include <stdlib.h>
#include "commands.h"

void print_commands() {
  fprintf(stdout,
    "Liste des commandes usuelles disponibles :\n"
    "    - \033[0;36mls ...\033[0m : Toutes les variantes de ls.\n"
    "    - \033[0;36mps ...\033[0m : Toutes les variantes de ps.\n"
    "    - \033[0;36mpwd ... \033[0m : Toutes les variantes de pwd.\n"
    "    - \033[0;36mexit\033[0m : Permet de se déconnecter du "
      "serveur.\n"
    "Liste des commandes personalisées disponibles :\n"
    "    - \033[0;36minfo <PID>\033[0m : Affiche sur la sortie standard les "
      "informations concernant le processus de numéro PID.\n"
    "    - \033[0;36mccp <src> <dest> -[v|a|b|e]\033[0m : Copie le fichier src "
      "dans le fichier dest. -v permet de vérifier si le fichier existe déjà, "
      "-a permet de copier en mode ajout, -b et -e permettent respectivement "
      "de définir un offset de début et de fin.\n"
    "    - \033[0;36mlsl\033[0m : Commande raccourcie de ls -li.\n"
  );
}