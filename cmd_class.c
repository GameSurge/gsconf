#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "input.h"
#include "table.h"
#include <search.h>

static char *classmod_arg_generator(const char *text, int state);
static char *classmod_priv_generator(const char *text, int state);
static char *classmod_priv_value_generator(const char *text, int state);
CMD_FUNC(class_list);
CMD_FUNC(class_info);
CMD_TAB_FUNC(class_info);
CMD_FUNC(class_add);
CMD_FUNC(class_mod);
CMD_TAB_FUNC(class_mod);
CMD_FUNC(class_del);
CMD_TAB_FUNC(class_del);

static const char *class_privs[] = { "local", "umode_nochan", "umode_noidle", "umode_chserv", "notargetlimit", "flood", "pseudoflood", "gline_immune", "die", "restart", "chan_limit", NULL };

static struct command commands[] = {
	CMD_STUB("class", "User Connclass Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	CMD("list", class_list, "Show a connclass list"),
	CMD_TC("info", class_info, "Show information about a connclass"),
	CMD("add", class_add, "Add a class"),
	CMD_TC("del", class_del, "Remove a connclass"),
	CMD_TC("mod", class_mod, "Modify a connclass"),
	CMD_TC("edit", class_mod, "Modify a connclass"),
	CMD_LIST_END
};



void cmd_class_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "class");
	cmd_alias("classes", "class", "list");
	cmd_alias("classinfo", "class", "info");
	cmd_alias("addclass", "class", "add");
	cmd_alias("delclass", "class", "del");
	cmd_alias("modclass", "class", "mod");
	cmd_alias("classmod", "class", "mod");
}

CMD_FUNC(class_list)
{
	struct table *table;
	PGresult *res;
	int rows;

	res = pgsql_query("SELECT	name,\
					maxlinks,\
					sendq,\
					recvq,\
					usermode,\
					fakehost,\
					priv_local\
			   FROM		connclasses_users\
			   ORDER BY	name ASC",
			  1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(7, rows);
	table_set_header(table, "Name", "MaxLinks", "SendQ", "RecvQ", "Opers", "UModes", "Fakehost");
	table_ralign_column(table, 2, 1);
	table_ralign_column(table, 3, 1);

	for(int i = 0; i < rows; i++)
	{
		int local = atoi(pgsql_nvalue(res, i, "priv_local"));
		const char *oper_type = "";
		if(local == 0)
			oper_type = "Global";
		else if(local == 1)
			oper_type = "Local";

		table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "maxlinks"));
		table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "sendq"));
		table_col_str(table, i, 3, (char *)pgsql_nvalue(res, i, "recvq"));
		table_col_str(table, i, 4, (char *)oper_type);
		table_col_str(table, i, 5, (char *)pgsql_nvalue(res, i, "usermode"));
		table_col_str(table, i, 6, (char *)pgsql_nvalue(res, i, "fakehost"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(class_info)
{
	PGresult *res;
	int priv_class, local;
	int rows;
	const char *tmp, *typestr;

	if(argc < 2)
	{
		out("Usage: classinfo <name>");
		return;
	}

	res = pgsql_query("SELECT	name,\
					pingfreq,\
					maxlinks,\
					sendq,\
					recvq,\
					usermode,\
					fakehost,\
					priv_local,\
					priv_umode_nochan,\
					priv_umode_noidle,\
					priv_umode_chserv,\
					priv_notargetlimit,\
					priv_flood,\
					priv_pseudoflood,\
					priv_gline_immune,\
					priv_chan_limit,\
					priv_die,\
					priv_restart\
			   FROM		connclasses_users\
			   WHERE	lower(name) = lower($1)",
			1, stringlist_build(argv[1], NULL));

	if(!pgsql_num_rows(res))
	{
		error("A connclass named `%s' does not exist", argv[1]);
		pgsql_free(res);
		return;
	}

	priv_class = atoi(pgsql_nvalue(res, 0, "priv_local"));
	if(priv_class == -1)
	{
		local = -1; // assume it's not an oper class
		typestr = "\033[" COLOR_DARKGRAY "mInvalid\033[0m";
	}
	else if(priv_class == 1)
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
	out("PingFreq:  %s", pgsql_nvalue(res, 0, "pingfreq"));
	tmp = pgsql_nvalue(res, 0, "maxlinks");
	out("MaxLinks:  %s", atoi(tmp) ? tmp : "Unlimited");
	out("SendQ:     %s", pgsql_nvalue(res, 0, "sendq"));
	out("RecvQ:     %s", pgsql_nvalue(res, 0, "recvq"));
	if((tmp = pgsql_nvalue(res, 0, "usermode")))
		out("Usermodes: +%s", tmp);
	else
		out("Usermodes: \033[" COLOR_DARKGRAY "m(none)\033[0m");
	tmp = pgsql_nvalue(res, 0, "fakehost");
	out("Fakehost:  %s", tmp ? tmp : "\033[" COLOR_DARKGRAY "m(none)\033[0m");
	out("Opers:     %s", typestr);
	putc('\n', stdout);


#define PRIVCOL_C_0	"\033[38;5;196mno\033[0m"
#define PRIVCOL_I_0	"\033[38;5;89mno\033[0m"

#define PRIVCOL_C_1	"\033[38;5;46myes\033[0m"
#define PRIVCOL_I_1	"\033[38;5;30myes\033[0m"

#define PRIVCOL_I_X	"\033[38;5;244mn/a\033[0m"

// This macro displays the priv status and where it's set (oper, class, ircd)
#define SHOW_PRIV(NAME, LDEFAULT, GDEFAULT)	do { \
	priv_class = atoi(pgsql_nvalue(res, 0, "priv_" NAME));	\
	if(priv_class == -1 && (LDEFAULT) == -1 && (GDEFAULT) == -1)	\
		out("  %-14s: %-3s", NAME, PRIVCOL_I_X);		\
	else if(priv_class == -1 && local == -1)			\
		out("  %-14s: %-3s", NAME, PRIVCOL_I_0);		\
	else if(priv_class == -1)					\
		out("  %-14s: %-3s", NAME,				\
			((local && (LDEFAULT)) || (!local && GDEFAULT))	\
			? PRIVCOL_I_1					\
			: PRIVCOL_I_0);					\
	else if(priv_class == 1)					\
		out("  %-14s: " PRIVCOL_C_1, NAME);	\
	else if(priv_class == 0)					\
		out("  %-14s: " PRIVCOL_C_0, NAME);	\
	} while(0)

	out("Privileges (" PRIVCOL_I_1 "/" PRIVCOL_I_0 ": ircd default\033[0m, "\
			   PRIVCOL_C_1 "/" PRIVCOL_C_0 ": explicitly set):");
	SHOW_PRIV("local",		-1, -1);
	SHOW_PRIV("umode_nochan",	1, 1);
	SHOW_PRIV("umode_noidle",	1, 1);
	SHOW_PRIV("umode_chserv",	0, 0);
	SHOW_PRIV("notargetlimit",	0, 1);
	SHOW_PRIV("flood",		0, 0);
	SHOW_PRIV("pseudoflood",	0, 1);
	SHOW_PRIV("gline_immune",	0, 0);
	SHOW_PRIV("chan_limit",		1, 0);
	SHOW_PRIV("die",		0, 1);
	SHOW_PRIV("restart",		0, 1);
#undef SHOW_PRIV
	pgsql_free(res);
	putc('\n', stdout);

	// Opers
	res = pgsql_query("SELECT name FROM opers WHERE lower(connclass) = lower($1)",
			  1, stringlist_build(argv[1], NULL));

	out("Opers:");
	if(!(rows = pgsql_num_rows(res)))
		out("  (none)");

	for(int i = 0; i < rows; i++)
		out("  %s", pgsql_nvalue(res, i, "name"));
	pgsql_free(res);
	putc('\n', stdout);

	// Clients
	res = pgsql_query("SELECT name, server FROM clients WHERE lower(connclass) = lower($1)",
			  1, stringlist_build(argv[1], NULL));

	out("Clients:");
	if(!(rows = pgsql_num_rows(res)))
		out("  (none)");

	for(int i = 0; i < rows; i++)
		out("  %s (%s)", pgsql_nvalue(res, i, "name"), pgsql_nvalue(res, i, "server"));
	pgsql_free(res);
}

CMD_FUNC(class_add)
{
	char *line, *tmp;
	char *name = NULL, *pingfreq = NULL, *maxlinks = NULL, *sendq = NULL, *recvq = NULL, *usermode = NULL, *fakehost = NULL;
	const char *local;
	int cnt;

	// Prompt name
	while(1)
	{
		line = readline_noac("Name", NULL);
		if(!line || !*line)
			goto out;

		if(strstr(line, "::"))
		{
			error("Connclass name may not contain `::'");
			continue;
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM connclasses_users WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("A connclass with this name already exists");
			continue;
		}

		cnt = pgsql_query_int("SELECT COUNT(*) FROM connclasses_servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("A server connclass with this name already exists");
			continue;
		}

		name = strdup(line);
		break;
	}

	// Prompt pingfreq
	while(1)
	{
		line = readline_noac("Ping frequency", "1 minutes 30 seconds");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		// Should probably be validated, but we'd have to parse the string.
		// Let's just assume that people using this tool are smart enough not
		// to use values breaking stuff.

		pingfreq = strdup(line);
		break;
	}

	// Prompt maxlinks
	while(1)
	{
		line = readline_noac("Maxlinks (0 for unlimited)", "0");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		long val = strtol(line, &tmp, 10);
		if(*tmp || val < 0)
		{
			error("Maxlinks must be an integer >=0");
			continue;
		}

		maxlinks = strdup(line);
		break;
	}

	// Prompt sendQ
	while(1)
	{
		line = readline_noac("Max SendQ", "655360");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		unsigned long val = strtoul(line, &tmp, 10);
		if(*tmp || val < 10240)
		{
			error("SendQ must be an integer >=10240");
			continue;
		}

		sendq = strdup(line);
		break;
	}

	// Prompt recvQ
	while(1)
	{
		line = readline_noac("Max RecvQ", "1024");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		unsigned long val = strtoul(line, &tmp, 10);
		if(*tmp || val < 1024)
		{
			error("RecvQ must be an integer >=1024");
			continue;
		}

		recvq = strdup(line);
		break;
	}

	// Prompt default umodes
	while(1)
	{
		line = readline_noac("Default usermodes", "iw");
		if(!line)
			goto out;
		else if(!*line)
			continue;

		if(*line == '+')
			line++;

		if(*(tmp = line + strspn(line, "iwxd")) != '\0')
		{
			error("Invalid default usermode: +%c", *tmp);
			continue;
		}

		usermode = strdup(line);
		break;
	}

	// Prompt default fakehost
	while(1)
	{
		line = readline_noac("Default fakehost", "");
		if(!line)
			goto out;

		if(*line)
			fakehost = strdup(line);
		break;
	}

	// Global/local
	while(1)
	{
		line = readline_noac("Oper type (None, Local, Global)", "None");
		if(!strcasecmp(line, "None"))
		{
			local = "-1";
			break;
		}
		else if(!strcasecmp(line, "Local") || !strcasecmp(line, "L"))
		{
			local = "1";
			break;
		}
		else if(!strcasecmp(line, "Global") || !strcasecmp(line, "G"))
		{
			local = "0";
			break;
		}
	}

	pgsql_query("INSERT INTO connclasses_users\
			(name, pingfreq, maxlinks, sendq, recvq,\
			 usermode, fakehost, priv_local)\
		     VALUES\
		     	($1, $2, $3, $4, $5,\
			 $6, $7, $8)",
		    0,
		    stringlist_build_n(8, name, pingfreq, maxlinks, sendq, recvq,
				       usermode, fakehost, local));

	out_color(COLOR_LIME, "Connclass `%s' added successfully", name);

out:
	xfree(name);
	xfree(pingfreq);
	xfree(maxlinks);
	xfree(sendq);
	xfree(recvq);
	xfree(usermode);
	xfree(fakehost);
}

CMD_FUNC(class_del)
{
	PGresult *res;
	int rows;
	int in_use = 0;

	if(argc < 2)
	{
		out("Usage: delclass <name>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM connclasses_users WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("An connclass named `%s' does not exist", argv[1]);
		return;
	}

	// Check for opers
	res = pgsql_query("SELECT name FROM opers WHERE lower(connclass) = lower($1)", 1, stringlist_build(argv[1], NULL));
	rows = pgsql_num_rows(res);
	if(rows)
	{
		in_use = 1;
		out("This class is used by the following opers:");
		for(int row = 0; row < rows; row++)
			out("  %s", pgsql_nvalue(res, row, "name"));

	}
	pgsql_free(res);

	// Check for clients
	res = pgsql_query("SELECT name, server FROM clients WHERE lower(connclass) = lower($1)", 1, stringlist_build(argv[1], NULL));
	rows = pgsql_num_rows(res);
	if(rows)
	{
		in_use = 1;
		out("This class is used by the following clients:");
		for(int row = 0; row < rows; row++)
			out("  %s (%s)", pgsql_nvalue(res, row, "name"), pgsql_nvalue(res, row, "server"));

	}
	pgsql_free(res);

	if(in_use)
	{
		error("Cannot delete a connclass which is still in use");
		return;
	}

	pgsql_query("DELETE FROM connclasses_users WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Connclass `%s' deleted successfully", argv[1]);
}

CMD_FUNC(class_mod)
{
	char *tmp;
	char name[32];
	int modified = 0;

	if(argc < 3)
	{
		out("Usage: modclass <name> <args...>");
		return;
	}

	tmp = pgsql_query_str("SELECT name FROM connclasses_users WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!*tmp)
	{
		error("A connclass named `%s' does not exist", argv[1]);
		return;
	}

	strlcpy(name, tmp, sizeof(name));

	pgsql_begin();
	for(int i = 2; i < argc; i++)
	{
		if(!strcmp(argv[i], "--priv"))
		{
			const char *value;
			char query[128];
			const char *priv;

			if(i >= argc - 2)
			{
				error("--priv needs a priv and a value argument");
				pgsql_rollback();
				return;
			}

			priv = NULL;
			for(int j = 0; class_privs[j]; j++)
			{
				if(!strcmp(class_privs[j], argv[i + 1]))
					priv = class_privs[j];
			}

			if(!priv)
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
					tmp = pgsql_query_str("SELECT	name\
							       FROM	opers\
							       WHERE	connclass = $1 AND\
									priv_local = -1",
						      stringlist_build(name, NULL));

					if(*tmp)
					{
						error("Ignoring local=inherit since the class has at least one oper (`%s') with local=inherit", tmp);
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
			snprintf(query, sizeof(query), "UPDATE connclasses_users SET priv_%s = $1 WHERE name = $2", argv[i + 1]);
			pgsql_query(query, 0, stringlist_build(value, name, NULL));
			i += 2;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--fakehost"))
		{
			char *fakehost;

			if(i == argc - 1)
			{
				error("--fakehost needs an argument");
				pgsql_rollback();
				return;
			}

			fakehost = argv[i + 1];
			if(!strcmp(fakehost, "*"))
				fakehost = NULL;
			else if(strchr(argv[i + 1], ' '))
			{
				error("Fakehost cannot contain spaces");
				pgsql_rollback();
				return;
			}

			out("Setting fakehost: %s", fakehost ? fakehost : "(none)");
			pgsql_query("UPDATE connclasses_users SET fakehost = $1 WHERE name = $2", 0, stringlist_build_n(2, fakehost, name));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--umode"))
		{
			char *umode;

			if(i == argc - 1)
			{
				error("--umode needs an argument");
				pgsql_rollback();
				return;
			}

			if(!strcmp(argv[i + 1], "*"))
				umode = NULL;
			else
			{
				umode = argv[i + 1];
				if(*umode == '+')
					umode++;

				if(*(tmp = umode + strspn(umode, "iwxd")) != '\0')
				{
					error("Invalid default usermode: +%c", *tmp);
					pgsql_rollback();
					return;
				}
			}

			if(umode)
				out("Setting default umode: +%s", umode);
			else
				out("Setting default umode: (none)");

			pgsql_query("UPDATE connclasses_users SET usermode = $1 WHERE name = $2", 0, stringlist_build_n(2, umode, name));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--pingfreq"))
		{
			if(i == argc - 1)
			{
				error("--pingfreq needs an ircu-style duration argument");
				pgsql_rollback();
				return;
			}

			for(char *tmp = argv[i + 1]; *tmp; tmp++)
			{
				if(!isalnum(*tmp) && *tmp != ' ')
				{
					error("Unexpected character in pingfreq: '%c'", *tmp);
					pgsql_rollback();
					return;
				}
			}

			out("Setting pingfreq: %s", argv[i + 1]);
			pgsql_query("UPDATE connclasses_users SET pingfreq = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
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

			long val = strtol(argv[i + 1], &tmp, 10);
			if(*tmp || val < 0)
			{
				error("Maxlinks must be an integer >=0");
				pgsql_rollback();
				return;
			}

			out("Setting maxlinks: %s", argv[i + 1]);
			pgsql_query("UPDATE connclasses_users SET maxlinks = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--sendq"))
		{
			if(i == argc - 1)
			{
				error("--sendq needs an argument");
				pgsql_rollback();
				return;
			}

			unsigned long val = strtoul(argv[i + 1], &tmp, 10);
			if(*tmp || val < 10240)
			{
				error("SendQ must be an integer >=10240");
				pgsql_rollback();
				return;
			}

			out("Setting SendQ: %s", argv[i + 1]);
			pgsql_query("UPDATE connclasses_users SET sendq = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
			i++;
			modified = 1;
		}
		else if(!strcmp(argv[i], "--recvq"))
		{
			if(i == argc - 1)
			{
				error("--recvq needs an argument");
				pgsql_rollback();
				return;
			}

			unsigned long val = strtoul(argv[i + 1], &tmp, 10);
			if(*tmp || val < 1024)
			{
				error("RecvQ must be an integer >=1024");
				pgsql_rollback();
				return;
			}

			out("Setting RecvQ: %s", argv[i + 1]);
			pgsql_query("UPDATE connclasses_users SET recvq = $1 WHERE name = $2", 0, stringlist_build(argv[i + 1], name, NULL));
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
		out_color(COLOR_LIME, "Connclass was updated successfully");
	else
		out_color(COLOR_LIME, "No changes were made");
}

// Tab completion stuff
CMD_TAB_FUNC(class_del)
{
	if(CAN_COMPLETE_ARG(1))
		return connclass_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(class_mod)
{
	if(CAN_COMPLETE_ARG(1))
		return connclass_generator(text, state);

	enum {
		ARG_NONE,
		ARG_PRIV,
		ARG_PRIV_VALUE
	} arg_type;

	for(int i = 2; i <= tc_argc; i++)
	{
		if(!CAN_COMPLETE_ARG(i))
			continue;

		arg_type = ARG_NONE;
		if(!strcmp(tc_argv[i - 1], "--priv"))
			arg_type = ARG_PRIV;
		else if(!strcmp(tc_argv[i - 2], "--priv"))
			arg_type = ARG_PRIV_VALUE;
		else if(!strcmp(tc_argv[i - 1], "--umode"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--pingfreq"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--maxlinks"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--sendq"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--recvq"))
			return NULL;
		else if(!strcmp(tc_argv[i - 1], "--fakehost"))
			return NULL;

		if(!arg_type)
			return classmod_arg_generator(text, state);

		if(arg_type == ARG_PRIV)
			return classmod_priv_generator(text, state);
		else if(arg_type == ARG_PRIV_VALUE)
			return classmod_priv_value_generator(text, state);
	}

	return NULL;
}

CMD_TAB_FUNC(class_info)
{
	if(CAN_COMPLETE_ARG(1))
		return connclass_generator(text, state);
	return NULL;
}

static char *classmod_arg_generator(const char *text, int state)
{
	static int idx;
	static size_t len;
	const char *val;
	static const char *values[] = {
		"--pingfreq",
		"--maxlinks",
		"--sendq",
		"--recvq",
		"--umode",
		"--priv",
		"--fakehost",
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

static char *classmod_priv_generator(const char *text, int state)
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
	while((val = class_privs[idx]))
	{
		idx++;
		if(!strncasecmp(val, text, len))
			return strdup(val);
	}

  	return NULL;
}

static char *classmod_priv_value_generator(const char *text, int state)
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

