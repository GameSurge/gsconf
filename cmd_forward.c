#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"

static char *forward_generator(const char *text, int state);
CMD_FUNC(forward_list);
CMD_FUNC(forward_add);
CMD_FUNC(forward_del);
CMD_TAB_FUNC(forward_del);

static struct command commands[] = {
	CMD_STUB("forward", "Forward Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "forward" subcommands
	CMD("list", forward_list, "Show forwards"),
	CMD("add", forward_add, "Add a forward"),
	CMD_TC("del", forward_del, "Remove a forward"),
	CMD_LIST_END
};



void cmd_forward_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "forward");
	cmd_alias("forwards", "forward", "list");
	cmd_alias("addforward", "forward", "add");
	cmd_alias("delforward", "forward", "del");
}

CMD_FUNC(forward_list)
{
	PGresult *res;
	int rows;

	res = pgsql_query("SELECT * FROM forwards ORDER BY prefix ASC, server ISNULL DESC", 1, NULL);
	rows = pgsql_num_rows(res);

	out("Forwards:");
	for(int i = 0; i < rows; i++)
	{
		const char *server = pgsql_nvalue(res, i, "server");
		out("  %s -> %s (%s)", pgsql_nvalue(res, i, "prefix"), pgsql_nvalue(res, i, "target"), server ? server : "\033[" COLOR_DARKGRAY "mall servers\033[0m");
	}

	pgsql_free(res);
}

CMD_FUNC(forward_add)
{
	const char *line;
	char prefix[2] = "";
	char *target = NULL;
	char *server = NULL;
	int cnt;

	// Prompt prefix
	while(1)
	{
		line = readline_noac("Prefix", NULL);
		if(!line || !*line)
			return;

		if(strlen(line) != 1)
		{
			error("Prefix must be exactly 1 char");
			continue;
		}

		*prefix = *line;
		break;
	}

	// Prompt target
	while(1)
	{
		line = readline_noac("Target", NULL);
		if(!line)
			return;
		else if(!*line)
			continue;

		if(!strchr(line, '.') || line[0] == '.' || line[strlen(line) - 1] == '.')
		{
			error("Target must contain a `.' which is not at the beginning/end");
			continue;
		}

		target = strdup(line);
		break;
	}

	// Prompt server
	out("If you want to use the forward on all servers, leave the field empty");
	while(1)
	{
		line = readline_server("Server", "");
		if(!line)
			goto out;
		else if(!*line)
			break;

		char *tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A server named `%s' does not exist", line);
			continue;
		}

		server = strdup(tmp);
		break;
	}

	// Check if forward exists
	if(server)
	{
		cnt = pgsql_query_int("SELECT	COUNT(*)\
				       FROM	forwards\
				       WHERE	prefix = $1 AND\
						server = $2",
				      stringlist_build(prefix, server, NULL));
	}
	else
	{
		cnt = pgsql_query_int("SELECT	COUNT(*)\
				       FROM	forwards\
				       WHERE	prefix = $1 AND\
						server ISNULL",
				      stringlist_build(prefix, NULL));
	}

	if(cnt)
	{
		error("A forward with this prefix already exists");
		goto out;
	}

	pgsql_query("INSERT INTO forwards\
			(prefix, target, server)\
		     VALUES\
			($1, $2, $3)",
		    0, stringlist_build_n(3, prefix, target, server));
	out("Forward `%s -> %s` added successfully", prefix, target);

out:
	xfree(target);
	xfree(server);
}

CMD_FUNC(forward_del)
{
	int cnt;

	if(argc < 2)
	{
		out("Usage: forward del <prefix> [server]");
		return;
	}

	if(argc > 2)
	{
		cnt = pgsql_query_int("SELECT COUNT(*) FROM forwards WHERE prefix = $1 AND lower(server) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
		if(!cnt)
		{
			error("This forward does not exist on `%s'", argv[2]);
			return;
		}

		pgsql_query("DELETE FROM forwards WHERE prefix = $1 AND lower(server) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
		out("Forward `%s' deleted successfully from `%s'", argv[1], argv[2]);
	}
	else
	{
		cnt = pgsql_query_int("SELECT COUNT(*) FROM forwards WHERE prefix = $1 AND server ISNULL", stringlist_build(argv[1], NULL));
		if(!cnt)
		{
			error("This forward does not exist");
			return;
		}

		pgsql_query("DELETE FROM forwards WHERE prefix = $1 AND server ISNULL", 0, stringlist_build(argv[1], NULL));
		out("Forward `%s' deleted successfully", argv[1]);
	}
}

// Tab completion stuff
CMD_TAB_FUNC(forward_del)
{
	if(CAN_COMPLETE_ARG(1))
		return forward_generator(text, state);
	if(CAN_COMPLETE_ARG(2))
		return server_generator(text, state);
	return NULL;
}

static char *forward_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	// We don't want readline to mess around with '~'
	rl_filename_completion_desired = 0;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT prefix FROM forwards", 1, NULL);
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

