#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"

static char *webirc_generator(const char *text, int state);
CMD_FUNC(webirc_list);
CMD_FUNC(webirc_add);
CMD_FUNC(webirc_del);
CMD_TAB_FUNC(webirc_del);
CMD_FUNC(webirc_edit);
CMD_TAB_FUNC(webirc_edit);

static struct command commands[] = {
	CMD_STUB("webirc", "WebIRC Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "webirc" subcommands
	CMD("list", webirc_list, "Show webirc authorizations"),
	CMD("add", webirc_add, "Add a webirc authorization"),
	CMD_TC("del", webirc_del, "Remove a webirc authorization"),
	CMD_TC("edit", webirc_edit, "Edit a webirc authorization"),
	CMD_LIST_END
};


void cmd_webirc_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "webirc");
	cmd_alias("webircs", "webirc", "list");
	cmd_alias("addwebirc", "webirc", "add");
	cmd_alias("delwebirc", "webirc", "del");
	cmd_alias("editwebirc", "webirc", "edit");
}

CMD_FUNC(webirc_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT * FROM webirc ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(6, rows);
	table_set_header(table, "Name", "IP", "Password", "Ident", "HMAC", "Description");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char*)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char*)pgsql_nvalue(res, i, "ip"));
		table_col_str(table, i, 2, (char*)pgsql_nvalue(res, i, "password"));
		table_col_str(table, i, 3, (char*)pgsql_nvalue(res, i, "ident"));
		table_col_str(table, i, 4, !strcasecmp(pgsql_nvalue(res, i, "hmac"), "t") ? (char*)pgsql_nvalue(res, i, "hmac_time") : "No");
		table_col_str(table, i, 5, (char*)pgsql_nvalue(res, i, "description"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(webirc_add)
{
	const char *line;
	char *name, *ip, *password, *ident, *hmac, *description;
	name = ip = password = ident = hmac = description = NULL;

	// Prompt webirc name
	while(1)
	{
		line = readline_noac("Name", NULL);
		if(!line || !*line)
			return;

		int cnt = pgsql_query_int("SELECT COUNT(*) FROM webirc WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("A webirc authorization with this name already exists");
			continue;
		}

		name = strdup(line);
		break;
	}

	// Prompt description
	while(1)
	{
		line = readline_noac("Description", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		description = strdup(line);
		break;
	}

	// Prompt ip
	while(1)
	{
		line = readline_noac("IP", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		if(strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
		{
			error("This IP doesn't look like a valid IP");
			continue;
		}

		ip = strdup(line);
		break;
	}

	// Prompt password
	while(1)
	{
		line = readline_noac("Password", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		password = strdup(line);
		break;
	}

	// Prompt ident
	while(1)
	{
		line = readline_noac("Ident", "");
		if(!line)
			goto out;
		else if(!*line)
		{
			ident = NULL;
			break;
		}

		if(strlen(line) > 10)
		{
			error("Ident length cannot exceed 10 characters");
			continue;
		}

		ident = strdup(line);
		break;
	}

	// Prompt HMAC
	hmac = NULL;
	if(readline_yesno("Use HMAC-MD5 authentication via extended USER (qwebirc-style)?", NULL))
	{
		while(1)
		{
			line = readline_noac("Time divisor", "30");
			if(!line)
				goto out;
			else if(!*line)
				continue;

			if(atoi(line) < 10)
			{
				error("Divisor must be >=10");
				continue;
			}

			hmac = strdup(line);
			break;
		}
	}

	pgsql_query("INSERT INTO webirc\
			(name, ip, password, ident, hmac,\
			 hmac_time, description)\
		     VALUES\
		     	($1, $2, $3, $4, $5,\
			 $6, $7)",
		    0,
		    stringlist_build_n(7,
			    name, ip, password, ident, (hmac ? "t" : "f"),
			    (hmac ? hmac : "0"), description));
	out("Webirc authorization `%s' added successfully", name);

	if(readline_yesno("Add this webirc authorization to all leaf servers?", "Yes"))
	{
		pgsql_query("INSERT INTO webirc2servers (webirc, server) SELECT $1, name FROM servers WHERE type = 'LEAF'", 0, stringlist_build(name, NULL));
		out("Webirc authorization added to servers successfully");
	}

out:
	xfree(name);
	xfree(ip);
	xfree(password);
	xfree(ident);
	xfree(hmac);
	xfree(description);
}

CMD_FUNC(webirc_del)
{
	if(argc < 2)
	{
		out("Usage: delwebirc <name>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM webirc WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("There is no webirc authorization named `%s'", argv[1]);
		return;
	}

	pgsql_query("DELETE FROM webirc WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Webirc authorization `%s' deleted successfully", argv[1]);
}

CMD_FUNC(webirc_edit)
{
	const char *line, *tmp;
	char *name, *ip, *password, *ident, *hmac, *description;
	name = ip = password = ident = hmac = description = NULL;
	PGresult *res;

	if(argc < 2)
	{
		out("Usage: editwebirc <name>");
		return;
	}

	res = pgsql_query("SELECT * FROM webirc WHERE lower(name) = lower($1)", 1, stringlist_build(argv[1], NULL));
	if(!pgsql_num_rows(res))
	{
		error("There is no webirc authorization named `%s'", argv[1]);
		goto out;
	}

	name = strdup(pgsql_nvalue(res, 0, "name"));
	out("Name: %s", name);

	// Prompt description
	while(1)
	{
		line = readline_noac("Description", pgsql_nvalue(res, 0, "description"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		description = strdup(line);
		break;
	}

	// Prompt ip
	while(1)
	{
		line = readline_noac("IP", pgsql_nvalue(res, 0, "ip"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		if(strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
		{
			error("This IP doesn't look like a valid IP");
			continue;
		}

		ip = strdup(line);
		break;
	}

	// Prompt password
	while(1)
	{
		line = readline_noac("Password", pgsql_nvalue(res, 0, "password"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		password = strdup(line);
		break;
	}

	// Prompt ident
	while(1)
	{
		tmp = pgsql_nvalue(res, 0, "ident");
		line = readline_noac("Ident", tmp ? tmp : "");
		if(!line)
			goto out;
		else if(!*line)
		{
			ident = NULL;
			break;
		}

		if(strlen(line) > 10)
		{
			error("Ident length cannot exceed 10 characters");
			continue;
		}

		ident = strdup(line);
		break;
	}

	// Prompt HMAC
	hmac = NULL;
	if(readline_yesno("Use HMAC-MD5 authentication via extended USER (qwebirc-style)?", (!strcasecmp(pgsql_nvalue(res, 0, "hmac"), "t") ? "Yes" : "No")))
	{
		while(1)
		{
			tmp = pgsql_nvalue(res, 0, "hmac_time");
			line = readline_noac("Time divisor", atoi(tmp) > 0 ? tmp : "30");
			if(!line)
				goto out;
			else if(!*line)
				continue;

			if(atoi(line) < 10)
			{
				error("Divisor must be >=10");
				continue;
			}

			hmac = strdup(line);
			break;
		}
	}

	pgsql_query("UPDATE	webirc\
		     SET	ip = $1,\
				password = $2,\
				ident = $3,\
				hmac = $4,\
				hmac_time = $5,\
				description = $6\
		     WHERE	name = $7",
		    0,
		    stringlist_build_n(7, ip, password, ident, (hmac ? "t" : "f"),
				     (hmac ? hmac : "0"), description, name));
	out("Webirc authorization `%s' updated successfully", name);

out:
	pgsql_free(res);
	xfree(name);
	xfree(ip);
	xfree(password);
	xfree(ident);
	xfree(hmac);
	xfree(description);
}

// Tab completion stuff
CMD_TAB_FUNC(webirc_del)
{
	if(CAN_COMPLETE_ARG(1))
		return webirc_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(webirc_edit)
{
	if(CAN_COMPLETE_ARG(1))
		return webirc_generator(text, state);
	return NULL;
}

static char *webirc_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM webirc WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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


