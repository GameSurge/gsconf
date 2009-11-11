#include "common.h"
#include "stringlist.h"
#include "strnatcmp.h"

// stringlists...
// ...DO NOT store duplicates of strings
// ...free the stored strings

struct stringlist *stringlist_create()
{
	struct stringlist *list = malloc(sizeof(struct stringlist));
	memset(list, 0, sizeof(struct stringlist));
	list->count = 0;
	list->size = 2;
	list->data = calloc(list->size, sizeof(char *));
	return list;
}

void stringlist_free(struct stringlist *list)
{
	for(unsigned int i = 0; i < list->count; i++)
	{
		if(list->data[i])
			free(list->data[i]);
	}
	free(list->data);
	free(list);
}

struct stringlist *stringlist_copy(const struct stringlist *slist)
{
	struct stringlist *new = malloc(sizeof(struct stringlist));
	new->count = slist->count;
	new->size = slist->size;
	new->data = calloc(new->size, sizeof(char *));
	for(unsigned int i = 0; i < slist->count; i++) // copy entries
		new->data[i] = strdup(slist->data[i]);

	return new;
}

void stringlist_add(struct stringlist *list, char *string)
{
	if(list->count == list->size) // list is full, we need to allocate more memory
	{
		list->size <<= 1; // double size
		list->data = realloc(list->data, list->size * sizeof(char *));
	}

	list->data[list->count++] = string;
}

void stringlist_del(struct stringlist *list, int pos)
{
	assert(pos < (int)list->count);
	if(list->data[pos])
		free(list->data[pos]);
	list->data[pos] = list->data[--list->count]; // copy last element into empty position
}

char *stringlist_shift(struct stringlist *list)
{
	char *string;

	if(list->count == 0)
		return NULL;

	string = strdup(list->data[0]);
	free(list->data[0]);
	list->count--;
	memmove(list->data, list->data + 1, list->count * sizeof(*list->data));
	return string;
}

int stringlist_find(struct stringlist *list, const char *string)
{
	for(unsigned int i = 0; i < list->count; i++)
	{
		if(!strcasecmp(list->data[i], string))
			return i;
	}

	return -1;
}

int stringlist_cmp(const void *a, const void *b)
{
	return strnatcasecmp(*(const char **)a, *(const char **)b);
}

void stringlist_sort(struct stringlist *list)
{
	qsort(list->data, list->count, sizeof(list->data[0]), stringlist_cmp);
}

struct stringlist *stringlist_build(const char *str, ...)
{
	va_list args;
	struct stringlist *list = stringlist_create();

	stringlist_add(list, strdup(str));
	va_start(args, str);
	while((str = va_arg(args, const char *)))
		stringlist_add(list, strdup(str));
	va_end(args);
	return list;
}

struct stringlist *stringlist_build_n(unsigned int count, ...)
{
	va_list args;
	struct stringlist *list = stringlist_create();

	va_start(args, count);
	for(unsigned int i = 0; i < count; i++)
	{
		const char *str = va_arg(args, const char *);
		stringlist_add(list, str ? strdup(str) : NULL);
	}
	va_end(args);
	return list;
}
