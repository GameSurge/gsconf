#include "common.h"
#include "dict.h"

struct dict *dict_create()
{
	struct dict *dict = malloc(sizeof(struct dict));
	memset(dict, 0, sizeof(struct dict));
	return dict;
}

void dict_free(struct dict *dict)
{
	while(dict->count)
		dict_delete_node(dict, dict->head);
	if(dict->free)
		free(dict->free);
	free(dict);
}

void dict_set_free_funcs(struct dict *dict, dict_free_f free_keys_func, dict_free_f free_data_func)
{
	dict->free_keys_func = free_keys_func;
	dict->free_data_func = free_data_func;
}

void dict_insert(struct dict *dict, char *key, void *data)
{
	struct dict_node *node = malloc(sizeof(struct dict_node));
	memset(node, 0, sizeof(struct dict_node));

	node->key = key;
	node->data = data;

	node->next = dict->head;
	dict->head = node;

	if(node->next)
		node->next->prev = node;

	if(!dict->tail)
		dict->tail = node;

	dict->count++;
}

struct dict_node *dict_find_node(struct dict *dict, const char *key)
{
	struct dict_node *lnode, *rnode;

	// we search from both sides
	for(lnode = dict->head, rnode = dict->tail; lnode && rnode; lnode = lnode->next, rnode = rnode->prev)
	{
		if(!strcasecmp(lnode->key, key))
			return lnode;
		if(!strcasecmp(rnode->key, key))
			return rnode;

		// we've reached the middle of the list
		if((lnode == rnode) || (lnode == rnode->prev))
			break;
	}

	// nothing found
	return NULL;
}

void *dict_find(struct dict *dict, const char *key)
{
	struct dict_node *node = dict_find_node(dict, key);
	return node ? node->data : NULL;
}

void dict_delete_node(struct dict *dict, struct dict_node *node)
{
	if(!node)
		return;

	if(dict->head == node)
		dict->head = node->next;
	if(dict->tail == node)
		dict->tail = node->prev;

	if(node->prev)
		node->prev->next = node->next;
	if(node->next)
		node->next->prev = node->prev;

	if(dict->free_data_func && node->data)
		dict->free_data_func(node->data);
	if(dict->free_keys_func && node->key)
		dict->free_keys_func(node->key);

	dict->count--;

	if(dict->free) // free old deleted node
		free(dict->free);

	node->key  = NULL;
	node->data = NULL;
	dict->free = node;
}

unsigned int dict_delete(struct dict *dict, const char *key)
{
	struct dict_node *node;
	if(!(node = dict_find_node(dict, key)))
		return 1;

	dict_delete_node(dict, node);
	return 0;
}

unsigned int dict_delete_key_value(struct dict *dict, const char *key, void *data)
{
	struct dict_node *node;
	if(!(node = dict_find_node(dict, key)) || node->data != data)
		return 1;

	dict_delete_node(dict, node);
	return 0;
}

void dict_clear(struct dict *dict)
{
	while(dict->count)
		dict_delete_node(dict, dict->head);
}

void dict_rename_key(struct dict *dict, const char *key, const char *newkey)
{
	struct dict_node *node = dict_find_node(dict, key);
	if(!node)
		return;
	free(node->key);
	node->key = strdup(newkey);
}
