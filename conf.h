#ifndef CONF_H
#define CONF_H

#include "database.h"

int conf_init();
void conf_fini();

struct dict *conf_root();
void *conf_get(const char *path, enum database_type type);
struct db_node *conf_node(const char *path);
#define conf_bool(PATH) true_string(conf_get((PATH), DB_STRING))
#define conf_str(PATH) conf_get((PATH), DB_STRING)

#endif

