#ifndef PTRLIST_H
#define PTRLIST_H

typedef void (ptrlist_free_f)(void *ptr);

struct ptrlist_node
{
	int	type;
	void	*ptr;
};

struct ptrlist
{
	unsigned int	count;
	unsigned int	size;

	ptrlist_free_f *free_func;
	struct ptrlist_node	**data;
};

struct ptrlist *ptrlist_create();
void ptrlist_free(struct ptrlist *list);

unsigned int ptrlist_add(struct ptrlist *list, int ptr_type, void *ptr);
int ptrlist_find(struct ptrlist *list, const void *ptr);
void ptrlist_set_free_func(struct ptrlist *list, ptrlist_free_f *free_func);
void ptrlist_clear(struct ptrlist *list);
void ptrlist_del(struct ptrlist *list, unsigned int pos, unsigned int *pos_ptr);
void ptrlist_del_ptr(struct ptrlist *list, void *ptr);

#endif
