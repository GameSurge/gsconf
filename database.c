#include "common.h"
#include "database.h"
#include "stringbuffer.h"
#include "ptrlist.h"
#include "stringlist.h"

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#define EOL '\n'

static struct dict *databases;

static const char *errors[] = {
	"Success, but if you see this message there was some weird error",
	"Unterminated string",
	"Expected opening quote ('\"')",
	"Expected opening brace ('{')",
	"Expected opening parenthesis ('(')",
	"Expected comma (',')",
	"Expected data begin ('\"', '(' or '{')",
	"Expected semicolon (';')",
	"Expected record data",
	"Expected comment end (\"*/\")"
};

static unsigned int database_eof(struct database *db);
static struct db_node *database_read_record(struct database *db, char **key);

struct dict *database_dict()
{
	return databases;
}

struct dict *database_load(const char *filename)
{
	struct dict *nodes = NULL;
	debug("Loading database from file %s", filename);

	struct database *db = malloc(sizeof(struct database));
	db->name = strdup(filename);
	db->filename = strdup(filename);
	db->tmp_filename = NULL;
	db->read_func = NULL;
	db->write_func = NULL;
	db->fp = NULL;
	db->free_on_error = NULL;
	db->nodes = NULL;

	if(database_read(db, 0) == 0)
		nodes = db->nodes;
	else if(db->nodes)
		dict_free(db->nodes);

	free(db->name);
	free(db->filename);
	free(db);

	return nodes;
}

struct database *database_create(const char *name, db_read_f *read_func, db_write_f *write_func)
{
	debug("Creating database %s", name);
	struct database *db = malloc(sizeof(struct database));
	db->name = strdup(name);
	db->filename = malloc(strlen(name) + 4); // filename + .db + \0
	snprintf(db->filename, strlen(name) + 4, "%s.db", name);
	db->tmp_filename = malloc(strlen(name) + 9); // . + filename + .db.tmp + \0
	snprintf(db->tmp_filename, strlen(name) + 9, ".%s.db.tmp", name);
	db->read_func = read_func;
	db->write_func = write_func;
	db->fp = NULL;
	db->free_on_error = NULL;
	db->nodes = NULL;

	dict_insert(databases, db->name, db);
	return db;
}

void database_delete(struct database *db)
{
	debug("Deleting database %s", db->name);
	dict_delete(databases, db->name);

	free(db->name);
	free(db->filename);
	free(db->tmp_filename);
	if(db->nodes)
		dict_free(db->nodes);
	free(db);
}

struct db_node *database_fetch_path(struct dict *db_nodes, const char *node_path)
{
	char *path = strdup(node_path);
	char *orig_path = path;
	struct dict *db_record = db_nodes;
	struct db_node *node;
	struct stringbuffer *buf = stringbuffer_create();

	if(*path == '/') // leading slash -> get rid of it
		path++;

	if(strlen(path) && path[strlen(path) - 1] == '/') // trailing slash -> get rid of it
		path[strlen(path) - 1] = '\0';

	if(!strlen(path))
		return NULL;

	while(strchr(path, '/'))
	{
		if(*path == '/') // next path element starting -> update record with previous path element
		{
			node = dict_find(db_record, buf->string);
			if(node == NULL || node->type != DB_OBJECT) // not found or not an object
			{
				stringbuffer_free(buf);
				free(orig_path);
				return NULL;
			}

			db_record = node->data.object;
			stringbuffer_flush(buf);
			path++;
		}
		else // path not yet complete
		{
			stringbuffer_append_char(buf, *path);
			path++;
		}
	}

	node = dict_find(db_record, path); // find node in last path path
	stringbuffer_free(buf);
	free(orig_path);
	return node;
}

// get a database node by path and type
void *database_fetch(struct dict *db_nodes, const char *path, enum database_type type)
{
	struct db_node *node = database_fetch_path(db_nodes, path);
	return ((node && node->type == type) ? node->data.ptr : NULL);
}

void database_free_node(struct db_node *node)
{
	if(node->type != DB_EMPTY)
	{
		switch(node->type)
		{
			case DB_OBJECT: // dict_free calls this function for each node -> recursive deletion of all sub-elements
				//debug("Freeing object node");
				dict_free(node->data.object);
				break;
			case DB_STRING:
				//debug("Freeing string node");
				free(node->data.string);
				break;
			case DB_STRINGLIST:
				//debug("Freeing stringlist node");
				stringlist_free(node->data.slist);
				break;
			default:
				error("Invalid node type: %d", node->type);
		}
	}

	free(node);
}

int database_read(struct database *db, unsigned int free_nodes_after_read)
{
	struct stat statinfo;
	debug("Reading database %s", db->name);
	if((db->fp = fopen(db->filename, "r")) == NULL)
	{
		debug("Could not open database %s (%s) for reading: %s (%d)", db->name, db->filename, strerror(errno), errno);
		return -1;
	}

	if(fstat(fileno(db->fp), &statinfo))
	{
		error("Could not fstat database file %s (database %s): %s (%d)", db->filename, db->name, strerror(errno), errno);
		return -2;
	}

	db->length = statinfo.st_size;

#ifdef HAVE_MMAP
	if((db->map = mmap(NULL, db->length, PROT_READ|PROT_WRITE, MAP_PRIVATE, fileno(db->fp), 0)) != MAP_FAILED)
	{
		db->source = SRC_MMAP;
	}
	else
	{
		error("mmap() failed: %s (%d), falling back to file mode", strerror(errno), errno);
		db->source = SRC_FILE;
		db->map = NULL;
	}
#else
	db->source = SRC_FILE;
	db->map = NULL;
#endif

	db->free_on_error = ptrlist_create();
	if(db->nodes)
		dict_free(db->nodes);
	db->nodes = dict_create();
	dict_set_free_funcs(db->nodes, free, (dict_free_f*)database_free_node);

	db->line = 1;
	db->line_pos = 0;
	db->map_pos = 0;

	int result;
	if((result = setjmp(db->jbuf)) == 0) // ==0 means direct call, !=0 means return from longjmp
	{
		while(!database_eof(db))
		{
			struct db_node *node;
			char *key;
			node = database_read_record(db, &key);
			if(key && node)
			{
				//debug("Successfully parsed node with key %s", key);
				dict_insert(db->nodes, key, node);
			}
		}

		if(db->read_func)
			db->read_func(db);
	}
	else
	{
		unsigned int i;
		error("Parse error in database %s on line %d at position %d: %s", db->name, db->line, db->line_pos, errors[result]);
		for(i = 0; i < db->free_on_error->count; i++)
		{
			struct ptrlist_node *pl_node = db->free_on_error->data[i];
			switch(pl_node->type)
			{
				case PTR_STRING:
					//debug("Freeing string");
					free(pl_node->ptr);
					break;
				case PTR_DICT:
					//debug("Freeing dict");
					dict_free(pl_node->ptr);
					break;
				case PTR_DB_ENTRY:
					//debug("Freeing db node");
					database_free_node(pl_node->ptr);
					break;
				case PTR_STRINGLIST:
					//debug("Freeing stringlist");
					stringlist_free(pl_node->ptr);
					break;
				default:
					error("Invalid ptr type %d in ptr list db->free_on_error", pl_node->type);
			}
		}
	}

	ptrlist_free(db->free_on_error);
	if(free_nodes_after_read)
	{
		dict_free(db->nodes);
		db->nodes = NULL;
	}

	switch(db->source)
	{
		case SRC_FILE:
			fclose(db->fp);
			break;
		case SRC_MMAP:
#ifdef HAVE_MMAP
			munmap(db->map, db->length);
#endif
			fclose(db->fp);
			break;
		default:
			error("Invalid database source in database_read(): %d", db->source);
			return EOF;
	}

	db->fp = NULL;
	return result;
}

// read functions
static unsigned int database_eof(struct database *db)
{
	switch(db->source)
	{
		case SRC_FILE:
			return feof(db->fp);
			break;
		case SRC_MMAP:
			return db->map_pos >= db->length;
			break;
		default:
			error("Invalid database source in database_eof(): %d", db->source);
			return 1;
			break;
	}
}

static char database_getc(struct database *db)
{
	char c;
	switch(db->source)
	{
		case SRC_FILE:
			c = fgetc(db->fp);
			break;
		case SRC_MMAP:
			c = (database_eof(db) ? EOF : db->map[db->map_pos++]); // we must check for EOF manually here
			break;
		default:
			error("Invalid database source in database_getc(): %d", db->source);
			return EOF;
	}

	if(c == EOL)
	{
		db->line++;
		db->line_pos = 1;
	}
	else if(c != EOF)
	{
		db->line_pos++;
	}

	return c;
}

static void database_ungetc(struct database *db, char c)
{
	switch(db->source)
	{
		case SRC_FILE:
			ungetc(c, db->fp);
			break;
		case SRC_MMAP:
			db->map[--db->map_pos] = c;
			break;
		default:
			error("Invalid database source in database_ungetc(): %d", db->source);
	}

	if(c == EOL) // end of line -> decrease line counter
	{
		db->line--;
		db->line_pos = -1;
	}
	else
	{
		db->line_pos--;
	}
}

static char database_valid_char(struct database *db)
{
	char c, cc;
	while(!database_eof(db))
	{
		c = database_getc(db);
		if(c == EOF)
			return EOF;

		if(isspace(c)) // this function is always called outside strings, so we can simply skip spaces
			continue;

		if(c != '/') // if it's not '/', it cannot be the begin of a comment
			return c;

		cc = database_getc(db); // read next char
		if(cc == '/') // single-line comment
		{
			do
			{
				c = database_getc(db);
			} while(c != EOL && c != EOF);
		}
		else if(cc == '*') // multi-line comment
		{
			unsigned int is_comment = 1;
			do
			{
				do
				{
					c = database_getc(db);
				} while(c != EOF && c != '*');

				c = database_getc(db);
				if(c == EOF) // eof and unclosed commend
					longjmp(db->jbuf, EXPECTED_COMMENT_END);
				else if(c == '/') // comment end
					is_comment = 0;
			} while(is_comment);
		}
		else // not a comment start -> push cc back to buffer and return c
		{
			if(cc != EOF) // if it's EOF we don't push it back
				database_ungetc(db, cc);
			return c;
		}
	}

	return EOF;
}

static char *database_read_string(struct database *db)
{
	char *buf;
	unsigned int len = 0, size = 8, pl_key;
	char c = database_valid_char(db);

	if(c == EOF)
		return NULL;
	else if(c != '"')
		longjmp(db->jbuf, EXPECTED_OPEN_QUOTE);

	buf = malloc(size);
	pl_key = ptrlist_add(db->free_on_error, PTR_STRING, buf);
	while(!database_eof(db) && (c = database_getc(db)) != '"')
	{
		if(c != '\\')
		{
			if(c == EOL)
			{
				database_ungetc(db, c);
				longjmp(db->jbuf, UNTERMINATED_STRING);
			}

			buf[len++] = c;
		}
		else
		{
			c = database_getc(db);
			switch(c)
			{
				case '\\': buf[len++] = '\\';   break; // backslash
				case 'n':  buf[len++] = '\n';   break; // newline
				case 'r':  buf[len++] = '\r';   break; // carriage return
				case 't':  buf[len++] = '\t';   break; // tab
				case 'C':  buf[len++] = '\033'; break; // custom escape for ansi color
				case '1':  buf[len++] = '\001'; break; // ascii 1, for readline
				case '2':  buf[len++] = '\002'; break; // ascii 2, for readline
				default:   buf[len++] = c;
			}
		}

		if(size == len) // buffer is full -> allocate more space
		{
			size <<= 1;
			buf = realloc(buf, size);
		}
	}

	ptrlist_del(db->free_on_error, pl_key, NULL);
	buf[len] = '\0';
	return buf;
}

static struct stringlist *database_read_stringlist(struct database *db)
{
	struct stringlist *slist;
	unsigned int pl_key;
	char c = database_valid_char(db);

	if(c == EOF)
		return NULL;
	else if(c != '(')
		longjmp(db->jbuf, EXPECTED_OPEN_PAREN);

	slist = stringlist_create();
	pl_key = ptrlist_add(db->free_on_error, PTR_STRINGLIST, slist);
	while(1)
	{
		c = database_valid_char(db);
		if(c == ')' || c == EOF)
			break; // end of stringlist or end of file

		database_ungetc(db, c); // push c back to buffer
		stringlist_add(slist, database_read_string(db));

		c = database_valid_char(db);
		if(c == ')' || c == EOF)
			break; // end of stringlist or end of file
		else if(c != ',')
		{
			stringlist_free(slist);
			ptrlist_del(db->free_on_error, pl_key, NULL);
			longjmp(db->jbuf, EXPECTED_COMMA);
		}
	}

	ptrlist_del(db->free_on_error, pl_key, NULL);
	return slist;
}

static struct dict *database_read_object(struct database *db)
{
	unsigned int pl_key;
	struct dict *object;
	char c = database_valid_char(db);

	if(c == EOF)
		return NULL;
	else if(c != '{')
		longjmp(db->jbuf, EXPECTED_OPEN_BRACE);

	object = dict_create();
	pl_key = ptrlist_add(db->free_on_error, PTR_DICT, object);
	dict_set_free_funcs(object, free, (dict_free_f*)database_free_node);
	while(1)
	{
		char *key;
		struct db_node *node;

		c = database_valid_char(db);
		if(c == '}' || c == EOF)
			break; // end of object or end of file

		database_ungetc(db, c); // push c back to buffer
		node = database_read_record(db, &key);
		if(node && key)
		{
			//debug("Successfully parsed subnode with key %s", key);
			dict_insert(object, key, node);
		}
	}
	ptrlist_del(db->free_on_error, pl_key, NULL);
	return object;
}

static struct db_node *database_read_record(struct database *db, char **key)
{
	unsigned int key_pl_key, pl_key;
	struct db_node *node;
	char c;

	*key = database_read_string(db);
	if(*key == NULL)
		return NULL;

	c = database_valid_char(db);
	if(c == EOF)
	{
		if(!*key)
			return NULL;
		free(*key);
		longjmp(db->jbuf, EXPECTED_RECORD_DATA);
	}

	key_pl_key = ptrlist_add(db->free_on_error, PTR_STRING, *key);

	if(c == '=')
		c = database_valid_char(db);
	database_ungetc(db, c);

	node = malloc(sizeof(struct db_node));
	node->type = DB_EMPTY;
	pl_key = ptrlist_add(db->free_on_error, PTR_DB_ENTRY, node);
	switch(c)
	{
		case '"': // string
			//debug("Found string in key %s", *key);
			node->data.string = database_read_string(db);
			node->type = DB_STRING;
			//debug("String data: %s", node->data.string);
			break;

		case '(': // string list
			//debug("Found stringlist in key %s", *key);
			node->data.slist = database_read_stringlist(db);
			node->type = DB_STRINGLIST;
			//debug("Stringlist data (%d):", node->data.slist->count);
			/*
			unsigned int i;
			for(i = 0; i < node->data.slist->count; i++)
				debug("  '%s'", node->data.slist->data[i]);
			*/
			break;

		case '{': // object
			//debug("Found object in key %s", *key);
			node->data.object = database_read_object(db);
			node->type = DB_OBJECT;
			break;

		default:
			longjmp(db->jbuf, EXPECTED_START_DATA);
	}

	if((c = database_valid_char(db)) != ';')
		longjmp(db->jbuf, EXPECTED_SEMICOLON);

	ptrlist_del(db->free_on_error, key_pl_key, &pl_key);
	ptrlist_del(db->free_on_error, pl_key, NULL);
	return node;
}

// debug functions
static inline void print_indent(unsigned int indentcount)
{
	while(indentcount--)
		printf("\t");
}

void database_dump_object(struct dict *object, unsigned int indent)
{
	struct db_node *child;
	unsigned int i;
	dict_iter(node, object)
	{
		child = node->data;
		switch(child->type)
		{
			case DB_EMPTY:
				print_indent(indent);
				printf("'%s' (empty)\n", node->key);
				break;

			case DB_OBJECT:
				print_indent(indent);
				printf("'%s' (object):\n", node->key);
				database_dump_object(child->data.object, indent + 1);
				printf("\n");
				break;

			case DB_STRING:
				print_indent(indent);
				printf("'%s' (string(%lu)): '%s'\n", node->key, (unsigned long)strlen(child->data.string), child->data.string);
				break;

			case DB_STRINGLIST:
				print_indent(indent);
				printf("'%s' (stringlist(%d)):\n", node->key, child->data.slist->count);
				for(i = 0; i < child->data.slist->count; i++)
				{
					print_indent(indent + 1);
					printf("'%s'\n", child->data.slist->data[i]);
				}
				printf("\n");
				break;
		}
	}
}

void database_dump(struct dict *db_nodes)
{
	database_dump_object(db_nodes, 0);
}

// write functions
int database_write(struct database *db)
{
	int result;

	assert(db->tmp_filename && db->write_func);
	debug("Writing database %s", db->name);

	if((db->fp = fopen(db->tmp_filename, "w")) == NULL)
	{
		error("Could not open database %s (%s) for writing: %s (%d)", db->name, db->tmp_filename, strerror(errno), errno);
		return -1;
	}

	db->indent = 0;
	result = db->write_func(db);
	if(db->indent != 0) // unclosed objects
	{
		error("Writing %s failed, %d unclosed objects", db->name, db->indent);
		fclose(db->fp);
		remove(db->tmp_filename); // delete temp. database file
		return -1;
	}

	if(result != 0) // write func returned error code
	{
		error("Writing %s failed, return code was %d", db->name, result);
		fclose(db->fp);
		remove(db->tmp_filename); // delete temp. database file
		return result;
	}
	else // write func returned success
	{
		fclose(db->fp);
		remove(db->filename); // delete old database
		rename(db->tmp_filename, db->filename); // rename temp. database file
		debug("Database %s successfully written", db->name);
		return 0;
	}
}

#define database_putc(DB, CHAR)	fputc(CHAR, (DB)->fp)
#define database_puts(DB, STR)	fputs(STR, (DB)->fp)

static void database_write_indent(struct database *db)
{
	unsigned int i;
	for(i = 0; i < db->indent; i++)
		database_putc(db, '\t');
}

static void database_write_quoted_string(struct database *db, const char *str)
{
	unsigned int i;
	char c;

	database_putc(db, '"');
	for(i = 0; i < strlen(str); i++)
	{
		c = str[i];
		switch(c)
		{
			case '\\':   database_puts(db, "\\\\"); break; // back
			case '\n':   database_puts(db, "\\n");  break; // newline
			case '\r':   database_puts(db, "\\r");  break; // carriage return
			case '\t':   database_puts(db, "\\t");  break; // tab
			case '\033': database_puts(db, "\\C");  break; // custom escape for ansi color
			case '\001': database_puts(db, "\\1");  break; // ascii 1, for readline
			case '\002': database_puts(db, "\\2");  break; // ascii 2, for readline
			case '"':    database_puts(db, "\\\""); break; // quote
			default:     database_putc(db, c);
		}
	}
	database_putc(db, '"');
}

void database_begin_object(struct database *db, const char *key)
{
	database_write_indent(db);
	database_write_quoted_string(db, key);
	database_puts(db, " = {\n");
	db->indent++;
}

void database_end_object(struct database *db)
{
	db->indent--;
	database_write_indent(db);
	database_puts(db, "};\n");
}

void database_write_long(struct database *db, const char *key, long value)
{
	char str[16];
	snprintf(str, sizeof(str), "%lu", value);
	database_write_string(db, key, str);
}

void database_write_string(struct database *db, const char *key, const char *value)
{
	database_write_indent(db);
	database_write_quoted_string(db, key);
	database_puts(db, " = ");
	database_write_quoted_string(db, value);
	database_puts(db, ";\n");
}

void database_write_stringlist(struct database *db, const char *key, struct stringlist *slist)
{
	unsigned int i;
	database_write_indent(db);
	database_write_quoted_string(db, key);
	database_puts(db, " = (");
	for(i = 0; i < slist->count; i++)
	{
		database_write_quoted_string(db, slist->data[i]);
		if(i < (slist->count - 1)) // not the last node
			database_puts(db, ", ");
	}
	database_puts(db, ");\n");
}

void database_write_object(struct database *db, const char *key, const struct dict *object)
{
	database_begin_object(db, key);

	dict_iter(node, object)
	{
		struct db_node *child = node->data;

		switch(child->type)
		{
			case DB_OBJECT:
				database_write_object(db, node->key, child->data.object);
				break;

			case DB_STRING:
				database_write_string(db, node->key, child->data.string);
				break;

			case DB_STRINGLIST:
				database_write_stringlist(db, node->key, child->data.slist);
				break;

			default:
				error("Invalid node type %d in database_write_object()", child->type);
		}
	}

	database_end_object(db);
}

// object write functions
struct database_object *database_obj_create()
{
	struct database_object *dbo = malloc(sizeof(struct database_object));
	memset(dbo, 0, sizeof(struct database_object));

	dbo->current	= dict_create();
	dbo->stack_used	= 0;
	dbo->stack_size	= 4;
	dbo->stack	= calloc(dbo->stack_size, sizeof(struct dict *));
	dict_set_free_funcs(dbo->current, free, (dict_free_f*)database_free_node);

	return dbo;
}

void database_obj_free(struct database_object *dbo)
{
	// We do NOT free the dict
	free(dbo->stack);
	free(dbo);
}

void database_obj_begin_object(struct database_object *dbo, const char *key)
{
	struct dict *object;
	struct db_node *node;

	object = dict_create();
	dict_set_free_funcs(object, free, (dict_free_f*)database_free_node);

	node = malloc(sizeof(struct db_node));
	node->type = DB_OBJECT;
	node->data.object = object;

	dict_insert(dbo->current, strdup(key), node);

	if(dbo->stack_size == dbo->stack_used)
	{
		dbo->stack_size += 2;
		dbo->stack = realloc(dbo->stack, dbo->stack_size * sizeof(struct dict *));
	}

	dbo->stack[dbo->stack_used++] = dbo->current;
	dbo->current = object;
}

void database_obj_end_object(struct database_object *dbo)
{
	assert(dbo->stack_used);
	dbo->current = dbo->stack[--dbo->stack_used];
}

void database_obj_write_long(struct database_object *dbo, const char *key, long value)
{
	char str[16];
	snprintf(str, sizeof(str), "%lu", value);
	database_obj_write_string(dbo, key, str);
}

void database_obj_write_string(struct database_object *dbo, const char *key, const char *value)
{
	struct db_node *node;

	node = malloc(sizeof(struct db_node));
	node->type = DB_STRING;
	node->data.string = strdup(value);;

	dict_insert(dbo->current, strdup(key), node);
}

void database_obj_write_stringlist(struct database_object *dbo, const char *key, struct stringlist *slist)
{
	struct db_node *node;

	node = malloc(sizeof(struct db_node));
	node->type = DB_STRINGLIST;
	node->data.slist = stringlist_copy(slist);

	dict_insert(dbo->current, strdup(key), node);
}

// misc functions
struct dict *database_copy_object(const struct dict *object)
{
	struct dict *copy;
	copy = dict_create();
	dict_set_free_funcs(copy, free, (dict_free_f*)database_free_node);

	dict_iter(node, object)
	{
		struct db_node *child, *node_copy;

		child = node->data;
		node_copy = malloc(sizeof(struct db_node));
		node_copy->type = child->type;

		switch(child->type)
		{
			case DB_OBJECT:
				node_copy->data.object = database_copy_object(child->data.object);
				break;

			case DB_STRING:
				node_copy->data.string = strdup(child->data.string);
				break;

			case DB_STRINGLIST:
				node_copy->data.slist = stringlist_copy(child->data.slist);
				break;

			default:
				error("Invalid node type %d in database_copy_object()", child->type);
				free(node_copy);
				node_copy = NULL;
		}

		if(node_copy)
			dict_insert(copy, strdup(node->key), node_copy);
	}

	return copy;
}

// init/cleanup functions
void database_init()
{
	databases = dict_create();
}

void database_fini()
{
	assert(dict_size(databases) == 0); // all existing databases should be deleted by whoever created them
	dict_free(databases);
}
