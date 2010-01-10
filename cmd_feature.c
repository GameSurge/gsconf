#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"

static char *feature_generator(const char *text, int state);
static char *feature_all_srvtypes_generator(const char *text, int state);
static char *feature_srvtype_generator(const char *text, int state);
CMD_FUNC(feature_list);
CMD_FUNC(feature_set);
CMD_TAB_FUNC(feature_set);
CMD_FUNC(feature_del);
CMD_TAB_FUNC(feature_del);

static const char *feat_name_tc = NULL;

static struct command commands[] = {
	CMD_STUB("feature", "Feature Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "feature" subcommands
	CMD("list", feature_list, "Show features"),
	CMD_TC("set", feature_set, "Set a feature"),
	CMD_TC("del", feature_del, "Remove a feature"),
	CMD_LIST_END
};



void cmd_feature_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "feature");
	cmd_alias("features", "feature", "list");
	cmd_alias("setfeature", "feature", "set");
	cmd_alias("delfeature", "feature", "del");
}

CMD_FUNC(feature_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT * FROM features ORDER BY name ASC, server_type ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(3, rows);
	table_set_header(table, "Name", "Servers", "Value");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char*)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char*)pgsql_nvalue(res, i, "server_type"));
		table_col_str(table, i, 2, (char*)pgsql_nvalue(res, i, "value"));
	}

	table_send(table);
	table_free(table);

	pgsql_free(res);
}

CMD_FUNC(feature_set)
{
	const char *line;
	char prefix[2] = "";
	const char *value;
	char *str;
	int exists;

	if(argc < 3)
	{
		out("Usage: setfeature <name> <*|server-type>");
		return;
	}

	// Validate feature name
	for(str = argv[1]; *str; str++)
	{
		if(*str != '_' && !isupper(*str))
		{
			error("Invalid feature name (unexpected char: '%c')", *str);
			return;
		}
	}

	// Validate server type
	if(strcmp(argv[2], "*") && strcmp(argv[2], "LEAF") &&
	   strcmp(argv[2], "HUB") && strcmp(argv[2], "STAFF") &&
	   strcmp(argv[2], "BOTS"))
	{
		error("Invalid server type");
		return;
	}

	pgsql_begin();
	exists = pgsql_query_int("SELECT COUNT(*) FROM features WHERE name = $1 AND server_type = $2", stringlist_build(argv[1], argv[2], NULL));
	if(!exists && !readline_yesno("Feature does not exist yet. Add it?", "Yes"))
	{
		pgsql_rollback();
		return;
	}

	// Fetch old value
	str = pgsql_query_str("SELECT value FROM features WHERE name = $1 AND server_type = $2", stringlist_build(argv[1], argv[2], NULL));

	value = readline_noac("Value", exists ? str : NULL);
	if(!value)
	{
		pgsql_rollback();
		return;
	}

	if(exists && !strcmp(value, str))
	{
		out("No change made");
		pgsql_rollback();
		return;
	}

	if(*str)
	{
		out("Overwriting old value `%s'", str);

		pgsql_query("UPDATE	features\
			     SET	value = $1\
			     WHERE	name = $2 AND\
					server_type = $3",
			    0, stringlist_build(value, argv[1], argv[2], NULL));
	}
	else
	{
		pgsql_query("INSERT INTO features\
				(name, value, server_type)\
			     VALUES\
				($1, $2, $3)",
			    0, stringlist_build(argv[1], value, argv[2], NULL));
	}

	pgsql_commit();
	out("Feature `%s' set successfully for `%s'", argv[1], argv[2]);
}

CMD_FUNC(feature_del)
{
	int cnt;

	if(argc < 3)
	{
		out("Usage: delfeature <name> <*|server-type>");
		return;
	}

	// Validate server type
	if(strcmp(argv[2], "*") && strcmp(argv[2], "LEAF") &&
	   strcmp(argv[2], "HUB") && strcmp(argv[2], "STAFF") &&
	   strcmp(argv[2], "BOTS"))
	{
		error("Invalid server type");
		return;
	}

	cnt = pgsql_query_int("SELECT COUNT(*) FROM features WHERE name = $1 AND server_type = $2", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		error("This feature is not set for `%s'", argv[2]);
		return;
	}

	pgsql_query("DELETE FROM features WHERE name = $1 AND server_type = $2", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Feature `%s' deleted successfully from `%s'", argv[1], argv[2]);
}

// Tab completion stuff
CMD_TAB_FUNC(feature_set)
{
	if(CAN_COMPLETE_ARG(1))
		return feature_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
		return feature_all_srvtypes_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(feature_del)
{
	if(CAN_COMPLETE_ARG(1))
		return feature_generator(text, state);
	if(CAN_COMPLETE_ARG(2))
	{
		feat_name_tc = tc_argv[1];
		return feature_srvtype_generator(text, state);
	}
	return NULL;
}

static char *feature_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT DISTINCT name FROM features", 1, NULL);
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

static char *feature_srvtype_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT server_type FROM features WHERE name = $1", 1, stringlist_build(feat_name_tc, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

static char *feature_all_srvtypes_generator(const char *text, int state)
{
	static const char *values[] = { "*", "LEAF", "HUB", "STAFF", "BOTS", NULL };
	static int idx;
	static size_t len;
	const char *val;

	if(!state) // New word
	{
		len = strlen(text);
		idx = 0;
	}
	else if(state == -1) // Cleanup
	{
		return NULL;
	}

	// Return the next name which partially matches from the command list.
	while((val = values[idx]))
	{
		idx++;
		if(!strncasecmp(val, text, len))
			return strdup(val);
	}

  	return NULL;
}
