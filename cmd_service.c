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

	table = table_create(7, rows);
	table_set_header(table, "Name", "Numeric", "IP", "Local IP", "Link Pass", "Hub", "UWorld");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char *)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char *)pgsql_nvalue(res, i, "numeric"));
		table_col_str(table, i, 2, (char *)pgsql_nvalue(res, i, "ip"));
		table_col_str(table, i, 3, (char *)pgsql_nvalue(res, i, "ip_local"));
		table_col_str(table, i, 4, (char *)pgsql_nvalue(res, i, "link_pass"));
		table_col_str(table, i, 5, (!strcasecmp(pgsql_nvalue(res, i, "flag_hub"), "t") ? "Yes" : "No"));
		table_col_str(table, i, 6, (!strcasecmp(pgsql_nvalue(res, i, "flag_uworld"), "t") ? "Yes" : "No"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(service_add)
{
	const char *line;
	char *name, *numeric, *ip, *ip_local, *link_pass;
	int flag_hub, flag_uworld;
	name = numeric = ip = ip_local = link_pass = NULL;

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

	// Prompt service numeric
	// Note: We don't support numeric 0. That makes checking stuff much easier.
	while(1)
	{
		line = readline_noac("Service numeric", pgsql_query_str("SELECT MAX(numeric) + 1 FROM services", NULL));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		int val = atoi(line);
		if(val < 1)
		{
			error("Invalid numeric, must be a positive number");
			continue;
		}

		char *str = pgsql_query_str("SELECT name FROM servers WHERE numeric = $1", stringlist_build(line, NULL));
		if(*str)
		{
			error("A server with this numeric already exists (%s)", str);
			continue;
		}

		str = pgsql_query_str("SELECT name FROM services WHERE numeric = $1", stringlist_build(line, NULL));
		if(*str)
		{
			error("A service with this numeric already exists (%s)", str);
			continue;
		}

		asprintf(&numeric, "%u", val);
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

	// Prompt local ip
	int has_local_ip = readline_yesno("Does this service have a local IP?", "No");
	if(has_local_ip)
	{
		out("Enter the IP in CIDR style. If two servers have local IPs which are in the same subnet, they will be linked via those ips.");
		while(1)
		{
			line = readline_noac("Local IP", NULL);
			if(!line)
				goto out;
			else if(!*line)
				continue;

			if(!strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
			{
				error("This IP doesn't look like a valid IP mask");
				continue;
			}

			ip_local = strdup(line);
			break;
		}
	}
	else
	{
		ip_local = NULL;
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
			(name, numeric, ip, ip_local, link_pass,\
			 flag_hub, flag_uworld)\
		     VALUES\
		     	($1, $2, $3, $4, $5,\
			 $6, $7)",
		    0,
		    stringlist_build_n(7, name, numeric, ip, ip_local, link_pass,
			    		  (flag_hub ? "t" : "f"), (flag_uworld ? "t" : "f")));
	out("Service `%s' added successfully", name);

out:
	xfree(name);
	xfree(numeric);
	xfree(ip);
	xfree(ip_local);
	xfree(link_pass);
}

CMD_FUNC(service_edit)
{
	const char *line;
	char *name, *numeric, *ip, *ip_local, *link_pass;
	int flag_hub, flag_uworld;
	name = numeric = ip = ip_local = link_pass = NULL;
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

	// Prompt service numeric
	// Note: We don't support numeric 0. That makes checking stuff much easier.
	while(1)
	{
		line = readline_noac("Service numeric", pgsql_nvalue(res, 0, "numeric"));
		if(!line)
			goto out;
		else if(!*line)
			continue;

		int val = atoi(line);
		if(val < 1)
		{
			error("Invalid numeric, must be a positive number");
			continue;
		}

		char *str = pgsql_query_str("SELECT name FROM servers WHERE numeric = $1", stringlist_build(line, NULL));
		if(*str)
		{
			error("A server with this numeric already exists (%s)", str);
			continue;
		}

		str = pgsql_query_str("SELECT name FROM services WHERE numeric = $1 AND name != $2", stringlist_build(line, name, NULL));
		if(*str)
		{
			error("A service with this numeric already exists (%s)", str);
			continue;
		}

		asprintf(&numeric, "%u", val);
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

	// Prompt local ip
	int has_local_ip = readline_yesno("Does this service have a local IP?", pgsql_nvalue(res, 0, "ip_local") ? "Yes" : "No");
	if(has_local_ip)
	{
		out("Enter the IP in CIDR style. If two servers have local IPs which are in the same subnet, they will be linked via those ips.");
		while(1)
		{
			line = readline_noac("Local IP", pgsql_nvalue(res, 0, "ip_local"));
			if(!line)
				goto out;
			else if(!*line)
				continue;

			if(!strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
			{
				error("This IP doesn't look like a valid IP mask");
				continue;
			}

			ip_local = strdup(line);
			break;
		}
	}
	else
	{
		ip_local = NULL;
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
				ip_local = $2,\
				link_pass = $3,\
				flag_hub = $4,\
				flag_uworld = $5,\
				numeric = $6\
		     WHERE	name = $7",
		    0,
		    stringlist_build_n(7, ip, ip_local, link_pass, (flag_hub ? "t" : "f"),
					  (flag_uworld ? "t" : "f"), numeric, name));
	out("Service `%s' updated successfully", name);

out:
	pgsql_free(res);
	xfree(name);
	xfree(numeric);
	xfree(ip);
	xfree(ip_local);
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
