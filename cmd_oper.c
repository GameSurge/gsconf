#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "input.h"
#include "tokenize.h"
#include "ircd_crypt_smd5.h"
#include "table.h"
#include "stringbuffer.h"
#include <search.h>

static const char *prompt_operpass(int allow_none);
static char *opermod_arg_generator(const char *text, int state);
static char *opermod_mask_generator(const char *text, int state);
static char *opermod_priv_generator(const char *text, int state);
static char *opermod_priv_value_generator(const char *text, int state);
static char *opermod_server_generator(const char *text, int state);
static char *oper_generator(const char *text, int state);
CMD_FUNC(oper_list);
CMD_FUNC(oper_info);
CMD_TAB_FUNC(oper_info);
CMD_FUNC(oper_add);
CMD_FUNC(oper_mod);
CMD_TAB_FUNC(oper_mod);
CMD_FUNC(oper_del);
CMD_TAB_FUNC(oper_del);

static const char *opermod_tc_oper = NULL;
static int opermod_server_adding = 0;

static const char *oper_privs[] = { "local", "umode_nochan", "umode_noidle", "umode_chserv", "notargetlimit", "flood", "pseudoflood", "gline_immune", "die", "restart", NULL };

static struct command commands[] = {
	CMD_STUB("oper", "Oper Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	CMD("list", oper_list, "Show an oper list"),
	CMD_TC("info", oper_info, "Show information about an oper"),
	CMD("add", oper_add, "Add an oper"),
	CMD_TC("del", oper_del, "Remove an oper"),
	CMD_TC("mod", oper_mod, "Modify an oper"),
	CMD_TC("edit", oper_mod, "Modify an oper"),
	CMD_LIST_END
};



void cmd_oper_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "oper");
	cmd_alias("opers", "oper", "list");
	cmd_alias("operinfo", "oper", "info");
	cmd_alias("addoper", "oper", "add");
	cmd_alias("deloper", "oper", "del");
	cmd_alias("modoper", "oper", "mod");
	cmd_alias("opermod", "oper", "mod");
}

CMD_FUNC(oper_list)
{
	struct table *table;
	PGresult *res;
	int rows;
	struct stringbuffer *serverlist = stringbuffer_create();
	int oper_start_row, oper_table_row;

	res = pgsql_query("SELECT	o.name,\
					o.username,\
					o.connclass,\
					o.priv_local AS oper_local,\
					c.priv_local AS class_local,\
					split_part(o2s.server, '.', 1) AS server\
			   FROM		opers o\
			   JOIN		connclasses_users c ON (c.name = o.connclass)\
			   LEFT JOIN	opers2servers o2s ON (o2s.oper = o.name)\
			   ORDER BY	lower(o.name) ASC,\
			   		o2s.server ASC",
			  1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(5, 0);
	table_set_header(table, "Name", "Username", "Connclass", "Global", "Servers");
	table_free_column(table, 4, 1);

	int table_row = 0;
	for(int i = 0; i < rows; i++)
	{
		if(i == 0 || strcmp(pgsql_nvalue(res, i, "name"), pgsql_nvalue(res, i - 1, "name"))) // 1st row
		{
			int local, oper_local, class_local;
			const char *server = pgsql_nvalue(res, i, "server");

			oper_start_row = i;
			oper_table_row = table_row;
			serverlist->len = 0;
			stringbuffer_append_string(serverlist, server ? server : "");

			oper_local = atoi(pgsql_nvalue(res, i, "oper_local"));
			class_local = atoi(pgsql_nvalue(res, i, "class_local"));
			local = (oper_local == -1) ? class_local : oper_local;

			table_col_str(table, table_row, 0, (char *)pgsql_nvalue(res, i, "name"));
			table_col_str(table, table_row, 1, (char *)pgsql_nvalue(res, i, "username"));
			table_col_str(table, table_row, 2, (char *)pgsql_nvalue(res, i, "connclass"));
			if(local != -1)
				table_col_str(table, table_row, 3, local ? "No" : "Yes");
			else
				table_col_str(table, table_row, 3, "Invalid");
		}
		else
		{
			// We have 3 servers and got another one -> put them in the table and start a new row
			if((i - oper_start_row) == 3)
			{
				table_col_str(table, oper_table_row, 4, strdup(serverlist->string));
				serverlist->len = 0;
				table_row++;
				oper_table_row = table_row;
				oper_start_row = i;
			}

			// Add server to list
			if(serverlist->len)
				stringbuffer_append_string(serverlist, ", ");
			stringbuffer_append_string(serverlist, pgsql_nvalue(res, i, "server"));
		}

		// If we are finished with the oper (EOF or new oper starting) put the servers in the table
		if(i == rows - 1 || strcmp(pgsql_nvalue(res, i, "name"), pgsql_nvalue(res, i + 1, "name")))
		{
			table_col_str(table, oper_table_row, 4, strdup(serverlist->string));
			if(i < rows - 1)
				table_row++;
		}
	}

	stringbuffer_free(serverlist);
	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(oper_info)
{
	PGresult *res;
	int priv_oper, priv_class;
	int local;
	const char *typestr;
	int rows;

	if(argc < 2)
	{
		out("Usage: operinfo <name>");
		return;
	}

	res = pgsql_query("SELECT	o.name,\
					o.username,\
					split_part(o.password, '$', 2) AS encryption,\
					o.connclass,\
					o.active,\
					o.priv_local AS oper_priv_local,\
					o.priv_umode_nochan AS oper_priv_umode_nochan,\
					o.priv_umode_noidle AS oper_priv_umode_noidle,\
					o.priv_umode_chserv AS oper_priv_umode_chserv,\
					o.priv_notargetlimit AS oper_priv_notargetlimit,\
					o.priv_flood AS oper_priv_flood,\
					o.priv_pseudoflood AS oper_priv_pseudoflood,\
					o.priv_gline_immune AS oper_priv_gline_immune,\
					o.priv_die AS oper_priv_die,\
					o.priv_restart AS oper_priv_restart,\
					c.priv_local AS class_priv_local,\
					c.priv_umode_nochan AS class_priv_umode_nochan,\
					c.priv_umode_noidle AS class_priv_umode_noidle,\
					c.priv_umode_chserv AS class_priv_umode_chserv,\
					c.priv_notargetlimit AS class_priv_notargetlimit,\
					c.priv_flood AS class_priv_flood,\
					c.priv_pseudoflood AS class_priv_pseudoflood,\
					c.priv_gline_immune AS class_priv_gline_immune,\
					c.priv_die AS class_priv_die,\
					c.priv_restart AS class_priv_restart\
			   FROM		opers o\
			   JOIN		connclasses_users c ON (c.name = o.connclass)\
			   WHERE	lower(o.name) = lower($1)",
			1, stringlist_build(argv[1], NULL));

	if(!pgsql_num_rows(res))
	{
		error("An oper named `%s' does not exist", argv[1]);
		pgsql_free(res);
		return;
	}

	priv_oper = atoi(pgsql_nvalue(res, 0, "oper_priv_local"));
	priv_class = atoi(pgsql_nvalue(res, 0, "class_priv_local"));
	if(priv_oper == -1 && priv_class == -1)
	{
		local = 0; // it doesn't really matter. the oper block won't work
		typestr = "\033[1;31mInvalid\033[0m";
		error("This oper is neither local nor global.");
		error("Set the local priv either for the connclass or the oper!");
	}
	else if((priv_oper == -1 && priv_class == 1) || priv_oper == 1)
	{
		local = 1;
		typestr = "\033[0;33mLocal\033[0m";
	}
	else
	{
		local = 0;
		typestr = "\033[1;32mGlobal\033[0m";
	}

	out("Name:      %s", pgsql_nvalue(res, 0, "name"));
	out("Username:  %s", pgsql_nvalue(res, 0, "username"));
	out("Pass enc:  %s", pgsql_nvalue(res, 0, "encryption"));
	out("Connclass: %s", pgsql_nvalue(res, 0, "connclass"));
	out("Status:    %s, %s", !strcasecmp(pgsql_nvalue(res, 0, "active"), "t") ? "Active" : "Inactive", typestr);
	putc('\n', stdout);


#define PRIVCOL_O_0	"\033[38;5;196mno\033[0m"
#define PRIVCOL_C_0	"\033[38;5;88mno\033[0m"
#define PRIVCOL_I_0	"\033[38;5;89mno\033[0m"

#define PRIVCOL_O_1	"\033[38;5;46myes\033[0m"
#define PRIVCOL_C_1	"\033[38;5;28myes\033[0m"
#define PRIVCOL_I_1	"\033[38;5;30myes\033[0m"

#define PRIVCOL_I_X	"\033[38;5;244mn/a\033[0m"

// This macro resolves to yes/no in either ircd or class colors to show from
// where the oper would get the privilege if he didn't have an explicit value set
#define PARENT_PRIV(LDEFAULT, GDEFAULT)	\
	(				\
		(priv_class == -1)	\
		? (			\
			((LDEFAULT) == -1 && (GDEFAULT) == -1)	\
			? PRIVCOL_I_X			\
			: (				\
				((local && LDEFAULT) ||		\
				 (!local && GDEFAULT))		\
				? (PRIVCOL_I_1) : (PRIVCOL_I_0)	\
			)				\
		  ) 					\
		: (					\
			(priv_class == 1)		\
			? (PRIVCOL_C_1)	: (PRIVCOL_C_0)	\
		  )					\
	)

// This macro displays the priv status and where it's set (oper, class, ircd)
#define SHOW_PRIV(NAME, LDEFAULT, GDEFAULT)	do { \
	priv_oper = atoi(pgsql_nvalue(res, 0, "oper_priv_" NAME));	\
	priv_class = atoi(pgsql_nvalue(res, 0, "class_priv_" NAME));	\
	if(priv_oper == -1 && priv_class == -1 && (LDEFAULT) == -1 && (GDEFAULT) == -1)	\
		out("  %-14s: %-3s", NAME, PRIVCOL_I_X);		\
	else if(priv_oper == -1 && priv_class == -1)			\
		out("  %-14s: %-3s", NAME,				\
			((local && (LDEFAULT)) || (!local && GDEFAULT))	\
			? PRIVCOL_I_1					\
			: PRIVCOL_I_0);					\
	else if(priv_oper == 1)						\
		out("  %-14s: %s (%s)", NAME, PRIVCOL_O_1, PARENT_PRIV(LDEFAULT, GDEFAULT));	\
	else if(priv_oper == 0)						\
		out("  %-14s: %s  (%s)", NAME, PRIVCOL_O_0, PARENT_PRIV(LDEFAULT, GDEFAULT));	\
	else if(priv_class == 1)					\
		out("  %-14s: " PRIVCOL_C_1, NAME);	\
	else if(priv_class == 0)					\
		out("  %-14s: " PRIVCOL_C_0, NAME);	\
	} while(0)

	out("Privileges (" PRIVCOL_I_1 "/" PRIVCOL_I_0 ": ircd default\033[0m, "\
			   PRIVCOL_C_1 "/" PRIVCOL_C_0 ": from connclass, "\
			   PRIVCOL_O_1 "/" PRIVCOL_O_0 ": explicitly set):");
	SHOW_PRIV("local",		-1, -1);
	SHOW_PRIV("umode_nochan",	1, 1);
	SHOW_PRIV("umode_noidle",	1, 1);
	SHOW_PRIV("umode_chserv",	0, 0);
	SHOW_PRIV("notargetlimit",	0, 1);
	SHOW_PRIV("flood",		0, 0);
	SHOW_PRIV("pseudoflood",	0, 1);
	SHOW_PRIV("gline_immune",	0, 0);
	SHOW_PRIV("die",		0, 1);
	SHOW_PRIV("restart",		0, 1);
#undef SHOW_PRIV
	pgsql_free(res);
	putc('\n', stdout);

	// Hosts
	res = pgsql_query("SELECT	mask\
			   FROM		operhosts\
			   WHERE	lower(oper) = lower($1)",
			  1, stringlist_build(argv[1], NULL));

	out("Hostmasks:");
	if(!(rows = pgsql_num_rows(res)))
		out("  (none)");

	for(int i = 0; i < rows; i++)
		out("  %s", pgsql_nvalue(res, i, "mask"));
	pgsql_free(res);
	putc('\n', stdout);

	// Servers
	res = pgsql_query("SELECT	server\
			   FROM		opers2servers\
			   WHERE	lower(oper) = lower($1)",
			  1, stringlist_build(argv[1], NULL));

	out("Servers:");
	if(!(rows = pgsql_num_rows(res)))
		out("  (none)");

	for(int i = 0; i < rows; i++)
		out("  %s", pgsql_nvalue(res, i, "server"));
	pgsql_free(res);
}

CMD_FUNC(oper_add)
{
	char *line, *tmp;
	char *name = NULL, *username = NULL, *class = NULL, *local = NULL;
	const char *pass;
	int cnt;

	// Prompt name
	while(1)
	{
		line = readline_noac("Name", NULL);
		if(!line || !*line)
			goto out;

		cnt = pgsql_query_int("SELECT COUNT(*) FROM opers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("An oper with this name already exists");
			continue;
		}

		name = strdup(line);
		break;
	}

	// Prompt username
	while(1)
	{
		line = readline_noac("/oper username", (!strchr(name, ' ') ? name : NULL));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		tmp = pgsql_query_str("SELECT name FROM opers WHERE lower(username) = lower($1)", stringlist_build(line, NULL));
		if(*tmp)
		{
			out_color(COLOR_YELLOW, "The oper `%s' has the same username", tmp);
			if(!readline_yesno("Use it anyway?", "No"))
				continue;
		}

		username = strdup(line);
		break;
	}

	// Prompt password
	if(!(pass = prompt_operpass(0)))
		goto out;

	// Prompt connclass
	while(1)
	{
		line = readline_connclass("Connclass", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		tmp = pgsql_query_str("SELECT name FROM connclasses_users WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A connclass with this name does not exist");
			continue;
		}

		class = strdup(tmp);
		break;
	}

	// Global/local
	tmp = pgsql_query_str("SELECT priv_local FROM connclasses_users WHERE name = $1", stringlist_build(class, NULL));
	int class_local = atoi(tmp);
	if(class_local == -1) // not set, we *must* set it for the oper
	{
		out_color(COLOR_YELLOW, "The chosen connection class does not specify if it is for local or global opers.");
		if(readline_yesno("Do you want the oper to be global?", "Yes"))
			local = "0";
		else
			local = "1";

	}
	else
	{
		char promptbuf[128];
		out("The default oper type in the chosen connection class is \033[1m%s\033[22m", class_local == 1 ? "local" : "global");
		local = "-1";

		snprintf(promptbuf, sizeof(promptbuf), "Do you want to override this and make the oper \033[1m%s\033[22m?", class_local == 1 ? "global" : "local");
		if(readline_yesno(promptbuf, "No"))
			local = (class_local == 1) ? "0" : "1";
	}

	pgsql_begin();
	pgsql_query("INSERT INTO opers\
			(name, username, password, connclass, priv_local)\
		     VALUES\
		     	($1, $2, $3, $4, $5)",
		    0,
		    stringlist_build(name, username, pass, class, local, NULL));

	// Prompt servers
	out("Use `*' to add the oper to all non-hub servers");
	while(1)
	{
		opermod_server_adding = 1;
		opermod_tc_oper = name;
		line = readline_custom("Server", NULL, opermod_server_generator);
		if(!line)
		{
			pgsql_rollback();
			goto out;
		}
		else if(!*line)
			break;

		if(!strcmp(line, "*"))
		{
			pgsql_query("INSERT INTO opers2servers (oper, server)\
					SELECT	$1::varchar, name\
					FROM 	servers\
					WHERE	type != 'HUB' AND\
						name NOT IN (\
							SELECT	server\
							FROM	opers2servers\
							WHERE	oper = $1\
						)",
				    0, stringlist_build(name, NULL));
			out("Added all non-hub servers");
			continue;
		}

		tmp = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(!*tmp)
		{
			error("A server named `%s' does not exist", line);
			continue;
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM opers2servers WHERE oper = $1 AND server = $2", stringlist_build(name, tmp, NULL));
		if(cnt)
		{
			out("This server is already added");
			continue;
		}

		pgsql_query("INSERT INTO opers2servers (oper, server) VALUES ($1, $2)", 0, stringlist_build(name, tmp, NULL));
		out("Added server `%s'", tmp);

		cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE name NOT IN (SELECT server FROM opers2servers WHERE oper = $1)", stringlist_build(name, NULL));
		if(!cnt)
			break;
	}

	// Prompt hostmasks
	while(1)
	{
		opermod_tc_oper = name;
		line = readline_custom("Hostmask", NULL, opermod_mask_generator);
		if(!line)
		{
			pgsql_rollback();
			goto out;
		}
		else if(!*line)
			break;

		int cnt = pgsql_query_int("SELECT COUNT(*) FROM operhosts WHERE oper = $1 AND mask = $2", stringlist_build(name, line, NULL));
		if(cnt)
		{
			out("`%s' is already added", line);
			continue;
		}

		pgsql_query("INSERT INTO operhosts (oper, mask) VALUES ($1, $2)", 0, stringlist_build(name, line, NULL));
		out("Added mask `%s'", line);
	}

	pgsql_commit();
	out_color(COLOR_LIME, "Oper `%s' added successfully", name);

out:
	xfree(name);
	xfree(username);
	xfree(class);
}

CMD_FUNC(oper_del)
{
	char promptbuf[128];

	if(argc < 2)
	{
		out("Usage: deloper <name>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM opers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("An oper named `%s' does not exist", argv[1]);
		return;
	}

	snprintf(promptbuf, sizeof(promptbuf), "Do you really want to delete the oper `%s'?", argv[1]);
	if(!readline_yesno(promptbuf, "No"))
		return;

	pgsql_query("DELETE FROM opers WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Oper `%s' successfully deleted.", argv[1]);
}

CMD_FUNC(oper_mod)
{
	const char *tmp;
	char name[64];
	int modified = 0;

	if(argc < 3)
	{
		out("Usage: modoper <name> <args...>");
		return;
	}

	tmp = pgsql_query_str("SELECT name FROM opers WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!*tmp)
	{
		error("An oper named `%s' does not exist", argv[1]);
		return;
	}

	strlcpy(name, tmp, sizeof(name));

	pgsql_begin();
	for(int i = 2; i < argc; i++)
	{
		if(!strcmp(argv[i], "--addmask"))
		{
			if(i == argc - 1 || match("*@*", argv[i + 1]))
			{
				error("--addmask needs an `ident@host' argument");
				pgsql_rollback();
				return;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM operhosts WHERE oper = $1 AND mask = $2",
						  stringlist_build(name, argv[i + 1], NULL));
			if(cnt)
			{
				out("`%s' is already added", argv[i + 1]);
				i++;
				continue;
			}

			out("Adding mask: %s", argv[i + 1]);
			pgsql_query("INSERT INTO operhosts (oper, mask) VALUES ($1, $2)", 0,
				    stringlist_build(name, argv[i + 1], NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--delmask"))
		{
			if(i == argc - 1 || match("*@*", argv[i + 1]))
			{
				error("--delmask needs an `ident@host' argument");
				pgsql_rollback();
				return;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM operhosts WHERE oper = $1 AND mask = $2",
						  stringlist_build(name, argv[i + 1], NULL));
			if(!cnt)
			{
				out("`%s' is not added; no need to delete it", argv[i + 1]);
				i++;
				continue;
			}

			out("Removing mask: %s", argv[i + 1]);
			pgsql_query("DELETE FROM operhosts WHERE oper = $1 AND mask = $2", 0,
				    stringlist_build(name, argv[i + 1], NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--addserver"))
		{
			const char *server;

			if(i == argc - 1)
			{
				error("--addserver needs a server argument");
				pgsql_rollback();
				return;
			}

			server = pgsql_query_str("SELECT name FROM servers WHERE lower(name) = lower($1)", stringlist_build(argv[i + 1], NULL));
			if(!*server)
			{
				error("There is no server named `%s'", argv[i + 1]);
				pgsql_rollback();
				return;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM opers2servers WHERE oper = $1 AND server = $2",
						  stringlist_build(name, server, NULL));
			if(cnt)
			{
				out("`%s' is already an oper on `%s'", name, server);
				i++;
				continue;
			}

			out("Adding server: %s", server);
			pgsql_query("INSERT INTO opers2servers (oper, server) VALUES ($1, $2)", 0,
				    stringlist_build(name, server, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--delserver"))
		{
			if(i == argc - 1)
			{
				error("--delserver needs a server argument");
				pgsql_rollback();
				return;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM opers2servers WHERE oper = $1 AND lower(server) = lower($2)",
						  stringlist_build(name, argv[i + 1], NULL));
			if(!cnt)
			{
				out("`%s' is not an oper on `%s'", name, argv[i + 1]);
				i++;
				continue;
			}

			out("Removing server: %s", argv[i + 1]);
			pgsql_query("DELETE FROM opers2servers WHERE oper = $1 AND lower(server) = lower($2)", 0,
				    stringlist_build(name, argv[i + 1], NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--suspend") || !strcmp(argv[i], "--unsuspend"))
		{
			int suspend = !strcmp(argv[i], "--suspend");
			int active = pgsql_query_bool("SELECT active FROM opers WHERE name = $1", stringlist_build(name, NULL));

			if(active && !suspend)
			{
				out("`%s' is not suspended", name);
				continue;
			}
			else if(!active && suspend)
			{
				out("`%s' is already suspended", name);
				continue;
			}

			if(suspend)
			{
				out("Suspending %s", name);
				pgsql_query("UPDATE opers SET active = false WHERE name = $1", 0, stringlist_build(name, NULL));
			}
			else
			{
				out("Unsuspending %s", name);
				pgsql_query("UPDATE opers SET active = true WHERE name = $1", 0, stringlist_build(name, NULL));
			}

			modified = 1;
		}
		else if(!strcmp(argv[i], "--password"))
		{
			const char *pass;

			tmp = pgsql_query_str("SELECT password FROM opers WHERE name = $1", stringlist_build(name, NULL));
			out("Old password: %s", tmp);

			if(!(pass = prompt_operpass(1)))
			{
				out("Not changing the password");
				continue;
			}

			out("Setting password: %s", pass);
			pgsql_query("UPDATE opers SET password = $1 WHERE name = $2", 0, stringlist_build(pass, name, NULL));
			modified = 1;
		}
		else if(!strcmp(argv[i], "--class"))
		{
			if(i == argc - 1)
			{
				error("--class needs a connclass argument");
				pgsql_rollback();
				return;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM connclasses_users WHERE name = $1",
						  stringlist_build(argv[i + 1], NULL));
			if(!cnt)
			{
				error("`%s' is not a valid connection class", argv[i + 1]);
				pgsql_rollback();
				return;
			}

			char class_local[3], oper_local[3];
			tmp = pgsql_query_str("SELECT priv_local FROM connclasses_users WHERE name = $1", stringlist_build(argv[i + 1], NULL));
			strlcpy(class_local, tmp, sizeof(class_local));
			tmp = pgsql_query_str("SELECT priv_local FROM opers WHERE name = $1", stringlist_build(name, NULL));
			strlcpy(oper_local, tmp, sizeof(oper_local));

			if(!strcmp(class_local, "-1") && !strcmp(oper_local, "-1"))
			{
				// Fetch localness of current class
				tmp = pgsql_query_str("SELECT	c.priv_local\
						       FROM	opers o\
						       JOIN	connclasses_users c ON (c.name = o.connclass)\
						       WHERE	o.name = $1",
						      stringlist_build(name, NULL));
				out_color(COLOR_YELLOW, "Assigning this class makes the oper explicitely %s", atoi(tmp) == 1 ? "local" : "global");
				pgsql_query("UPDATE opers SET priv_local = $1 WHERE name = $2", 0, stringlist_build(tmp, name, NULL));
			}

			out("Setting connclass: %s", argv[i + 1]);
			pgsql_query("UPDATE opers SET connclass = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--priv"))
		{
			const char *value;
			char query[128];

			if(i >= argc - 2)
			{
				error("--priv needs a priv and a value argument");
				pgsql_rollback();
				return;
			}

			tmp = NULL;
			for(int j = 0; oper_privs[j]; j++)
			{
				if(!strcmp(oper_privs[j], argv[i + 1]))
					tmp = oper_privs[j];
			}

			if(!tmp)
			{
				error("Invalid priv `%s'", argv[i + 1]);
				pgsql_rollback();
				return;
			}

			// Validate value
			if(!strcmp(argv[i + 2], "yes"))
				value = "1";
			else if(!strcmp(argv[i + 2], "no"))
				value = "0";
			else if(!strcmp(argv[i + 2], "inherit"))
			{
				value = "-1";
				if(!strcmp(argv[i + 1], "local"))
				{
					tmp = pgsql_query_str("SELECT	c.priv_local\
							       FROM	opers o\
							       JOIN	connclasses_users c ON (c.name = o.connclass)\
							       WHERE	o.name = $1",
						      stringlist_build(name, NULL));

					if(!strcmp(tmp, "-1"))
					{
						error("Ignoring local=inherit since the class has local=inherit and either class or oper must have an explicit local setting");
						i += 2;
						continue;
					}
				}
			}
			else
			{
				error("Invalid value `%s', expected yes/no/inherit", argv[i + 2]);
				pgsql_rollback();
				return;
			}

			out("Setting priv: %s -> %s", argv[i + 1], value);
			snprintf(query, sizeof(query), "UPDATE opers SET priv_%s = $1 WHERE name = $2", argv[i + 1]);
			pgsql_query(query, 0, stringlist_build(value, name, NULL));
			i += 2;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--username"))
		{
			if(i == argc - 1)
			{
				error("--username needs a username argument");
				pgsql_rollback();
				return;
			}

			if(strchr(argv[i + 1], ' '))
			{
				error("Username cannot contain spaces");
				pgsql_rollback();
				return;
			}

			out("Setting username: %s", argv[i + 1]);
			pgsql_query("UPDATE opers SET username = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
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
		out_color(COLOR_LIME, "Oper was updated successfully");
	else
		out_color(COLOR_LIME, "No changes were made");
}

static const char *prompt_operpass(int allow_none)
{
	static char passbuf[128];
	static char *passv[3];
	int passc;
	char *line;

	out("Note: You can enter either a plaintext or an encrypted (umkpasswd-style) password");
	while(1)
	{
		line = readline_noac("New password", NULL);
		if(!line || (!*line && allow_none)) // Bail out if we got EOF / accepted-empty
			return NULL;
		else if(*line) // We got something -> leave thel oop
			break;
	}

	strlcpy(passbuf, line, sizeof(passbuf) - 7); // always leave space for $PLAIN$ or $SMD5$
	passc = tokenize(line, passv, 3, '$', 1);

	if(passc == 3 && !*passv[0] && *passv[1] && *passv[2]) // Looks like umkpasswd-style
	{
		debug("Password is already encrypted (%s)", passv[1]);
		return passbuf;
	}

	int encrypt = readline_yesno("Do you want to encrypt the password (SMD5)?", "Yes");
	if(!encrypt)
	{
		memmove(passbuf + strlen("$PLAIN$"), passbuf, strlen(passbuf) + 1);
		memcpy(passbuf, "$PLAIN$", strlen("$PLAIN$"));
		return passbuf;
	}

	char *crypted = crypt_pass_smd5(passbuf);
	snprintf(passbuf, sizeof(passbuf), "$SMD5$%s", crypted);

	return passbuf;
}

// Tab completion stuff
CMD_TAB_FUNC(oper_del)
{
	if(CAN_COMPLETE_ARG(1))
		return oper_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(oper_mod)
{
	if(CAN_COMPLETE_ARG(1))
		return oper_generator(text, state);

	enum {
		ARG_NONE,
		ARG_MASK,
		ARG_PRIV,
		ARG_PRIV_VALUE,
		ARG_CLASS,
		ARG_ADDSERVER,
		ARG_DELSERVER
	} arg_type;

	for(int i = 2; i <= tc_argc; i++)
	{
		if(!CAN_COMPLETE_ARG(i))
			continue;

		arg_type = ARG_NONE;
		if(!strcmp(tc_argv[i - 1], "--addmask") ||
		   !strcmp(tc_argv[i - 1], "--delmask"))
			arg_type = ARG_MASK;
		else if(!strcmp(tc_argv[i - 1], "--priv"))
			arg_type = ARG_PRIV;
		else if(!strcmp(tc_argv[i - 2], "--priv"))
			arg_type = ARG_PRIV_VALUE;
		else if(!strcmp(tc_argv[i - 1], "--class"))
			arg_type = ARG_CLASS;
		else if(!strcmp(tc_argv[i - 1], "--addserver"))
			arg_type = ARG_ADDSERVER;
		else if(!strcmp(tc_argv[i - 1], "--delserver"))
			arg_type = ARG_DELSERVER;
		else if(!strcmp(tc_argv[i - 1], "--username"))
			return NULL;

		if(!arg_type)
			return opermod_arg_generator(text, state);

		// We autocomplete masks even in addmask for opers
		// with crappy ISPs where only one block changes
		// usually (like gonz ;x)
		if(arg_type == ARG_MASK)
		{
			opermod_tc_oper = tc_argv[1];
			return opermod_mask_generator(text, state);
		}
		else if(arg_type == ARG_PRIV)
			return opermod_priv_generator(text, state);
		else if(arg_type == ARG_PRIV_VALUE)
			return opermod_priv_value_generator(text, state);
		else if(arg_type == ARG_CLASS)
			return connclass_generator(text, state);
		else if(arg_type == ARG_ADDSERVER || arg_type == ARG_DELSERVER)
		{
			opermod_tc_oper = tc_argv[1];
			opermod_server_adding = (arg_type == ARG_ADDSERVER);
			return opermod_server_generator(text, state);
		}
	}

	return NULL;
}

CMD_TAB_FUNC(oper_info)
{
	if(CAN_COMPLETE_ARG(1))
		return oper_generator(text, state);
	return NULL;
}

static char *opermod_arg_generator(const char *text, int state)
{
	static const char *values[] = {
		"--password",
		"--addmask",
		"--delmask",
		"--suspend",
		"--unsuspend",
		"--priv",
		"--class",
		"--addserver",
		"--delserver",
		"--username",
		NULL
	};
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

static char *opermod_mask_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		assert(opermod_tc_oper);
		textlen = strlen(text);
		row = 0;
		res = pgsql_query("SELECT mask FROM operhosts WHERE lower(oper) = lower($1)", 1, stringlist_build(opermod_tc_oper, NULL));
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

static char *opermod_priv_generator(const char *text, int state)
{
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
	while((val = oper_privs[idx]))
	{
		idx++;
		if(!strncasecmp(val, text, len))
			return strdup(val);
	}

  	return NULL;
}

static char *opermod_priv_value_generator(const char *text, int state)
{
	static const char *values[] = { "yes", "no", "inherit", NULL };
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

static char *opermod_server_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		if(opermod_server_adding)
			res = pgsql_query("SELECT name FROM servers WHERE name ILIKE $1||'%' AND name NOT IN (SELECT server FROM opers2servers WHERE oper = $2)", 1, stringlist_build(text, opermod_tc_oper, NULL));
		else
			res = pgsql_query("SELECT server FROM opers2servers WHERE oper = $1 AND server ILIKE $2||'%'", 1, stringlist_build(opermod_tc_oper, text, NULL));
		rows = pgsql_num_rows(res);
	}
	else if(state == -1) // Cleanup
	{
		pgsql_free(res);
		return NULL;
	}

	// Return the next name which partially matches from the command list.
	while(row < rows)
	{
		name = pgsql_value(res, row, 0);
		row++;
		if(!strncasecmp(name, text, len))
			return strdup(name);
	}

  	return NULL;
}

static char *oper_generator(const char *text, int state)
{
	static size_t textlen;
	static PGresult *res;
	static int row, rows;

	if(state == 0)
	{
		textlen = strlen(text);
		row = 0;
		res = pgsql_query("SELECT name FROM opers WHERE name ILIKE ($1 || '%')", 1, stringlist_build(text, NULL));
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
