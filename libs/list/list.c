#include <stdio.h>
#include <stdlib.h>
#include "list.h"

/*
 * Codes d'erreur
 */

#define NULL_ELEM -1
#define MEMORY_ERROR -2

/*
 * Structures
 */

typedef struct clist clist;

struct clist {
    void *value;
    clist *next;
};

struct list {
    clist *head;
    clist *tail;
    size_t size;
    int (*compar)(void *, void*);
};

list *init_list(int (*compar)(void *, void *)) {
    list *list_p = malloc(sizeof(list));
    if (list_p == NULL) {
        return NULL;
    }
    list_p->head = NULL;
    list_p->tail = NULL;
    list_p->size = 0;
    list_p->compar = compar;

    return list_p;
}

int list_add(list *list_p, void *elem) {
    if (list_p == NULL || elem == NULL) {
        return NULL_ELEM;
    }
    clist *cell = malloc(sizeof(*cell));
    if (cell == NULL) {
        return MEMORY_ERROR;
    }
    cell->value = elem;
    cell->next = NULL;
    if (list_p->size == 0) {
        list_p->head = cell;
    } else {
        list_p->tail->next = cell;
    }
    list_p->tail = cell;
    ++list_p->size;

    return 1;
}

int list_remove(list *list_p, void *elem) {
    if (list_p == NULL || elem == NULL) {
        return NULL_ELEM;
    }
    clist *cell = list_p->head;
    int r = 0;
    while (cell != NULL) {
        if (list_p->compar(elem, cell->value) == 0) {
            r = 1;
            break;
        }
        cell = cell->next;
    }

    return r;
}

int list_apply(list *list_p, int (*apply)(void *, int)) {
    if (list_p == NULL || apply == NULL) {
        return NULL_ELEM;
    }
    clist *cell = list_p->head;
    int acc = 0;
    while (cell != NULL) {
        acc = apply(cell->value, acc);
        cell = cell->next;
    }

    return acc;
}

int list_dispose(list *list_p) {
    if (list_p == NULL) {
        return NULL_ELEM;
    }
    clist *cell = list_p->head;
    clist *next_cell;
    while (cell != NULL) {
        next_cell = cell->next;
        free(cell);
        cell = next_cell;
    }
    free(list_p);

    return 1;
}