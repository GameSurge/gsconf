#include "common.h"
#include "ptrlist.h"

struct ptrlist *ptrlist_create()
{
	struct ptrlist *list = malloc(sizeof(struct ptrlist));
	memset(list, 0, sizeof(struct ptrlist));
	list->count = 0;
	list->size = 2;
	list->data = calloc(list->size, sizeof(struct ptrlist_node *));
	return list;
}

void ptrlist_free(struct ptrlist *list)
{
	unsigned int i;
	for(i = 0; i < list->count; i++)
	{
		if(list->free_func)
			list->free_func(list->data[i]->ptr);
		free(list->data[i]);
	}
	free(list->data);
	free(list);
}

unsigned int ptrlist_add(struct ptrlist *list, int ptr_type, void *ptr)
{
	struct ptrlist_node *node;
	int pos;

	if((pos = ptrlist_find(list, ptr)) != -1)
		return pos;

	if(list->count == list->size) // list is full, we need to allocate more memory
	{
		list->size <<= 1; // double size
		list->data = realloc(list->data, list->size * sizeof(struct ptrlist_node *));
	}

	node = malloc(sizeof(struct ptrlist_node));
	node->type = ptr_type;
	node->ptr = ptr;

	pos = list->count++;
	list->data[pos] = node;
	return pos;
}

int ptrlist_find(struct ptrlist *list, const void *ptr)
{
	for(unsigned int i = 0; i < list->count; i++)
		if(list->data[i]->ptr == ptr)
			return i;

	return -1;
}

void ptrlist_set_free_func(struct ptrlist *list, ptrlist_free_f *free_func)
{
	list->free_func = free_func;
}

void ptrlist_clear(struct ptrlist *list)
{

	for(unsigned int i = 0; i < list->count; i++)
	{
		if(list->free_func)
			list->free_func(list->data[i]->ptr);
		free(list->data[i]);
	}
	list->count = 0;
}

void ptrlist_del(struct ptrlist *list, unsigned int pos, unsigned int *pos_ptr)
{
	assert(pos < list->count);
	if(list->free_func)
		list->free_func(list->data[pos]->ptr);
	free(list->data[pos]);
	list->data[pos] = list->data[--list->count]; // copy last element into empty position
	if(pos_ptr != NULL && *pos_ptr == list->count)
		*pos_ptr = pos;
}

void ptrlist_del_ptr(struct ptrlist *list, void *ptr)
{
	int pos = ptrlist_find(list, ptr);
	if(pos != -1)
		ptrlist_del(list, pos, NULL);
}
