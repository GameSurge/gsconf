#include "common.h"
#include "conf.h"

static struct dict *cfg;

int conf_init()
{
	if((cfg = database_load(CFG_FILE)) == NULL)
	{
		error("Could not parse config file (%s)", CFG_FILE);
		return 1;
	}

	return 0;
}

void conf_fini()
{
	dict_free(cfg);
}

struct dict *conf_root()
{
	return cfg;
}

void *conf_get(const char *path, enum database_type type)
{
	assert(cfg);
	return database_fetch(cfg, path, type);
}

struct db_node *conf_node(const char *path)
{
	assert(cfg);
	return database_fetch_path(cfg, path);
}
