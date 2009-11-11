#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"
#include "conf.h"

static char *clientmod_arg_generator(const char *text, int state);
static char *client_generator(const char *text, int state);
static char *client_server_generator(const char *text, int state);
CMD_FUNC(client_list);
CMD_TAB_FUNC(client_list);
CMD_FUNC(client_add);
CMD_FUNC(client_del);
CMD_TAB_FUNC(client_del);
CMD_FUNC(client_mod);
CMD_TAB_FUNC(client_mod);

static struct command commands[] = {
	CMD_STUB("client", "Client Authorization Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "client" subcommands
	CMD_TC("list", client_list, "Show client authorizations"),
	CMD("add", client_add, "Add a client authorization"),
	CMD_TC("del", client_del, "Remove a client authorization"),
	CMD_TC("mod", client_mod, "Modify a client authorization"),
	CMD_LIST_END
};

static const char *tc_client = NULL;

void cmd_client_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "client");
	cmd_alias("clients", "client", "list");
	cmd_alias("addclient", "client", "add");
	cmd_alias("delclient", "client", "del");
	cmd_alias("modclient", "client", "mod");
	cmd_alias("clientmod", "client", "mod");
}

CMD_FUNC(client_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT	name,\
					server,\
					connclass,\
					password,\
					ident,\
					class_maxlinks,\
					COALESCE(host, '*') AS host,\
					COALESCE(ip::varchar, '*') AS ip\
			   FROM		clients\
			   ORDER BY	server ASC,\
					name ASC",
			  1, NULL);
	rows = pgsql_num_rows(res);
	table = table_create(8, 0);
	table_set_header(table, "Name", "Server", "Connclass", "Host", "IP", "Ident", "Password", "ClassMax");
	for(int i = 0, table_row = 0; i < rows; i++)
	{
		// Hack to filter by server without modifying the query
		if(argc > 1 && strcasecmp(argv[1], pgsql_nvalue(res, i, "server")))
			continue;

		table_col_str(table, table_row, 0, (char *)pgsql_nvalue(res, i, "name"));
		table_col_str(table, table_row, 1, (char *)pgsql_nvalue(res, i, "server"));
		table_col_str(table, table_row, 2, (char *)pgsql_nvalue(res, i, "connclass"));
		table_col_str(table, table_row, 3, (char *)pgsql_nvalue(res, i, "host"));
		table_col_str(table, table_row, 4, (char *)pgsql_nvalue(res, i, "ip"));
		table_col_str(table, table_row, 5, (char *)pgsql_nvalue(res, i, "ident"));
		table_col_str(table, table_row, 6, (char *)pgsql_nvalue(res, i, "password"));
		table_col_str(table, table_row, 7, (char *)pgsql_nvalue(res, i, "class_maxlinks"));
		table_row++;
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(client_add)
{
	const char *line;
	char *tmp;
	char *name = NULL, *server = NULL, *class = NULL, *ident = NULL;
	char *ip = NULL, *host = NULL, *password = NULL, *class_maxlinks = NULL;
	int cnt;

	// Prompt name
	while(1)
	{
		line = readline_noac("Name", NULL);
		if(!line || !*line)
			return;

		name = strdup(line);
		break;
	}

	// Prompt server name
	while(1)
	{
		line = readline_custom("Server", NULL, server_nohub_generator);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1) AND type != 'HUB'", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A server named `%s' does not exist", line);
			continue;
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM clients WHERE lower(name) = lower($1) AND server = $2", stringlist_build(name, tmp, NULL));
		if(cnt)
		{
			error("There is already a client authorization named `%s' on `%s'", name, tmp);
			continue;
		}

		server = strdup(tmp);
		break;
	}

	// Prompt connclass
	while(1)
	{
		line = readline_connclass("Connclass", conf_str("defaults/client_connclass"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		tmp = pgsql_query_str("SELECT name FROM connclasses_users WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A connclass named `%s' does not exist", line);
			continue;
		}

		class = strdup(tmp);
		break;
	}

	// Prompt ident
	while(1)
	{
		line = readline_noac("Ident", "*");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		if(strlen(line) > 10)
		{
			error("Ident length cannot exceed 10 characters");
			continue;
		}

		ident = strdup(line);
		break;
	}

	// Prompt ip
	while(1)
	{
		line = readline_noac("IP (CIDR)", "");
		if(!line)
			goto out;
		else if(!*line || !strcmp(line, "*"))
		{
			ip = NULL;
			break;
		}

		if(!pgsql_valid_for_type(line, "inet"))
		{
			error("This IP doesn't look like a valid IP mask");
			continue;
		}

		ip = strdup(line);
		break;
	}

	// Prompt host
	line = readline_noac("Host", "");
	if(!line)
		goto out;
	else if(!*line || !strcmp(line, "*"))
		host = NULL;
	else
		host = strdup(line);

	// Prompt password
	line = readline_noac("Password", "");
	if(!line)
		goto out;
	else if(!*line)
		password = NULL;
	else
		password = strdup(line);

	// Prompt class maxlinks
	while(1)
	{
		line = readline_noac("Class Maxlinks (0 for unlimited, empty for default)", "");
		if(!line)
			goto out;
		else if(!*line)
			break;

		long val = strtol(line, &tmp, 10);
		if(*tmp || val < 0)
		{
			error("Maxlinks must be an integer >=0");
			continue;
		}

		class_maxlinks = strdup(line);
		break;
	}

	pgsql_query("INSERT INTO clients\
			(name, server, connclass, ident, ip,\
			 host, password, class_maxlinks)\
		     VALUES\
			($1, $2, $3, $4, $5,\
			 $6, $7, $8)",
		    0, stringlist_build_n(8, name, server, class, ident, ip,
					     host, password, class_maxlinks));
	out("Client authorization `%s' added successfully", name);
	if(class_maxlinks)
		out("Client will use implicit connection class `%s::%s' with maxlinks=%s", class, name, class_maxlinks);

out:
	xfree(name);
	xfree(server);
	xfree(class);
	xfree(ident);
	xfree(ip);
	xfree(host);
	xfree(password);
	xfree(class_maxlinks);
}

CMD_FUNC(client_del)
{
	if(argc < 3)
	{
		out("Usage: delclient <name> <server>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM clients WHERE lower(name) = lower($1) AND lower(server) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		error("There is no client authorization named `%s' on `%s'", argv[1], argv[2]);
		return;
	}

	pgsql_query("DELETE FROM clients WHERE lower(name) = lower($1) AND lower(server) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Client authorization `%s' deleted successfully", argv[1]);
}

CMD_FUNC(client_mod)
{
	char *tmp;
	char name[32], server[63];
	int modified = 0;
	PGresult *res;

	if(argc < 4)
	{
		out("Usage: modclient <name> <server> <args...>");
		return;
	}

	res = pgsql_query("SELECT name, server FROM clients WHERE lower(name) = lower($1) AND lower(server) = lower($2)", 1, stringlist_build(argv[1], argv[2], NULL));
	if(!pgsql_num_rows(res))
	{
		error("A client authorization named `%s' does not exist on `%s'", argv[1], argv[2]);
		pgsql_free(res);
		return;
	}

	strlcpy(name, pgsql_nvalue(res, 0, "name"), sizeof(name));
	strlcpy(server, pgsql_nvalue(res, 0, "server"), sizeof(server));
	pgsql_free(res);

	pgsql_begin();
	for(int i = 3; i < argc; i++)
	{
		if(!strcmp(argv[i], "--class"))
		{
			if(i == argc - 1)
			{
				error("--class needs an argument");
				pgsql_rollback();
				return;
			}

			tmp = pgsql_query_str("SELECT name FROM connclasses_users WHERE name = $1", stringlist_build(argv[i + 1], NULL));
			if(!*tmp)
			{
				error("`%s' is not a valid connection class", argv[i + 1]);
				pgsql_rollback();
				return;
			}

			out("Setting connclass: %s", tmp);
			pgsql_query("UPDATE clients SET connclass = $1 WHERE name = $2 AND server = $3", 0, stringlist_build(tmp, name, server, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--ident"))
		{
			if(i == argc - 1)
			{
				error("--ident needs an argument");
				pgsql_rollback();
				return;
			}

			if(strlen(argv[i + 1]) > 10)
			{
				error("Ident length cannot exceed 10 characters");
				pgsql_rollback();
				return;
			}

			tmp = argv[i + 1];
			if(!*tmp)
				tmp = "*";

			out("Setting ident: %s", tmp);
			pgsql_query("UPDATE clients SET ident = $1 WHERE name = $2 AND server = $3", 0, stringlist_build(tmp, name, server, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--ip"))
		{
			if(i == argc - 1)
			{
				error("--ip needs an argument");
				pgsql_rollback();
				return;
			}

			if(!strcmp(argv[i + 1], "*") || !strlen(argv[i + 1]))
				tmp = NULL;
			else
			{
				if(!pgsql_valid_for_type(argv[i + 1], "inet"))
				{
					error("This IP doesn't look like a valid IP");
					pgsql_rollback();
					return;
				}

				tmp = argv[i + 1];
			}

			out("Setting ip: %s", tmp ? tmp : "*");
			pgsql_query("UPDATE clients SET ip = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--host"))
		{
			if(i == argc - 1)
			{
				error("--host needs an argument");
				pgsql_rollback();
				return;
			}

			if(!strcmp(argv[i + 1], "*") || !strlen(argv[i + 1]))
				tmp = NULL;
			else
				tmp = argv[i + 1];

			out("Setting host: %s", tmp ? tmp : "*");
			pgsql_query("UPDATE clients SET host = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--password"))
		{
			if(i == argc - 1)
			{
				error("--password needs an argument");
				pgsql_rollback();
				return;
			}

			if(!strcmp(argv[i + 1], "*") || !strlen(argv[i + 1]))
				tmp = NULL;
			else
				tmp = argv[i + 1];

			out("Setting password: %s", tmp ? tmp : "\033[" COLOR_DARKGRAY "m(none)\033[0m");
			pgsql_query("UPDATE clients SET password = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--maxlinks"))
		{
			if(i == argc - 1)
			{
				error("--maxlinks needs an argument");
				pgsql_rollback();
				return;
			}

			if(!strcmp(argv[i + 1], "*") || !strlen(argv[i + 1]) || !strcmp(argv[i + 1], "-1"))
				tmp = NULL;
			else
			{
				long val = strtol(argv[i + 1], &tmp, 10);
				if(*tmp || val < 0)
				{
					error("Maxlinks must be an integer >=0");
					pgsql_rollback();
					return;
				}

				tmp = argv[i + 1];
			}

			out("Setting class maxlinks: %s", tmp ? tmp : "\033[" COLOR_DARKGRAY "m(default)\033[0m");
			pgsql_query("UPDATE clients SET class_maxlinks = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
			i++;
			modified = 1;
		}
		else
		{
			error("Unepected argument: %s", argv[i]);
			pgsql_rollback();
			return;
		}
	}

	pgsql_commit();
	if(modified)
		out_color(COLOR_LIME, "Client authorization was updated successfully");
	else
		out_color(COLOR_LIME, "No changes were made");
}

// Tab completion stuff
CMD_TAB_FUNC(client_list)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(client_del)
{
	if(CAN_COMPLETE_ARG(1))
		return client_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_client = tc_argv[1];
		return client_server_generator(text, state);
	}

	return NULL;
}

CMD_TAB_FUNC(client_mod)
{
	if(CAN_COMPLETE_ARG(1))
		return client_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_client = tc_argv[1];
		return client_server_generator(text, state);
	}

	enum {
		ARG_NONE,
		ARG_CLASS
	} arg_type;

	for(int i = 3; i <= tc_argc; i++)
	{
		if(!CAN_COMPLETE_ARG(i))
			continue;

		arg_type = ARG_NONE;
		if(!strcmp(tc_argv[i - 1], "--class"))
			arg_type = ARG_CLASS;
		else if(!strcmp(tc_argv[i - 1], "--ident"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--ip"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--host"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--password"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--maxlinks"))
			return NULL;

		if(!arg_type)
			return clientmod_arg_generator(text, state);

		if(arg_type == ARG_CLASS)
			return connclass_generator(text, state);
	}

	return NULL;
}

static char *clientmod_arg_generator(const char *text, int state)
{
	static int idx;
	static size_t len;
	const char *val;
	static const char *values[] = {
		"--class",
		"--ident",
		"--ip",
		"--host",
		"--password",
		"--maxlinks",
		NULL
	};

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

static char *client_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM clients WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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

static char *client_server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		assert(tc_client);
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT server FROM clients WHERE lower(name) = lower($1)", 1, stringlist_build(tc_client, NULL));
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

