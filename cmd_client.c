#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"
#include "conf.h"

static void show_clientgroup_clients(const char *group, const char *server);
static char *clientgroupmod_arg_generator(const char *text, int state);
static char *clientgroup_generator(const char *text, int state);
static char *clientgroup_server_generator(const char *text, int state);
CMD_FUNC(client_list);
CMD_TAB_FUNC(client_list);
CMD_FUNC(client_addgroup);
CMD_FUNC(client_delgroup);
CMD_TAB_FUNC(client_delgroup);
CMD_FUNC(client_modgroup);
CMD_TAB_FUNC(client_modgroup);
CMD_FUNC(client_editclients);
CMD_TAB_FUNC(client_editclients);

static struct command commands[] = {
	CMD_STUB("client", "Client Authorization Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "client" subcommands
	CMD_TC("list", client_list, "Show client authorizations"),
	CMD("addgroup", client_addgroup, "Add a client authorization group and a client authorization"),
	CMD_TC("delgroup", client_delgroup, "Remove a client authorization group"),
	CMD_TC("modgroup", client_modgroup, "Modify a client authorization group"),
	CMD_TC("editclients", client_editclients, "Edit the clients in a client authorization group"),
	CMD_LIST_END
};

static const char *tc_clientgroup = NULL;

void cmd_client_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "client");
	cmd_alias("clients", "client", "list");
	cmd_alias("addclientgroup", "client", "addgroup");
	cmd_alias("delclientgroup", "client", "delgroup");
	cmd_alias("modclientgroup", "client", "modgroup");
	cmd_alias("clientgroupmod", "client", "modgroup");
	cmd_alias("editclients", "client", "editclients");
}

CMD_FUNC(client_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT	cg.name,\
					cg.server,\
					cg.connclass,\
					cg.password,\
					cl.ident,\
					cg.class_maxlinks,\
					COALESCE(cl.host, '*') AS host,\
					COALESCE(cl.ip::varchar, '*') AS ip,\
					(cl.group IS NOT NULL) AS has_clients\
			   FROM		clientgroups cg\
			   LEFT JOIN	clients cl ON (cl.group = cg.name AND cl.server = cg.server)\
			   ORDER BY	cg.server ASC,\
					cg.name ASC",
			  1, NULL);
	rows = pgsql_num_rows(res);
	table = table_create(8, 0);
	table->field_len = table_strlen_colors;
	table_free_column(table, 0, 1);
	table_set_header(table, "Name", "Server", "Connclass", "Host", "IP", "Ident", "Password", "ClassMax");
	for(int i = 0, table_row = 0; i < rows; i++)
	{
		char buf[128];
		int has_clients = !strcasecmp(pgsql_nvalue(res, i, "has_clients"), "t");

		// Hack to filter by server without modifying the query
		if(argc > 1 && strcasecmp(argv[1], pgsql_nvalue(res, i, "server")))
			continue;

		if(has_clients)
			snprintf(buf, sizeof(buf), "%s", pgsql_nvalue(res, i, "name"));
		else
			snprintf(buf, sizeof(buf), "\033[" COLOR_LIGHT_RED "m%s\033[0m", pgsql_nvalue(res, i, "name"));

		table_col_str(table, table_row, 0, strdup(buf));
		table_col_str(table, table_row, 1, (char *)pgsql_nvalue(res, i, "server"));
		table_col_str(table, table_row, 2, (char *)pgsql_nvalue(res, i, "connclass"));
		if(has_clients)
		{
			table_col_str(table, table_row, 3, (char *)pgsql_nvalue(res, i, "host"));
			table_col_str(table, table_row, 4, (char *)pgsql_nvalue(res, i, "ip"));
			table_col_str(table, table_row, 5, (char *)pgsql_nvalue(res, i, "ident"));
		}
		else
		{
			table_col_str(table, table_row, 3, "\033[" COLOR_DARKGRAY "mnone\033[0m");
			table_col_str(table, table_row, 4, "\033[" COLOR_DARKGRAY "mnone\033[0m");
			table_col_str(table, table_row, 5, "\033[" COLOR_DARKGRAY "mnone\033[0m");
		}
		table_col_str(table, table_row, 6, (char *)pgsql_nvalue(res, i, "password"));
		table_col_str(table, table_row, 7, (char *)pgsql_nvalue(res, i, "class_maxlinks"));
		table_row++;
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(client_addgroup)
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

		tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A server named `%s' does not exist", line);
			continue;
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM clientgroups WHERE lower(name) = lower($1) AND server = $2", stringlist_build(name, tmp, NULL));
		if(cnt)
		{
			error("There is already a client authorization group named `%s' on `%s'", name, tmp);
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

	pgsql_begin();
	pgsql_query("INSERT INTO clientgroups\
			(name, server, connclass, password, class_maxlinks)\
		     VALUES\
			($1, $2, $3, $4, $5)",
		    0, stringlist_build_n(5, name, server, class, password, class_maxlinks));
	pgsql_query("INSERT INTO clients\
			(\"group\", server, ident, ip, host)\
		     VALUES\
			($1, $2, $3, $4, $5)",
		    0, stringlist_build_n(5, name, server, ident, ip, host));
	pgsql_commit();
	out("Client authorization group `%s' added successfully", name);
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

CMD_FUNC(client_delgroup)
{
	if(argc < 3)
	{
		out("Usage: delclientgroup <groupname> <server>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM clientgroups WHERE lower(name) = lower($1) AND lower(server) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		error("There is no client authorization group named `%s' on `%s'", argv[1], argv[2]);
		return;
	}

	pgsql_query("DELETE FROM clientgroups WHERE lower(name) = lower($1) AND lower(server) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Client authorization group `%s' deleted successfully", argv[1]);
}

CMD_FUNC(client_modgroup)
{
	char *tmp;
	char name[32], server[63];
	int modified = 0;
	PGresult *res;

	if(argc < 4)
	{
		out("Usage: modclient <groupname> <server> <args...>");
		return;
	}

	res = pgsql_query("SELECT name, server FROM clientgroups WHERE lower(name) = lower($1) AND lower(server) = lower($2)", 1, stringlist_build(argv[1], argv[2], NULL));
	if(!pgsql_num_rows(res))
	{
		error("A client authorization group named `%s' does not exist on `%s'", argv[1], argv[2]);
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
			pgsql_query("UPDATE clientgroups SET connclass = $1 WHERE name = $2 AND server = $3", 0, stringlist_build(tmp, name, server, NULL));
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
			pgsql_query("UPDATE clientgroups SET password = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
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
			pgsql_query("UPDATE clientgroups SET class_maxlinks = $1 WHERE name = $2 AND server = $3", 0, stringlist_build_n(3, tmp, name, server));
			i++;
			modified = 1;
		}
		else
		{
			error("Unexpected argument: %s", argv[i]);
			pgsql_rollback();
			return;
		}
	}

	pgsql_commit();
	if(modified)
		out_color(COLOR_LIME, "Client authorization group was updated successfully");
	else
		out_color(COLOR_LIME, "No changes were made");
}

static void show_clientgroup_clients(const char *group, const char *server)
{
	PGresult *res;
	int rows;

	res = pgsql_query("SELECT	id,\
					ident,\
					COALESCE(host, '*') AS host,\
					COALESCE(ip::varchar, '*') AS ip\
			   FROM		clients\
			   WHERE	\"group\" = $1 AND\
			   		server = $2\
			   ORDER BY	id ASC",
			  1, stringlist_build(group, server, NULL));
	if((rows = pgsql_num_rows(res)))
	{
		struct table *table;

		table = table_create(4, pgsql_num_rows(res));
		table_set_header(table, "ID", "Host", "IP", "Ident");
		for(int i = 0; i < rows; i++)
		{
			table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "id"));
			table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "host"));
			table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "ip"));
			table_col_str(table, i, 3, (char *)pgsql_nvalue(res, i, "ident"));
		}

		putc('\n', stdout);
		out("This group contains the following client authorizations:");
		table_send(table);
		table_free(table);
	}
	pgsql_free(res);
}

CMD_FUNC(client_editclients)
{
	char group[32], server[63];
	PGresult *res;
	int rows;
	const char *line;

	if(argc < 3)
	{
		out("Usage: editclients <groupname> <server>");
		return;
	}

	res = pgsql_query("SELECT * FROM clientgroups WHERE lower(name) = lower($1) AND lower(server) = lower($2)", 1, stringlist_build(argv[1], argv[2], NULL));
	if(!pgsql_num_rows(res))
	{
		error("A client authorization group named `%s' does not exist on `%s'", argv[1], argv[2]);
		pgsql_free(res);
		return;
	}

	// Show group information
	out("Name:      %s", pgsql_nvalue(res, 0, "name"));
	out("Server:    %s", pgsql_nvalue(res, 0, "server"));
	out("Connclass: %s", pgsql_nvalue(res, 0, "connclass"));
	out("Password:  %s", pgsql_nvalue(res, 0, "password"));
	out("ClassMax:  %s", pgsql_nvalue(res, 0, "class_maxlinks"));

	// Copy exact group information so we don't need lower() later
	strlcpy(group, pgsql_nvalue(res, 0, "name"), sizeof(group));
	strlcpy(server, pgsql_nvalue(res, 0, "server"), sizeof(server));
	pgsql_free(res);

	show_clientgroup_clients(group, server);
	while(1)
	{
		putc('\n', stdout);
		out("To delete a client authorization, enter its ID; to add a new one, press ENTER; to abort, press CTRL+D");
		line = readline_noac("Delete authorization", "");
		if(!line)
			break;
		else if(*line)
		{
			// Delete client authorization
			res = pgsql_query("DELETE FROM clients WHERE \"group\" = $1 AND server = $2 AND id = $3",
					  1, stringlist_build(group, server, line, NULL));
			if(!pgsql_num_affected(res))
				out("A client authorization with this ID does not exist in this group.");
			else
			{
				out("Client authorization has been deleted successfully.");
				show_clientgroup_clients(group, server);
			}
			pgsql_free(res);
		}
		else
		{
			// Add a new client authorization
			out("Adding a new client authorization; to abort, press CTRL+D.");
			char *ident, *ip, *host;
			ident = ip = host = NULL;

			// Prompt ident
			while(1)
			{
				line = readline_noac("Ident", "*");
				if(!line)
					goto cancel_add;
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
					goto cancel_add;
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
				goto cancel_add;
			else if(!*line || !strcmp(line, "*"))
				host = NULL;
			else
				host = strdup(line);

			pgsql_query("INSERT INTO clients\
					(\"group\", server, ident, ip, host)\
				     VALUES\
					($1, $2, $3, $4, $5)",
				    0, stringlist_build_n(5, group, server, ident, ip, host));
			out("Client authorization has been added successfully.");
			show_clientgroup_clients(group, server);

cancel_add:
			xfree(ident);
			xfree(ip);
			xfree(host);
		}

		if(!readline_yesno("Continue editing this client authorization?", "No"))
			break;
	}
}

// Tab completion stuff
CMD_TAB_FUNC(client_list)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(client_delgroup)
{
	if(CAN_COMPLETE_ARG(1))
		return clientgroup_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_clientgroup = tc_argv[1];
		return clientgroup_server_generator(text, state);
	}

	return NULL;
}

CMD_TAB_FUNC(client_modgroup)
{
	if(CAN_COMPLETE_ARG(1))
		return clientgroup_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_clientgroup = tc_argv[1];
		return clientgroup_server_generator(text, state);
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
		else if(!strcmp(tc_argv[i - 1], "--password"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--maxlinks"))
			return NULL;

		if(!arg_type)
			return clientgroupmod_arg_generator(text, state);

		if(arg_type == ARG_CLASS)
			return connclass_generator(text, state);
	}

	return NULL;
}

CMD_TAB_FUNC(client_editclients)
{
	if(CAN_COMPLETE_ARG(1))
		return clientgroup_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_clientgroup = tc_argv[1];
		return clientgroup_server_generator(text, state);
	}

	return NULL;
}

static char *clientgroupmod_arg_generator(const char *text, int state)
{
	static int idx;
	static size_t len;
	const char *val;
	static const char *values[] = {
		"--class",
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

static char *clientgroup_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM clientgroups WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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

static char *clientgroup_server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		assert(tc_clientgroup);
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT server FROM clientgroups WHERE lower(name) = lower($1)", 1, stringlist_build(tc_clientgroup, NULL));
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

