#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"

static char *pseudo_generator(const char *text, int state);
static char *pseudo_server_generator(const char *text, int state);
CMD_FUNC(pseudo_list);
CMD_FUNC(pseudo_add);
CMD_FUNC(pseudo_del);
CMD_TAB_FUNC(pseudo_del);

static struct command commands[] = {
	CMD_STUB("pseudo", "Service Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "pseudo" subcommands
	CMD("list", pseudo_list, "Show pseudo-commands"),
	CMD("add", pseudo_add, "Add a pseudo-command"),
	CMD_TC("del", pseudo_del, "Remove a pseudo-command"),
	CMD_LIST_END
};

static const char *tc_pseudo = NULL;

void cmd_pseudo_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "pseudo");
	cmd_alias("pseudos", "pseudo", "list");
	cmd_alias("addpseudo", "pseudo", "add");
	cmd_alias("delpseudo", "pseudo", "del");
}

CMD_FUNC(pseudo_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT	command,\
					name,\
					target,\
					prepend,\
					COALESCE(server, '*') AS server\
			   FROM		pseudos\
			   ORDER BY	command ASC,\
					server ISNULL DESC,\
					server ASC",
			  1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(5, rows);
	table_set_header(table, "Command", "Name", "Target", "Server", "Prepend");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char*)pgsql_nvalue(res, i, "command"));
		table_col_str(table, i, 1, (char*)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 2, (char*)pgsql_nvalue(res, i, "target"));
		table_col_str(table, i, 3, (char*)pgsql_nvalue(res, i, "server"));
		table_col_str(table, i, 4, (char*)pgsql_nvalue(res, i, "prepend"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(pseudo_add)
{
	const char *line, *tmp;
	char *command = NULL, *server = NULL, *name = NULL, *target = NULL, *prepend = NULL;
	int cnt;

	// Prompt command
	while(1)
	{
		line = readline_noac("Command", NULL);
		if(!line || !*line)
			return;

		if(strchr(line, ' '))
		{
			error("Command may not contain spaces");
			continue;
		}

		command = strdup(line);
		break;
	}

	// Prompt server name
	out("Use `*' if you want the pseudocommand to be used on all servers which don't override it.");
	while(1)
	{
		line = readline_custom("Server", NULL, server_nohub_generator);
		if(!line)
			return;
		else if(!*line)
			goto out;

		if(!strcmp(line, "*"))
			server = NULL;
		else
		{
			tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1) AND type != 'HUB'", stringlist_build(line, NULL));
			if(!*tmp)
			{
				error("A server named `%s' does not exist", line);
				continue;
			}

			server = strdup(tmp);
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM pseudos WHERE lower(command) = lower($1) AND (server = $2 OR (server ISNULL AND $2 ISNULL))", stringlist_build_n(2, command, server));
		if(cnt)
		{
			error("There is already a pseudocommand named `%s' on `%s'", command, line);
			xfree(server);
			continue;
		}

		break;
	}

	// Prompt name
	while(1)
	{
		line = readline_noac("Service name", NULL);
		if(!line)
			return;
		else if(!*line)
			continue;

		name = strdup(line);
		break;
	}

	// Prompt target
	while(1)
	{
		line = readline_noac("Target (nick@server)", NULL);
		if(!line)
			return;
		else if(!*line)
			continue;

		if(match("*?@?*", line))
		{
			error("Target must be in nick@server format");
			continue;
		}

		target = strdup(line);
		break;
	}

	// Prompt prepend string
	while(1)
	{
		line = readline_noac("Prepend", "");
		if(!line)
			return;

		if(*line)
			prepend = strdup(line);
		break;
	}

	pgsql_query("INSERT INTO pseudos\
			(command, name, target, prepend, server)\
		     VALUES\
			(upper($1), $2, $3, $4, $5)",
		    0, stringlist_build_n(5, command, name, target, prepend, server));
	out("Pseudo-command `%s' added successfully", command);

out:
	xfree(command);
	xfree(server);
	xfree(name);
	xfree(target);
	xfree(prepend);
}

CMD_FUNC(pseudo_del)
{
	if(argc < 2)
	{
		out("Usage: delpseudo <command> <server>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM pseudos WHERE lower(command) = lower($1) AND (lower(server) = lower($2) OR (server ISNULL AND $2 = '*'))", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		error("There is no pseudo named `%s' on `%s'", argv[1], argv[2]);
		return;
	}

	pgsql_query("DELETE FROM pseudos WHERE lower(command) = lower($1) AND (lower(server) = lower($2) OR (server ISNULL AND $2 = '*'))", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Pseudo-command `%s' deleted successfully", argv[1]);
}

// Tab completion stuff
CMD_TAB_FUNC(pseudo_del)
{
	if(CAN_COMPLETE_ARG(1))
		return pseudo_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_pseudo = tc_argv[1];
		return pseudo_server_generator(text, state);
	}

	return NULL;
}

static char *pseudo_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT command FROM pseudos WHERE command ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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

static char *pseudo_server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		assert(tc_pseudo);
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT COALESCE(server, '*') FROM pseudos WHERE lower(command) = lower($1)", 1, stringlist_build(tc_pseudo, NULL));
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

