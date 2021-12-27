/**
 * Implémentation de listes chaînées génériques simples thread-safe.
 * 
 * @author Jordan ELIE.
 */

#ifndef LIST_H
#define LIST_H

/**
 * Structure de liste pouvant être manipulées via les différentes fonctions
 * de cette interface.
 */
typedef struct list list;

/**
 * Alloue dynamiquement et retourne un pointeur vers une liste. Retourne NULL 
 * en cas de mémoire indisponible. compar est la fonction qui permettra de
 * comparer les élements entre eux.
 * 
 * @param {int *(void *, void *)} La fonction de comparaison des élements.
 * @return {list *} Un pointeur vers la liste en cas de succès ou NULL s'il 
 *                  n'y a pas assez de mémoire disponible.
 */
list *init_list(int (*compar)(void *, void *));

/**
 * Ajoute dynamiquement elem à la liste pointée par p_list. Retourne l'élement
 * ajouté à la liste en cas de succès et NULL sinon.
 * 
 * @param {list *} La liste où l'on souhaite ajouter un élement.
 * @param {void *} La valeur à ajouter.
 * @param {size_t} La taille de l'élement à ajouter.
 * @return {void *} La valeur dans la liste en cas de succès et NULL sinon.
 */
void *list_add(list *list_p, void *elem, size_t elem_size);

/**
 * Retire la première occurence de elem dans la liste pointée par p_list.
 * Retourne 1 en cas de succès et un nombre négatif en cas d'erreur.
 * 
 * @param {list *} La liste où l'on souhaite ajouter un élement.
 * @param {void *} La valeur à supprimer.
 * 
 * @return {int} 1 en cas de succès et un nombre négatif sinon. 0 si l'élement 
 *               n'existait pas.
 */
int list_remove(list *list_p, void *elem);

void *list_last_inserted(list *list_p);

/**
 * Applique à tous les élements pointés par list_p la fonction apply.
 * apply prend en premier paramètre l'élement courant de la liste, en second
 * paramètre elle prend l'accumulateur courant. Celle-ci doit retourner le 
 * nouvel accumulateur. Retourne l'accumilateur final.
 * 
 * @param {list *} La liste où l'on souhaite appliquer la fonction.
 * @param {int *(void *, int)} La fonction à appliquer
 * @return {int} L'accumulateur final.
 */
int list_apply(list *list_p, int (*apply)(void *, int));

/**
 * Libère la liste pointée par list_p ansi que tous ses élements. Retourne 1
 * en cas de succès et un nombre négatif sinon.
 * 
 * @param {list *} La liste à libérer.
 * @return {int} 1 en cas de succès ou un nombre négatif sinon.
 */
int list_dispose(list *list_p);

#endif