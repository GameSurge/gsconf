#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "stringbuffer.h"
#include "input.h"
#include "table.h"
#include "conf.h"
#include "ssh.h"
#include "configs.h"
#include "tokenize.h"
#include "table.h"

static char *server_port_generator(const char *text, int state);
static char *server_jupe_generator(const char *text, int state);
static char *server_webirc_generator(const char *text, int state);
CMD_FUNC(server_info);
CMD_TAB_FUNC(server_info);
CMD_FUNC(server_list);
CMD_FUNC(server_add);
CMD_FUNC(server_edit);
CMD_TAB_FUNC(server_edit);
CMD_FUNC(server_del);
CMD_TAB_FUNC(server_del);
CMD_FUNC(server_install);
CMD_TAB_FUNC(server_install);
CMD_FUNC(server_port_add);
CMD_TAB_FUNC(server_port_add);
CMD_FUNC(server_port_del);
CMD_TAB_FUNC(server_port_del);
CMD_FUNC(server_jupe_add);
CMD_TAB_FUNC(server_jupe_add);
CMD_FUNC(server_jupe_del);
CMD_TAB_FUNC(server_jupe_del);
CMD_FUNC(server_webirc_add);
CMD_TAB_FUNC(server_webirc_add);
CMD_FUNC(server_webirc_del);
CMD_TAB_FUNC(server_webirc_del);
CMD_FUNC(exec);
CMD_TAB_FUNC(exec);

static const char *tc_server = NULL;
static int tc_jupe_add = 0;
static int tc_webirc_add = 0;

static struct command commands[] = {
	CMD_STUB("server", "Server Management"),
	CMD_TC("exec", exec, "Execute a command on a server"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "server" subcommands
	CMD_TC("info", server_info, "Show information about a server"),
	CMD("list", server_list, "Show a server list"),
	CMD("add", server_add, "Add a server"),
	CMD_TC("edit", server_edit, "Edit a server"),
	CMD_TC("del", server_del, "Delete a server"),
	CMD_TC("install", server_install, "Install the ircd on a server"),
	CMD_TC("addport", server_port_add, "Add a port"),
	CMD_TC("delport", server_port_del, "Delete a port"),
	CMD_TC("addjupe", server_jupe_add, "Add a jupe"),
	CMD_TC("deljupe", server_jupe_del, "Delete a jupe"),
	CMD_TC("addwebirc", server_webirc_add, "Add a webirc authorization"),
	CMD_TC("delwebirc", server_webirc_del, "Delete a webirc authorization"),
	CMD_LIST_END
};



void cmd_server_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "server");
	cmd_alias("servers", "server", "list");
	cmd_alias("serverinfo", "server", "info");
	cmd_alias("addserver", "server", "add");
	cmd_alias("editserver", "server", "edit");
	cmd_alias("delserver", "server", "del");
	cmd_alias("install", "server", "install");
	cmd_alias("addport", "server", "addport");
	cmd_alias("delport", "server", "delport");
}

CMD_FUNC(server_info)
{
	struct server_info *server;
	PGresult *res;
	int rows;
	struct table *table;

	if(argc < 2)
	{
		out("Usage: serverinfo <server>");
		return;
	}

	if(!(server = serverinfo_load(argv[1])))
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	// Common information
	serverinfo_show(server);
	putc('\n', stdout);

	// Links to hubs
	res = pgsql_query("SELECT hub, autoconnect FROM links WHERE server = $1 ORDER BY hub ASC", 1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);
	out("Hubs (\033[" COLOR_CYAN "mautoconnect\033[0m):");
	for(int i = 0; i < rows; i++)
	{
		int autoconnect = !strcasecmp(pgsql_nvalue(res, i, "autoconnect"), "t");
		out_color(autoconnect ? COLOR_CYAN : COLOR_GRAY, "  %s", pgsql_nvalue(res, i, "hub"));
	}
	pgsql_free(res);

	// Hub for
	if(server->type == SERVER_HUB)
	{
		putc('\n', stdout);
		res = pgsql_query("SELECT server, autoconnect FROM links WHERE hub = $1 ORDER BY server ASC", 1, stringlist_build(server->name, NULL));
		rows = pgsql_num_rows(res);
		out("Hub for (\033[" COLOR_CYAN "mautoconnecting to this server\033[0m):");
		for(int i = 0; i < rows; i++)
		{
			int autoconnect = !strcasecmp(pgsql_nvalue(res, i, "autoconnect"), "t");
			out_color(autoconnect ? COLOR_CYAN : COLOR_GRAY, "  %s", pgsql_nvalue(res, i, "server"));
		}
		pgsql_free(res);
	}

	// Ports
	res = pgsql_query("SELECT * FROM ports WHERE server = $1 ORDER BY flag_server ASC, port ASC", 1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);
	putc('\n', stdout);
	out("Ports:");
	out("  %s SH \033[" COLOR_DARKGRAY "m%-15s\033[0m (main server port)", server->server_port, server->irc_ip_priv);
	for(int i = 0; i < rows; i++)
	{
		const char *ip;
		char flags[3] = { "C " };
		int flag_server;
		if((flag_server = !strcasecmp(pgsql_nvalue(res, i, "flag_server"), "t")))
			flags[0] = 'S';
		if(!strcasecmp(pgsql_nvalue(res, i, "flag_hidden"), "t"))
			flags[1] = 'H';

		if((ip = pgsql_nvalue(res, i, "ip")))
			out("  %s %s %s", pgsql_nvalue(res, i, "port"), flags, ip);
		else
			out("  %s %s \033[" COLOR_DARKGRAY "m%-15s\033[0m", pgsql_nvalue(res, i, "port"),
			    flags, (flag_server ? server->irc_ip_priv : server->irc_ip_pub));
	}
	pgsql_free(res);

	// Opers
	if(server->type != SERVER_HUB)
	{
		res = pgsql_query("SELECT oper FROM opers2servers WHERE server = $1 ORDER BY oper ASC", 1, stringlist_build(server->name, NULL));
		rows = pgsql_num_rows(res);
		putc('\n', stdout);
		out("Opers:");
		for(int i = 0; i < rows; i++)
			out("  %s", pgsql_nvalue(res, i, "oper"));
		pgsql_free(res);
	}

	// Clients
	if(server->type != SERVER_HUB)
	{
		res = pgsql_query("SELECT	name,\
						connclass,\
						password,\
						(ident || '@' || COALESCE(host, '*')) AS hostmask,\
						(ident || '@' || COALESCE(ip::varchar, '*')) AS ipmask\
				   FROM		clients\
				   WHERE	server = $1\
				   ORDER BY	name ASC",
				  1, stringlist_build(server->name, NULL));
		rows = pgsql_num_rows(res);
		putc('\n', stdout);
		out("Clients:");
		table = table_create(5, rows);
		table->prefix = "  ";
		table_set_header(table, "Name", "Connclass", "Hostmask", "IPmask", "Password");
		for(int i = 0; i < rows; i++)
		{
			table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "name"));
			table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "connclass"));
			table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "hostmask"));
			table_col_str(table, i, 3, (char *)pgsql_nvalue(res, i, "ipmask"));
			table_col_str(table, i, 4, (char *)pgsql_nvalue(res, i, "password"));
		}

		table_send(table);
		table_free(table);
		pgsql_free(res);
	}

	// Jupes
	if(server->type != SERVER_HUB)
	{
		res = pgsql_query("SELECT jupe FROM jupes2servers WHERE server = $1 ORDER BY jupe ASC", 1, stringlist_build(server->name, NULL));
		rows = pgsql_num_rows(res);
		putc('\n', stdout);
		out("Jupes:");
		if(!rows)
			out("  (none)");;
		for(int i = 0; i < rows; i++)
			out("  %s", pgsql_nvalue(res, i, "jupe"));
		pgsql_free(res);
	}

	// Webirc authorizations
	if(server->type != SERVER_HUB)
	{
		res = pgsql_query("SELECT webirc FROM webirc2servers WHERE server = $1 ORDER BY webirc ASC", 1, stringlist_build(server->name, NULL));
		rows = pgsql_num_rows(res);
		putc('\n', stdout);
		out("WebIRC authorizations:");
		if(!rows)
			out("  (none)");;
		for(int i = 0; i < rows; i++)
			out("  %s", pgsql_nvalue(res, i, "webirc"));
		pgsql_free(res);
	}

	serverinfo_free(server);
}

CMD_FUNC(server_list)
{
	struct table *table;
	PGresult *res;
	int rows;

	res = pgsql_query("SELECT name, type, numeric, contact FROM servers ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(4, rows);
	table_set_header(table, "Name", "Type", "Numeric", "Contact");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "type"));
		table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "numeric"));
		table_col_str(table, i, 3, (char *)pgsql_nvalue(res, i, "contact"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(server_add)
{
	struct server_info *data;

	data = serverinfo_prompt(NULL);
	if(!data)
		return;

	// We got all information; insert it into the database
	pgsql_begin();
	pgsql_query("INSERT INTO servers\
			(name, type, description, irc_ip_priv, irc_ip_pub,\
			 numeric, contact, location1, location2, provider,\
			 sno_connexit, ssh_user, ssh_host, ssh_port, link_pass,\
			 server_port)\
		     VALUES\
		     	($1, $2, $3, $4, $5,\
			 $6, $7, $8, $9, $10,\
			 $11, $12, $13, $14, $15,\
			 $16)",
		    0,
		    stringlist_build_n(16,
			data->name, serverinfo_db_from_type(data), data->description, data->irc_ip_priv, data->irc_ip_pub,
			data->numeric, data->contact, data->location1, data->location2, data->provider,
			(data->sno_connexit ? "t" : "f"), data->ssh_user, data->ssh_host, data->ssh_port, data->link_pass,
			data->server_port));

	// Add default ports / client authorizations
	if(data->type == SERVER_LEAF)
	{
		struct stringlist *ports = conf_get("defaults/client_ports", DB_STRINGLIST);
		if(ports)
		{
			for(unsigned int i = 0; i < ports->count; i++)
			{
				out("Adding client port: %s", ports->data[i]);
				pgsql_query("INSERT INTO ports (server, port) VALUES ($1, $2)",
					    0, stringlist_build(data->name, ports->data[i], NULL));
			}
		}

		char *class = conf_str("defaults/client_connclass");
		if(class)
		{
			out("Adding default client authorization: *@* -> %s", class);
			pgsql_query("INSERT INTO clients (name, server, connclass) VALUES ('Default', $1, $2)",
				    0, stringlist_build(data->name, class, NULL));
		}
	}

	// Add jupes
	if(data->type != SERVER_HUB)
	{
		out("Adding all jupes");
		pgsql_query("INSERT INTO jupes2servers (jupe, server) SELECT name, $1 FROM jupes", 0, stringlist_build(data->name, NULL));
	}

	pgsql_commit();
	out("Server `%s' added successfully", data->name);
	serverinfo_free(data);
}

CMD_FUNC(server_edit)
{
	struct server_info *old, *new;

	if(argc < 2)
	{
		out("Usage: editserver <server>");
		return;
	}

	old = serverinfo_load(argv[1]);
	if(!old)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	new = serverinfo_prompt(old);
	if(!new)
	{
		serverinfo_free(old);
		return;
	}

	// Build query to update the server
	struct stringbuffer *query = stringbuffer_create();
	struct stringlist *params = stringlist_create();
	int i = 1;

	stringbuffer_append_string(query, "UPDATE servers SET ");

	if(old->type != new->type)
	{
		if(i != 1)
			stringbuffer_append_string(query, ", ");
		stringbuffer_append_printf(query, "\"type\" = $%u", i++);
		stringlist_add(params, strdup(serverinfo_db_from_type(new)));
	}

	if(old->sno_connexit != new->sno_connexit)
	{
		if(i != 1)
			stringbuffer_append_string(query, ", ");
		stringbuffer_append_printf(query, "\"sno_connexit\" = $%u", i++);
		stringlist_add(params, strdup(new->sno_connexit ? "t" : "f"));
	}

#define FIELD_CHANGED(FIELD)	\
	(\
		(new->FIELD && !old->FIELD) || \
		(!new->FIELD && old->FIELD) || \
		(new->FIELD && old->FIELD && strcmp(new->FIELD, old->FIELD)) \
	)
#define UPDATE_FIELD(FIELD)	\
	if(FIELD_CHANGED(FIELD)) { \
		if(i != 1) \
			stringbuffer_append_string(query, ", "); \
		stringbuffer_append_printf(query, "\"" #FIELD "\" = $%u", i++); \
		stringlist_add(params, new->FIELD ? strdup(new->FIELD) : NULL); \
	}

	UPDATE_FIELD(numeric);
	UPDATE_FIELD(irc_ip_priv);
	UPDATE_FIELD(irc_ip_pub);
	UPDATE_FIELD(description);
	UPDATE_FIELD(contact);
	UPDATE_FIELD(location1);
	UPDATE_FIELD(location2);
	UPDATE_FIELD(provider);
	UPDATE_FIELD(ssh_user);
	UPDATE_FIELD(ssh_host);
	UPDATE_FIELD(ssh_port);
	UPDATE_FIELD(link_pass);
	UPDATE_FIELD(server_port);
#undef UPDATE_FIELD
#undef FIELD_CHANGED

	if(i == 1)
	{
		out("Nothing changed");
		stringbuffer_free(query);
		stringlist_free(params);
		serverinfo_free(new);
		serverinfo_free(old);
		return;
	}

	stringbuffer_append_printf(query, " WHERE name = $%u", i++);
	stringlist_add(params, strdup(new->name));

	debug("Query: %s", query->string);
	for(unsigned int i = 0; i < params->count; i++) debug("$%u = `%s'", i+1, params->data[i]);

	pgsql_query(query->string, 0, params);
	stringbuffer_free(query);

	out("Server %s updated successfully", new->name);

	serverinfo_free(new);
	serverinfo_free(old);
}

CMD_FUNC(server_del)
{
	char *line;
	char promptbuf[128];

	if(argc < 2)
	{
		out("Usage: delserver <server>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	snprintf(promptbuf, sizeof(promptbuf), "Do you really want to delete the server %s?", argv[1]);
	while(1)
	{
		line = readline_noac(promptbuf, "No");
		if(true_string(line))
			break;
		else if(false_string(line))
			return;
	}

	pgsql_query("DELETE FROM servers WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Server `%s' deleted successfully", argv[1]);
	out("Please do not forget to remove ircd and config manually!");
}

CMD_FUNC(server_install)
{
	struct ssh_session *session;
	struct server_info *server;
	char conf_key[32];
	char *src_file;
	char cmd[256];
	const char *cd_src_cmd, *tmp;

	if(argc < 2)
	{
		out("Usage: install <server>");
		return;
	}

	if(!(server = serverinfo_load(argv[1])))
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	snprintf(conf_key, sizeof(conf_key), "ircd_src/%s", serverinfo_db_from_type(server));
	if(!(src_file = conf_str(conf_key)))
	{
		error("Config key `%s' is not set", conf_key);
		serverinfo_free(server);
		return;
	}

	if(!file_exists(src_file))
	{
		error("ircu source archive `%s' does not exist", src_file);
		serverinfo_free(server);
		return;
	}

	out_color(COLOR_BROWN, "Installing `%s'", server->name);

	if(!(session = ssh_open(server)))
	{
		serverinfo_free(server);
		return;
	}

	if(!(cd_src_cmd = conf_str("install_cmds/cd_src")))
		cd_src_cmd = "cd ~/ircu2.10.12";

	if(argc > 2)
	{
		// Allow jumping to various install steps
		if(!strcmp(argv[2], "--reinstall"))
			goto reinstall;
		else if(!strcmp(argv[2], "--unpack"))
			goto unpack;
		else if(!strcmp(argv[2], "--configure"))
			goto configure;
		else if(!strcmp(argv[2], "--build"))
			goto build;
		else if(!strcmp(argv[2], "--install"))
			goto install;
		else
		{
			error("Invalid action: `%s'", argv[2]);
			serverinfo_free(server);
			ssh_close(session);
			return;
		}
	}

	if(ssh_file_exists(session, "ircu"))
	{
		error("ircu directory found. Use `install %s --reinstall' to install anyway", server->name);
		goto out;
	}

reinstall:
	if(ssh_file_exists(session, basename(src_file)))
	{
		error("Source archive found. Use `install %s --unpack' to install anyway", server->name);
		goto out;
	}

	out_color(COLOR_BROWN, "Uploading ircd to `%s'", server->name);
	if(ssh_scp_put(session, src_file, basename(src_file)) != 0)
		goto out;

unpack:
	if(ssh_file_exists(session, "ircu2.10.12"))
	{
		error("Unpacked source found. Use `install %s --configure' to install anyway.", server->name);
		goto out;
	}

	if(!(tmp = conf_str("install_cmds/unpack")))
		tmp = "tar xvf $1";
	expand_num_args(cmd, sizeof(cmd), tmp, 1, basename(src_file));
	out_color(COLOR_BROWN, "Unpacking source: %s", cmd);
	if(ssh_exec_live(session, cmd) != 0)
	{
		error("Unpacking failed. Unpack the source manually, then run `install %s --configure'", server->name);
		goto out;
	}

configure:
	if(!(tmp = conf_str("install_cmds/configure")))
		tmp = "./configure --prefix=$HOME/ircu";
	out_color(COLOR_BROWN, "Configuring ircu: %s", tmp);
	snprintf(cmd, sizeof(cmd), "%s && %s", cd_src_cmd, tmp);
	if(ssh_exec_live(session, cmd) != 0)
	{
		error("Configure failed. Run configure manually, then run `install %s --build'", server->name);
		goto out;
	}

build:
	if(!(tmp = conf_str("install_cmds/make")))
		tmp = "make";
	out_color(COLOR_BROWN, "Building ircu: %s", tmp);
	snprintf(cmd, sizeof(cmd), "%s && %s", cd_src_cmd, tmp);
	if(ssh_exec_live(session, cmd) != 0 || !readline_yesno("Did `make' succeed?", "Yes"))
	{
		error("Make failed. Run make manually, then run `install %s --install'", server->name);
		goto out;
	}

install:
	if(!(tmp = conf_str("install_cmds/install")))
		tmp = "make install";
	out_color(COLOR_BROWN, "Building ircu: %s", tmp);
	snprintf(cmd, sizeof(cmd), "%s && %s", cd_src_cmd, tmp);
	if(ssh_exec_live(session, cmd) != 0)
	{
		error("Make install failed. Run make install manually, then run `putconf %s'", server->name);
		goto out;
	}
	else if(!ssh_file_exists(session, "ircu/bin/ircd"))
	{
		error("Make install succeeded but ``~/ircu/bin/ircd' does not exist. Fix this manually, then run `putconf %s'", server->name);
		goto out;
	}

	out_color(COLOR_LIME, "ircd installed successfully; use `commit' to install the config and `exec %s ~/ircu/bin/ircd' to start it", server->name);

out:
	ssh_close(session);
	serverinfo_free(server);
}

CMD_FUNC(server_port_add)
{
	char *line;
	struct server_info *server;
	char *port = NULL, *ip = NULL;
	int flag_server, flag_hidden;
	int cnt;

	if(argc < 2)
	{
		out("Usage: addport <server>");
		return;
	}

	if(!(server = serverinfo_load(argv[1])))
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	// Prompt port
	while(1)
	{
		line = readline_noac("Port", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		int val = atoi(line);
		if(val <= 1024)
		{
			error("Invalid port, must be a positive number above 1024");
			continue;
		}

		port = strdup(line);
		break;
	}

	// Prompt server flag. Default to yes if it's a 4xxx port.
	flag_server = readline_yesno("Is this a server port?", *port == '4' ? "Yes" : "No");
	// Prompt hidden flag. Default to yes if it's a server port.
	flag_hidden = readline_yesno("This is a hidden port?", flag_server ? "Yes" : "No");

	// Prompt bind ip
	while(1)
	{
		line = readline_noac("IP", "");
		if(!line)
			goto out;
		else if(!*line)
			break;

		if(match("?*.?*.?*.?*", line) && match("?*:?*:?*", line))
		{
			error("This IP doesn't look like a valid IP");
			continue;
		}

		if((flag_server && !strcmp(line, server->irc_ip_priv)) ||
		   (!flag_server && !strcmp(line, server->irc_ip_pub)))
		{
			out("This is the default ip; won't set it explicitely");
			break;
		}

		ip = strdup(line);
		break;
	}

	if(ip)
		cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2 AND ip = $3", stringlist_build(server->name, port, ip, NULL));
	else
		cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2", stringlist_build(server->name, port, NULL));

	if(cnt)
	{
		error("This port already exists");
		goto out;
	}

	pgsql_query("INSERT INTO ports\
			(server, port, ip, flag_server, flag_hidden)\
		     VALUES\
			($1, $2, $3, $4, $5)",
		    0, stringlist_build_n(5, server->name, port, ip,
					     (flag_server ? "t" : "f"),
					     (flag_hidden ? "t" : "f")));

	out("Port %s added successfully", port);

out:
	xfree(port);
	xfree(ip);
	serverinfo_free(server);
}

CMD_FUNC(server_port_del)
{
	char *line;
	struct server_info *server;
	int cnt;
	int server_port;
	char *default_ip = NULL;
	const char *tmp;
	char msg[64];
	char *id;
	PGresult *res;

	if(argc < 3)
	{
		out("Usage: delport <server> <port> [ip]");
		return;
	}

	if(!(server = serverinfo_load(argv[1])))
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	// Check if port exists at all
	cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2", stringlist_build(server->name, argv[2], NULL));
	if(!cnt)
	{
		out("Port %s is not open", argv[2]);
		goto out;
	}

	if(argc < 4)
	{
		// No IP given, check if the port exists on more than one ip
		cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2", stringlist_build(server->name, argv[2], NULL));
		if(cnt > 1)
		{
			char promptbuf[64];
			out("If you want to delete the port only for a single ip, use delport %s %s <ip>", server->name, argv[2]);
			snprintf(promptbuf, sizeof(promptbuf), "Delete this port for all %u ips?", cnt);
			if(!readline_yesno(promptbuf, "Yes"))
				goto out;
		}
	}
	else
	{
		// IP given, check if the port exists on that ip
		cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2 AND ip = $3", stringlist_build(server->name, argv[2], argv[3], NULL));
		if(!cnt)
		{
			server_port = pgsql_query_bool("SELECT flag_server FROM ports WHERE server = $1 AND port = $2", stringlist_build(server->name, argv[2], NULL));
			if((server_port && !strcmp(server->irc_ip_priv, argv[3])) ||
			   (!server_port && !strcmp(server->irc_ip_pub, argv[3])))
			{
				// No port found, but it's one of the default ips -> check if that one exists
				default_ip = server_port ? server->irc_ip_priv : server->irc_ip_pub;
				cnt = pgsql_query_int("SELECT COUNT(*) FROM ports WHERE server = $1 AND port = $2 AND ip ISNULL", stringlist_build(server->name, argv[2], NULL));
			}

			if(!cnt)
			{
				out("Port %s is not open on %s", argv[2], argv[3]);
				goto out;
			}
		}
	}

	if(default_ip)
	{
		tmp = pgsql_query_str("SELECT id FROM ports WHERE server = $1 AND port = $2 AND ip ISNULL", stringlist_build(server->name, argv[2], NULL));
		snprintf(msg, sizeof(msg), "Port %s:%s deleted successfully", default_ip, argv[2]);
	}
	else if(argc > 3)
	{
		if(!pgsql_valid_for_type(argv[3], "inet"))
		{
			error("The IP doesn't look like a valid IP");
			goto out;
		}

		tmp = pgsql_query_str("SELECT id FROM ports WHERE server = $1 AND port = $2 AND ip = $3", stringlist_build(server->name, argv[2], argv[3], NULL));
		snprintf(msg, sizeof(msg), "Port %s:%s deleted successfully", argv[3], argv[2]);
	}
	else
	{
		tmp = pgsql_query_str("SELECT id FROM ports WHERE server = $1 AND port = $2", stringlist_build(server->name, argv[2], NULL));
		snprintf(msg, sizeof(msg), "Port %s deleted successfully", argv[2]);
	}

	assert(*tmp && atoi(tmp) > 0);
	id = strdup(tmp);

	res = pgsql_query("SELECT server FROM links WHERE port = $1", 1, stringlist_build(id, NULL));
	if(pgsql_num_rows(res) != 0)
	{
		error("This port is still used by the following server links:");
		for(int i = 0, rows = pgsql_num_rows(res); i < rows; i++)
			error("  %s -> %s", pgsql_nvalue(res, i, "server"), server->name);
		free(id);
		pgsql_free(res);
		goto out;
	}

	pgsql_free(res);

	pgsql_query("DELETE FROM ports WHERE id = $1", 0, stringlist_build(id, NULL));
	out("%s", msg);
	free(id);

out:
	serverinfo_free(server);
}

CMD_FUNC(server_jupe_add)
{
	int cnt;
	char *server = NULL, *jupe = NULL;
	const char *tmp;

	if(argc < 3)
	{
		out("Usage: server addjupe <server> <jupe>");
		return;
	}

	// Check server and get correctly-cased server name
	tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!*tmp)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	server = strdup(tmp);

	// Check jupe and get correctly-cased jupe name
	tmp = pgsql_query_str("SELECT name FROM jupes WHERE lower(name) = lower($1)", stringlist_build(argv[2], NULL));
	if(!*tmp)
	{
		error("A jupe named `%s' does not exist", argv[2]);
		goto out;
	}

	jupe = strdup(tmp);

	cnt = pgsql_query_int("SELECT COUNT(*) FROM jupes2servers WHERE server = $1 AND jupe = $2", stringlist_build(server, jupe, NULL));
	if(cnt)
	{
		error("This jupe already exists on `%s'", argv[1]);
		goto out;
	}

	pgsql_query("INSERT INTO jupes2servers (jupe, server) VALUES ($1, $2)",
		    0, stringlist_build(jupe, server, NULL));
	out("Jupe `%s' added sucessfully to `%s'", jupe, server);

out:
	xfree(server);
	xfree(jupe);
}

CMD_FUNC(server_jupe_del)
{
	if(argc < 3)
	{
		out("Usage: server deljupe <server> <jupe>");
		return;
	}


	int cnt = pgsql_query_int("SELECT COUNT(*) FROM jupes2servers WHERE lower(server) = lower($1) AND lower(jupe) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		out("This jupe does not exist on `%s'", argv[1]);
		return;
	}

	pgsql_query("DELETE FROM jupes2servers WHERE lower(server) = lower($1) AND lower(jupe) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Jupe `%s' deleted successfully from `%s'", argv[2], argv[1]);
}

CMD_FUNC(server_webirc_add)
{
	int cnt;
	char *server = NULL, *webirc = NULL;
	const char *tmp;

	if(argc < 3)
	{
		out("Usage: server addwebirc <server> <webirc-name>");
		return;
	}

	// Check server and get correctly-cased server name
	tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!*tmp)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	server = strdup(tmp);

	// Check webirc and get correctly-cased webirc name
	tmp = pgsql_query_str("SELECT name FROM webirc WHERE lower(name) = lower($1)", stringlist_build(argv[2], NULL));
	if(!*tmp)
	{
		error("A webirc authorization named `%s' does not exist", argv[2]);
		goto out;
	}

	webirc = strdup(tmp);

	cnt = pgsql_query_int("SELECT COUNT(*) FROM webirc2servers WHERE server = $1 AND webirc = $2", stringlist_build(server, webirc, NULL));
	if(cnt)
	{
		error("This webirc authorization already exists on `%s'", argv[1]);
		goto out;
	}

	pgsql_query("INSERT INTO webirc2servers (webirc, server) VALUES ($1, $2)",
		    0, stringlist_build(webirc, server, NULL));
	out("Webirc authorization `%s' added sucessfully to `%s'", webirc, server);

out:
	xfree(server);
	xfree(webirc);
}

CMD_FUNC(server_webirc_del)
{
	if(argc < 3)
	{
		out("Usage: server delwebirc <server> <webirc-name>");
		return;
	}


	int cnt = pgsql_query_int("SELECT COUNT(*) FROM webirc2servers WHERE lower(server) = lower($1) AND lower(webirc) = lower($2)", stringlist_build(argv[1], argv[2], NULL));
	if(!cnt)
	{
		out("This webirc authorization does not exist on `%s'", argv[1]);
		return;
	}

	pgsql_query("DELETE FROM webirc2servers WHERE lower(server) = lower($1) AND lower(webirc) = lower($2)", 0, stringlist_build(argv[1], argv[2], NULL));
	out("Webirc authorization `%s' deleted successfully from `%s'", argv[2], argv[1]);
}

CMD_FUNC(exec)
{
	struct ssh_session *session;
	struct server_info *server;
	char *cmd;
	int ret;

	if(argc < 3)
	{
		out("Usage: exec <server> <command>");
		return;
	}

	if(!(server = serverinfo_load(argv[1])))
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	if(!(session = ssh_open(server)))
	{
		serverinfo_free(server);
		return;
	}

	// TODO: re-tokenize raw line with limited field count
	cmd = untokenize(argc - 2, argv + 2, " ");
	ssh_exec_live(session, cmd);
	free(cmd);

	ssh_close(session);
	serverinfo_free(server);
}

// Tab completion stuff
CMD_TAB_FUNC(server_info)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(server_edit)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(server_del)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(server_install)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(server_port_add)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(server_port_del)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		return server_port_generator(text, state);
	}
	return NULL;
}

CMD_TAB_FUNC(server_jupe_add)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		tc_jupe_add = 1;
		return server_jupe_generator(text, state);
	}
	return NULL;
}

CMD_TAB_FUNC(server_jupe_del)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		tc_jupe_add = 0;
		return server_jupe_generator(text, state);
	}
	return NULL;
}

CMD_TAB_FUNC(server_webirc_add)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		tc_webirc_add = 1;
		return server_webirc_generator(text, state);
	}
	return NULL;
}

CMD_TAB_FUNC(server_webirc_del)
{
	if(CAN_COMPLETE_ARG(1))
		return server_nohub_generator(text, state);
	else if(CAN_COMPLETE_ARG(2))
	{
		tc_server = tc_argv[1];
		tc_webirc_add = 0;
		return server_webirc_generator(text, state);
	}
	return NULL;
}

CMD_TAB_FUNC(exec)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

static char *server_port_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		assert(tc_server);
		textlen = strlen(text);
		row = 0;
		res = pgsql_query("SELECT port FROM ports WHERE lower(server) = lower($1)", 1, stringlist_build(tc_server, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1)
	{
		pgsql_free(res);
		res = NULL;
		return NULL;
	}

	while(row < rows)
	{
		const char *name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, textlen))
			return strdup(name);
	}

	return NULL;
}

static char *server_jupe_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		assert(tc_server);
		textlen = strlen(text);
		row = 0;
		if(tc_jupe_add)
			res = pgsql_query("SELECT name FROM jupes WHERE name NOT IN (SELECT jupe FROM jupes2servers WHERE lower(server) = lower($1))", 1, stringlist_build(tc_server, NULL));
		else
			res = pgsql_query("SELECT jupe FROM jupes2servers WHERE lower(server) = lower($1)", 1, stringlist_build(tc_server, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1)
	{
		pgsql_free(res);
		res = NULL;
		return NULL;
	}

	while(row < rows)
	{
		const char *name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, textlen))
			return strdup(name);
	}

	return NULL;
}

static char *server_webirc_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		assert(tc_server);
		textlen = strlen(text);
		row = 0;
		if(tc_webirc_add)
			res = pgsql_query("SELECT name FROM webirc WHERE name NOT IN (SELECT webirc FROM webirc2servers WHERE lower(server) = lower($1))", 1, stringlist_build(tc_server, NULL));
		else
			res = pgsql_query("SELECT webirc FROM webirc2servers WHERE lower(server) = lower($1)", 1, stringlist_build(tc_server, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1)
	{
		pgsql_free(res);
		res = NULL;
		return NULL;
	}

	while(row < rows)
	{
		const char *name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, textlen))
			return strdup(name);
	}

	return NULL;
}
