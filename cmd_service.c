#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"

static char *service_generator(const char *text, int state);
CMD_FUNC(service_list);
CMD_FUNC(service_add);
CMD_FUNC(service_del);
CMD_TAB_FUNC(service_del);
CMD_FUNC(service_edit);
CMD_TAB_FUNC(service_edit);

static struct command commands[] = {
	CMD_STUB("service", "Service Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "service" subcommands
	CMD("list", service_list, "Show services"),
	CMD("add", service_add, "Add a service"),
	CMD_TC("del", service_del, "Remove a service"),
	CMD_TC("edit", service_edit, "Edit a service"),
	CMD_LIST_END
};



void cmd_service_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "service");
	cmd_alias("services", "service", "list");
	cmd_alias("addservice", "service", "add");
	cmd_alias("delservice", "service", "del");
	cmd_alias("editservice", "service", "edit");
}

CMD_FUNC(service_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT * FROM services ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(5, rows);
	table_set_header(table, "Name", "IP", "Link Pass", "Hub", "UWorld");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "ip"));
		table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "link_pass"));
		table_col_str(table, i, 3, (!strcasecmp(pgsql_nvalue(res, i, "flag_hub"), "t") ? "Yes" : "No"));
		table_col_str(table, i, 4, (!strcasecmp(pgsql_nvalue(res, i, "flag_uworld"), "t") ? "Yes" : "No"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(service_add)
{
	const char *line;
	char *name, *ip, *link_pass;
	int flag_hub, flag_uworld;
	name = ip = link_pass = NULL;

	// Prompt service name
	while(1)
	{
		line = readline_noac("Service server name", NULL);
		if(!line || !*line)
			return;

		if(!strchr(line, '.') || line[0] == '.' || line[strlen(line) - 1] == '.')
		{
			error("Service name must contain a `.' which is not at the beginning/end");
			continue;
		}

		int cnt = pgsql_query_int("SELECT COUNT(*) FROM services WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("A service with this name already exists");
			continue;
		}

		name = strdup(line);
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

	// Prompt link password
	while(1)
	{
		line = readline_noac("Link password", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		link_pass = strdup(line);
		break;
	}

	flag_hub = readline_yesno("Allow service to introduce servers (hub)?", "Yes");
	flag_uworld = readline_yesno("Give service UWorld privileges?", "Yes");

	pgsql_query("INSERT INTO services\
			(name, ip, link_pass, flag_hub, flag_uworld)\
		     VALUES\
		     	($1, $2, $3, $4, $5)",
		    0,
		    stringlist_build(name, ip, link_pass, (flag_hub ? "t" : "f"),
				     (flag_uworld ? "t" : "f"), NULL));
	out("Service `%s' added successfully", name);

out:
	xfree(name);
	xfree(ip);
	xfree(link_pass);
}

CMD_FUNC(service_edit)
{
	const char *line;
	char *name, *ip, *link_pass;
	int flag_hub, flag_uworld;
	name = ip = link_pass = NULL;
	PGresult *res;

	if(argc < 2)
	{
		out("Usage: editservice <name>");
		return;
	}

	res = pgsql_query("SELECT * FROM services WHERE lower(name) = lower($1)", 1, stringlist_build(argv[1], NULL));
	if(!pgsql_num_rows(res))
	{
		error("A service named `%s' does not exist", argv[1]);
		goto out;
	}

	name = strdup(pgsql_nvalue(res, 0, "name"));
	out("Service server name: %s", name);

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

	// Prompt link password
	while(1)
	{
		line = readline_noac("Link password", pgsql_nvalue(res, 0, "link_pass"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		link_pass = strdup(line);
		break;
	}

	flag_hub = readline_yesno("Allow service to introduce servers (hub)?", (!strcasecmp(pgsql_nvalue(res, 0, "flag_hub"), "t") ? "Yes" : "No"));
	flag_uworld = readline_yesno("Give service UWorld privileges?", (!strcasecmp(pgsql_nvalue(res, 0, "flag_uworld"), "t") ? "Yes" : "No"));

	pgsql_query("UPDATE	services\
		     SET	ip = $1,\
				link_pass = $2,\
				flag_hub = $3,\
				flag_uworld = $4\
		     WHERE	name = $5",
		    0,
		    stringlist_build(ip, link_pass, (flag_hub ? "t" : "f"),
				     (flag_uworld ? "t" : "f"), name, NULL));
	out("Service `%s' updated successfully", name);

out:
	pgsql_free(res);
	xfree(name);
	xfree(ip);
	xfree(link_pass);
}

CMD_FUNC(service_del)
{
	if(argc < 2)
	{
		out("Usage: delservice <name>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM services WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("A service named `%s' does not exist", argv[1]);
		return;
	}

	pgsql_query("DELETE FROM services WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Service `%s' deleted successfully", argv[1]);
}

// Tab completion stuff
CMD_TAB_FUNC(service_del)
{
	if(CAN_COMPLETE_ARG(1))
		return service_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(service_edit)
{
	if(CAN_COMPLETE_ARG(1))
		return service_generator(text, state);
	return NULL;
}

static char *service_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM services WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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
