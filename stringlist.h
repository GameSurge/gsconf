#ifndef STRINGLIST_H
#define STRINGLIST_H

struct stringlist
{
	unsigned int count;
	unsigned int size;

	char **data;
};

struct stringlist *stringlist_create();
void stringlist_free(struct stringlist *list);
struct stringlist *stringlist_copy(const struct stringlist *slist);

void stringlist_add(struct stringlist *list, char *string);
void stringlist_del(struct stringlist *list, int pos);
char *stringlist_shift(struct stringlist *list);
int stringlist_find(struct stringlist *list, const char *string);
void stringlist_sort(struct stringlist *list);
struct stringlist *stringlist_build(const char *str, ...) NULL_SENTINEL;
struct stringlist *stringlist_build_n(unsigned int count, ...);

#endif
