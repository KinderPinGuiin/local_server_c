/**
 * Interface permettant d'exécuter des commandes usuelles linux ainsi que des
 * commandes personnalisées. La liste des commandes disponibles peut-être 
 * affichée via la fonction print_commands.
 * 
 * @author Jordan ELIE
 */

#ifndef COMMANDS_H
#define COMMANDS_H

/*
 * Codes d'erreur
 */
#define INVALID_COMMAND -1
#define EXEC_ERROR -2
#define INVALID_POINTER_COMMANDS -3

/**
 * Affiche sur la sortie standard la liste des commandes pouvant être exécutées
 * par cette interface.
 */
void print_commands();

/**
 * Renvoie 1 si cmd est une commande valide et 0 sinon.
 * 
 * @param {char *} La commande.
 * @return {int} 1 si la commande est valide et 0 sinon.
 */
int is_command_available(const char *cmd);

/**
 * Execute la commande cmd si celle-ci est valide. Renvoie 1 en cas de succès 
 * et un nombre négatif en cas d'erreur.
 * 
 * @param {char *} La commande à exécuter.
 * @return {int} 1 en cas de succès et un nombre négatif en cas d'erreur.
 */
int exec_cmd(const char *cmd);


#endif