#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "stringbuffer.h"
#include "input.h"

static char *link_server_service_generator(const char *text, int state);
static char *link_server_generator(const char *text, int state);
static char *link_hub_generator(const char *text, int state);
CMD_FUNC(link_list);
CMD_FUNC(link_add);
CMD_FUNC(link_del);
CMD_TAB_FUNC(link_del);
CMD_FUNC(link_autoconnect);
CMD_TAB_FUNC(link_autoconnect);

static const char *tc_server = NULL;
static int tc_existing_link = 0;

static struct command commands[] = {
	CMD_STUB("link", "Link Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "link" subcommands
	CMD("list", link_list, "Show links between servers"),
	CMD("add", link_add, "Add a link between servers"),
	CMD_TC("del", link_del, "Remove a link between servers"),
	CMD_TC("autoconnect", link_autoconnect, "Show/set autoconnect flag of a link"),
	CMD_LIST_END
};



void cmd_link_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "link");
	cmd_alias("links", "link", "list");
	cmd_alias("addlink", "link", "add");
	cmd_alias("dellink", "link", "del");
	cmd_alias("autoconnect", "link", "autoconnect");
}

CMD_FUNC(link_list)
{
	PGresult *res;
	int rows;
	const char *last_server = NULL;

	// Server links
	res = pgsql_query("SELECT	l.server,\
					l.hub,\
					l.autoconnect,\
					p.ip,\
					p.port\
			   FROM		links l\
			   LEFT JOIN	ports p ON (p.id = l.port)\
			   ORDER BY	l.server ASC,\
					l.hub ASC",
			  1, NULL);

	rows = pgsql_num_rows(res);
	if(rows)
		out("Server links:");
	for(int i = 0; i < rows; i++)
	{
		const char *server_name = pgsql_nvalue(res, i, "server");
		const char *ip = pgsql_nvalue(res, i, "ip");
		const char *port = pgsql_nvalue(res, i, "port");
		int autoconnect = !strcasecmp(pgsql_nvalue(res, i, "autoconnect"), "t");

		if(!last_server || strcmp(last_server, server_name))
			out_color(COLOR_WHITE, "  %s:", server_name);

		if(port && ip)
			out_color(autoconnect ? COLOR_CYAN : COLOR_GRAY, "    %s (%s:%s)", pgsql_nvalue(res, i, "hub"), ip, port);
		else if(port)
			out_color(autoconnect ? COLOR_CYAN : COLOR_GRAY, "    %s (%s)", pgsql_nvalue(res, i, "hub"), port);
		else
			out_color(autoconnect ? COLOR_CYAN : COLOR_GRAY, "    %s", pgsql_nvalue(res, i, "hub"));
		last_server = server_name;
	}

	pgsql_free(res);

	// Service links
	res = pgsql_query("SELECT	service,\
					hub\
			   FROM		servicelinks\
			   ORDER BY	service ASC,\
					hub ASC",
			  1, NULL);

	rows = pgsql_num_rows(res);
	if(rows)
		out("\nService links:");
	last_server = NULL;
	for(int i = 0; i < rows; i++)
	{
		const char *server_name = pgsql_nvalue(res, i, "service");

		if(!last_server || strcmp(last_server, server_name))
			out_color(COLOR_WHITE, "  %s:", server_name);
		out_color(COLOR_GRAY, "    %s", pgsql_nvalue(res, i, "hub"));
		last_server = server_name;
	}

	pgsql_free(res);
}

CMD_FUNC(link_add)
{
	const char *line;
	char *server = NULL;
	char *hub = NULL;
	int service = 0;
	PGresult *res;

	out("If you want to allow all servers (excluding services) to connect to a hub, use the server name `*'");
	while(1)
	{
		tc_existing_link = 0;
		line = readline_custom("Server name", NULL, link_server_service_generator);
		if(!line || !*line)
			goto out;

		if(!strcmp(line, "*")) // all servers
			server = NULL;
		else
		{
			char *tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
			if(!*tmp)
			{
				// Check if it's a service
				tmp = pgsql_query_str("SELECT name FROM services WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
				if(*tmp)
					service = 1;
				else
				{
					error("A server named `%s' does not exist", line);
					continue;
				}
			}

			server = strdup(tmp);
		}

		break;
	}

	while(1)
	{
		line = readline_hub("Hub", server ? "*" : NULL);
		if(!line)
			goto out;

		if(!strcmp(line, "*")) // all hubs
		{
			if(!server)
			{
				error("Cannot setup `* -> *' link, you must specify either a server or a hub");
				continue;
			}

			hub = NULL;
		}
		else
		{
			if(server && !strcasecmp(server, line))
			{
				error("A server cannot be linked to itself");
				continue;
			}

			char *tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1) AND type = 'HUB'", stringlist_build(line, NULL));
			if(!*tmp)
			{
				error("A hub named `%s' does not exist", line);
				continue;
			}

			if(server)
			{
				int cnt = pgsql_query_int("SELECT COUNT(*) FROM links WHERE (server = $1 AND hub = $2) OR (server = $2 AND hub = $1)", stringlist_build(server, tmp, NULL));
				if(cnt)
				{
					error("There is already a link between `%s' and `%s'", server, tmp);
					continue;
				}
			}

			hub = strdup(line);
		}

		break;
	}

	if(service) // service -> hub|*
	{
		assert(server);
		if(hub) // service -> hub
		{
			pgsql_query("INSERT INTO servicelinks (service, hub) VALUES ($1, $2)",
				    0, stringlist_build(server, hub, NULL));
			out("`%s' may now connect to `%s'", server, hub);
		}
		else // service -> *
		{
			pgsql_begin();
			pgsql_query("DELETE FROM servicelinks WHERE service = $1", 0, stringlist_build(server, NULL));
			pgsql_query("INSERT INTO servicelinks (service, hub)\
					SELECT	$1, name\
					FROM 	servers\
					WHERE	type = 'HUB'",
				    0, stringlist_build(server, NULL));
			pgsql_commit();
			out("`%s' may now connect to all hubs", server);
		}
	}
	else if(server && hub) // server -> hub
	{
		char *tmp;
		const char *autoconnect;
		int rows;
		const char *port = NULL;

		tmp = pgsql_query_str("SELECT hub FROM links WHERE server = $1 AND autoconnect = true", stringlist_build(server, NULL));
		if(*tmp)
			out("Do you want to autoconnect? In this case the autoconnect to `%s' will be removed.", tmp);
		autoconnect = readline_yesno("Autoconnect?", (*tmp ? "No" : "Yes")) ? "t" : "f";

		pgsql_begin();
		if(*tmp) // Disable old autoconnect
			pgsql_query("UPDATE links SET autoconnect = false WHERE server = $1 AND hub = $2", 0, stringlist_build(server, tmp, NULL));

		res = pgsql_query("SELECT id, port, ip FROM ports WHERE server = $1 AND flag_server = true", 1, stringlist_build(hub, NULL));
		if((rows = pgsql_num_rows(res)))
		{
			const char *default_ip = pgsql_query_str("SELECT irc_ip_priv FROM servers WHERE name = $1", stringlist_build(hub, NULL));;
			out("This hub has the following non-default server ports:");
			for(int i = 0; i < rows; i++)
			{
				const char *ip = pgsql_nvalue(res, i, "ip");
				if(ip)
					out("  [%s] %s:%s", pgsql_nvalue(res, i, "id"), ip, pgsql_nvalue(res, i, "port"));
				else
					out("  [%s] \033[" COLOR_DARKGRAY "m%s\033[0m:%s", pgsql_nvalue(res, i, "id"), default_ip, pgsql_nvalue(res, i, "port"));
			}

			while(1)
			{
				tmp = readline_noac("Port ID", "");
				if(!tmp)
				{
					pgsql_free(res);
					goto out;
				}
				else if(!*tmp)
				{
					port = NULL;
					break;
				}

				if(!atoi(tmp))
				{
					error("The port ID must be a positive integer");
					continue;
				}

				int cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE id = $1 AND server = $2 AND flag_server = true", stringlist_build(tmp, hub, NULL));
				if(!cnt)
				{
					error("A port with this ID does not exist");
					continue;
				}

				// We don't readline_*() anymore until we access port the last time, so there's no need to strdup()
				port = tmp;
				break;
			}
		}
		pgsql_free(res);

		pgsql_query("INSERT INTO links (server, hub, autoconnect, port) VALUES ($1, $2, $3, $4)",
			    0, stringlist_build_n(4, server, hub, autoconnect, port));
		pgsql_commit();
		out("`%s' may now connect to `%s'", server, hub);
	}
	else if(server) // server -> *
	{
		const char *autoconnect;
		while(1)
		{
			autoconnect = readline_hub("Autoconnect to", "");
			if(!autoconnect)
				goto out;
			else if(!*autoconnect)
				break;

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE lower(name) = lower($1)", stringlist_build(autoconnect, NULL));
			if(cnt)
				break;

			error("A server named `%s' does not exist", autoconnect);
			continue;
		}

		pgsql_begin();
		pgsql_query("DELETE FROM links WHERE server = $1", 0, stringlist_build(server, NULL));
		pgsql_query("INSERT INTO links (server, hub, autoconnect)\
				SELECT	$1, name, (name = $2)\
				FROM 	servers\
				WHERE	name != $1::varchar AND type = 'HUB'",
			    0, stringlist_build(server, autoconnect, NULL));
		pgsql_commit();
		out("`%s' may now connect to all hubs", server);
	}
	else if(hub) // * -> hub
	{
		pgsql_begin();
		pgsql_query("DELETE FROM links WHERE hub = $1 AND autoconnect = false", 0, stringlist_build(hub, NULL));
		pgsql_query("INSERT INTO links (server, hub, autoconnect)\
				SELECT	name, $1, false\
				FROM 	servers\
				WHERE	name != $1::varchar AND\
					name NOT IN (\
						SELECT	server\
						FROM	links\
						WHERE	hub = $1\
					) AND\
					name NOT IN (\
						SELECT	hub\
						FROM	links\
						WHERE	server = $1\
					)",
			    0, stringlist_build(hub, NULL));
		pgsql_commit();
		out("All servers may now connect to `%s'", hub);
	}

out:
	xfree(server);
	xfree(hub);
}

CMD_FUNC(link_del)
{
	int service = 0;

	if(argc < 3)
	{
		out("Usage: dellink <server|service> <hub>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM links WHERE lower(server) = lower($1) AND lower(hub) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		cnt = pgsql_query_int("SELECT COUNT(*) FROM servicelinks WHERE lower(service) = lower($1) AND lower(hub) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
		if(!cnt)
		{
			error("There is no link from `%s' to `%s'", argv[1], argv[2]);
			return;
		}

		service = 1;
	}

	if(service)
		pgsql_query("DELETE FROM servicelinks WHERE lower(service) = lower($1) AND lower(hub) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	else
		pgsql_query("DELETE FROM links WHERE lower(server) = lower($1) AND lower(hub) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Link from `%s' to `%s' successfully deleted", argv[1], argv[2]);
}

CMD_FUNC(link_autoconnect)
{
	int cnt;
	char *autoconnect;
	int ac_current, ac_change = -1;

	if(argc < 3)
	{
		out("Usage: autoconnect <server> <hub> [on|off]");
		return;
	}

	if(argc > 3)
	{
		if(true_string(argv[3]))
			ac_change = 1;
		else if(false_string(argv[3]))
			ac_change = 0;
		else
		{
			error("Invalid binary value: `%s'", argv[3]);
			return;
		}
	}

	cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE lower(name) = lower($1) AND type = 'HUB'", stringlist_build(argv[2], NULL));
	if(!cnt)
	{
		error("A hub named `%s' does not exist", argv[2]);
		return;
	}

	autoconnect = pgsql_query_str("SELECT autoconnect FROM links WHERE lower(server) = lower($1) AND lower(hub) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!*autoconnect) // no link found
	{
		error("There is no link from `%s' to `%s'", argv[1], argv[2]);

		// Check vice versa
		autoconnect = pgsql_query_str("SELECT autoconnect FROM links WHERE lower(server) = lower($2) AND lower(hub) = lower($1)", stringlist_build(argv[1], argv[2], NULL));
		if(*autoconnect)
		{
			ac_current = !strcasecmp(autoconnect, "t");
			out("However, autoconnect from `%s' to `%s' is currently \033[%sm%s\033[0m",
			    argv[2], argv[1],
			    (ac_current ? COLOR_CYAN : COLOR_GRAY), (ac_current ? "enabled" : "disabled"));
		}

		return;
	}

	ac_current = !strcasecmp(autoconnect, "t");
	if(ac_change == -1 || ac_current == ac_change)
	{
		out("Autoconnect from `%s' to `%s' is currently \033[%sm%s\033[0m",
		    argv[1], argv[2],
		    (ac_current ? COLOR_CYAN : COLOR_GRAY), (ac_current ? "enabled" : "disabled"));
		return;
	}

	pgsql_begin();
	// Disable autoconnect for other links if necessary
	if(ac_change)
	{
		pgsql_query("UPDATE links SET autoconnect = false WHERE lower(server) = lower($1) AND lower(hub) != lower($2)",
			    0, stringlist_build(argv[1], argv[2], NULL));
	}
	// Enable/Disable autoconnect
	pgsql_query("UPDATE links SET autoconnect = $1 WHERE lower(server) = lower($2) AND lower(hub) = lower($3)",
		    0, stringlist_build(ac_change ? "t" : "f", argv[1], argv[2], NULL));
	pgsql_commit();

	out("Autoconnect from `%s' to `%s' is now \033[%sm%s\033[0m",
	    argv[1], argv[2],
	    (ac_change ? COLOR_CYAN : COLOR_GRAY), (ac_change ? "enabled" : "disabled"));
}

// Tab completion stuff
CMD_TAB_FUNC(link_del)
{
	if(CAN_COMPLETE_ARG(1))
	{
		tc_existing_link = 1;
		return link_server_service_generator(text, state);
	}
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		return link_hub_generator(text, state);
	}

	return NULL;
}

CMD_TAB_FUNC(link_autoconnect)
{
	if(CAN_COMPLETE_ARG(1))
	{
		tc_existing_link = 1;
		return link_server_generator(text, state);
	}
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		return link_hub_generator(text, state);
	}
	else if(CAN_COMPLETE_ARG(3))
		return onoff_generator(text, state);

	return NULL;
}

static char *link_server_service_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		if(tc_existing_link)
		{
			res = pgsql_query("SELECT server FROM links WHERE server ILIKE $1||'%'\
					   UNION\
					   SELECT service FROM servicelinks WHERE service ILIKE $1||'%'",
					  1, stringlist_build(text, NULL));
		}
		else
		{
			res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%'\
					   UNION\
					   SELECT name FROM services WHERE name ILIKE $1||'%'",
					  1, stringlist_build(text, NULL));
		}
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

static char *link_server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		if(tc_existing_link)
		{
			res = pgsql_query("SELECT server FROM links WHERE server ILIKE $1||'%'",
					  1, stringlist_build(text, NULL));
		}
		else
		{
			res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%'",
					  1, stringlist_build(text, NULL));
		}
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

static char *link_hub_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		assert(tc_server);
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT hub FROM links WHERE hub ILIKE $1||'%' AND lower(server) = lower($2)\
				   UNION\
				   SELECT hub FROM servicelinks WHERE hub ILIKE $1||'%' AND lower(service) = lower($2)",
				  1, stringlist_build(text, tc_server, NULL));
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
