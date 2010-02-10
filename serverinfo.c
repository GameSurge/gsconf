#include "common.h"
#include "serverinfo.h"
#include "input.h"
#include "pgsql.h"
#include "stringlist.h"
#include "conf.h"

static struct server_type server_types[] = {
	{ "0", NULL, NULL },
	{ "1", "LEAF", "Leaf" },
	{ "2", "HUB", "Hub" },
	{ "3", "STAFF", "Staff" },
	{ "4", "BOTS", "Bots" }
};

const char *serverinfo_db_from_type(struct server_info *info)
{
	assert(info->type >= 1 && info->type <= 4);
	return server_types[info->type].db_name;
}

const char *serverinfo_name_from_type(struct server_info *info)
{
	assert(info->type >= 1 && info->type <= 4);
	return server_types[info->type].name;
}

int serverinfo_type_from_db(const char *db_type)
{
	for(int i = 1; i <= 4; i++)
	{
		if(!strcasecmp(server_types[i].db_name, db_type))
			return i;
	}

	assert(0 && "invalid server type in database");
	return 0;
}

struct server_info *serverinfo_load_pg(PGresult *res, int row)
{
	struct server_info *data = malloc(sizeof(struct server_info));
	memset(data, 0, sizeof(struct server_info));
	data->free_self = 1;

#define LOAD_FIELD(FIELD)	data->FIELD = xstrdup(pgsql_nvalue(res, row, #FIELD));
	LOAD_FIELD(name);
	LOAD_FIELD(numeric);
	LOAD_FIELD(link_pass);
	LOAD_FIELD(irc_ip_priv);
	LOAD_FIELD(irc_ip_priv_local);
	LOAD_FIELD(irc_ip_pub);
	LOAD_FIELD(server_port);
	LOAD_FIELD(description);
	LOAD_FIELD(contact);
	LOAD_FIELD(location1);
	LOAD_FIELD(location2);
	LOAD_FIELD(provider);
	LOAD_FIELD(ssh_user);
	LOAD_FIELD(ssh_host);
	LOAD_FIELD(ssh_port);
#undef LOAD_FIELD
	data->type = serverinfo_type_from_db(pgsql_nvalue(res, row, "type"));
	data->sno_connexit = !strcasecmp(pgsql_nvalue(res, row, "sno_connexit"), "t") ? 1 : 0;

	return data;
}

struct server_info *serverinfo_load(const char *server)
{
	struct server_info *data;
	PGresult *res;

	res = pgsql_query("SELECT * FROM servers WHERE lower(name) = lower($1)", 1, stringlist_build(server, NULL));
	if(!pgsql_num_rows(res))
	{
		pgsql_free(res);
		return NULL;
	}

	data = serverinfo_load_pg(res, 0);
	pgsql_free(res);
	return data;
}

void serverinfo_copy(struct server_info *to, struct server_info *from)
{
#define COPY_FIELD(FIELD)	to->FIELD = from->FIELD ? strdup(from->FIELD) : NULL
	COPY_FIELD(name);
	COPY_FIELD(numeric);
	COPY_FIELD(link_pass);
	COPY_FIELD(irc_ip_priv);
	COPY_FIELD(irc_ip_priv_local);
	COPY_FIELD(irc_ip_pub);
	COPY_FIELD(server_port);
	COPY_FIELD(description);
	COPY_FIELD(contact);
	COPY_FIELD(location1);
	COPY_FIELD(location2);
	COPY_FIELD(provider);
	COPY_FIELD(ssh_user);
	COPY_FIELD(ssh_host);
	COPY_FIELD(ssh_port);
	COPY_FIELD(link_pass);
#undef COPY_FIELD

	to->type = from->type;
	to->sno_connexit = from->sno_connexit;
}

struct server_info *serverinfo_prompt(struct server_info *current)
{
	static struct server_info data;
	char *line;

	memset(&data, 0, sizeof(struct server_info));
	if(current)
		serverinfo_copy(&data, current);

again:
	// Prompt/show server name
	if(current && current->name)
	{
		out("Server name: %s", data.name);
	}
	else
	{
		while(1)
		{
			line = readline_noac("Server name", data.name);
			if(!line || !*line)
				goto out;

			if(!strchr(line, '.') || line[0] == '.' || line[strlen(line) - 1] == '.')
			{
				error("Server name must contain a `.' which is not at the beginning/end");
				continue;
			}

			int cnt = pgsql_query_int("SELECT COUNT(*) FROM servers WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
			if(cnt)
			{
				error("A server with this name already exists");
				continue;
			}

			xfree(data.name);
			data.name = strdup(line);
			break;
		}
	}

	// Prompt server type
	if(current && current->type == 2) // Hub
	{
		out("Type: %s", server_types[data.type].name);
	}
	else
	{
		while(1)
		{
			if(current)
				line = readline_noac("Type (1 = Leaf, \033[" COLOR_DARKGRAY "m2 = Hub\033[0m, 3 = Staff, 4 = Bots)", (data.type ? server_types[data.type].idx : "1"));
			else
				line = readline_noac("Type (1 = Leaf, 2 = Hub, 3 = Staff, 4 = Bots)", (data.type ? server_types[data.type].idx : "1"));

			if(!line)
				goto out;

			int tmp_type = atoi(line);
			if(tmp_type < 1 || tmp_type > 4)
				continue;
			else if(current && tmp_type == 2)
			{
				error("To convert this server into a hub, delete and re-add it");
				continue;
			}

			data.type = tmp_type;
			break;
		}
	}

	// Prompt server numeric
	// Note: We don't support numeric 0. That makes checking stuff much easier.
	while(1)
	{
		line = readline_noac("Server numeric", data.numeric ? data.numeric : pgsql_query_str("SELECT MAX(numeric) + 1 FROM servers", NULL));
		if(!line)
			goto out;

		if(!*line)
			continue;

		int val = atoi(line);
		if(val < 1)
		{
			error("Invalid numeric, must be a positive number");
			continue;
		}

		char *str = pgsql_query_str("SELECT name FROM servers WHERE numeric = $1 AND name != $2", stringlist_build(line, data.name, NULL));
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

		xfree(data.numeric);
		asprintf(&data.numeric, "%u", val);
		break;
	}

	// Prompt link password
	while(1)
	{
		line = readline_noac("Link password", data.link_pass);
		if(!line)
			goto out;

		if(!*line)
			continue;

		xfree(data.link_pass);
		data.link_pass = strdup(line);
		break;
	}

	// Prompt private server ip
	while(1)
	{
		line = readline_noac("Private IP (use non-local IP if the server has both)", data.irc_ip_priv);
		if(!line)
			goto out;

		if(!*line)
			continue;

		if(strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
		{
			error("This IP doesn't look like a valid IP");
			continue;
		}

		xfree(data.irc_ip_priv);
		data.irc_ip_priv = strdup(line);
		break;
	}

	// Prompt local server ip
	int has_local_ip = readline_yesno("Does this server have a local IP?", data.irc_ip_priv_local ? "Yes" : "No");
	if(has_local_ip)
	{
		out("Enter the IP in CIDR style. If two servers have local IPs which are in the same subnet, they will be linked via those ips.");
		while(1)
		{
			line = readline_noac("Local IP", data.irc_ip_priv_local);
			if(!line)
				goto out;

			if(!*line)
				continue;

			if(!strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
			{
				error("This IP doesn't look like a valid IP mask");
				continue;
			}

			xfree(data.irc_ip_priv_local);
			data.irc_ip_priv_local = strdup(line);
			break;
		}
	}
	else
	{
		xfree(data.irc_ip_priv_local);
		data.irc_ip_priv_local = NULL;
	}

	// Prompt public server ip
	while(1)
	{
		line = readline_noac("Public IP", data.irc_ip_pub ? data.irc_ip_pub : data.irc_ip_priv);
		if(!line)
			goto out;

		if(!*line)
			continue;

		if(strchr(line, '/') || !pgsql_valid_for_type(line, "inet"))
		{
			error("This IP doesn't look like a valid IP");
			continue;
		}

		xfree(data.irc_ip_pub);
		data.irc_ip_pub = strdup(line);
		break;
	}

	// Prompt server port
	while(1)
	{
		line = readline_noac("Server port", data.server_port ? data.server_port : conf_str("defaults/server_port"));
		if(!line)
			goto out;

		if(!*line)
			continue;

		int val = atoi(line);
		if(val <= 1024)
		{
			error("Invalid port, must be a positive number above 1024");
			continue;
		}

		xfree(data.server_port);
		data.server_port = strdup(line);
		break;
	}

	// Prompt server description
	line = readline_noac("Description", data.description ? data.description : "");
	if(!line)
		goto out;
	else if(!*line)
	{
		xfree(data.description);
		data.description = NULL;
	}
	else
	{
		xfree(data.description);
		data.description = strdup(line);
	}

	// Prompt admin contact
	line = readline_noac("Contact", data.contact ? data.contact : "");
	if(!line)
		goto out;
	else if(!*line)
	{
		xfree(data.contact);
		data.contact = NULL;
	}
	else
	{
		xfree(data.contact);
		data.contact = strdup(line);
	}

	// Prompt location 1
	line = readline_noac("Location 1", data.location1 ? data.location1 : "");
	if(!line)
		goto out;
	else if(!*line)
	{
		xfree(data.location1);
		xfree(data.location2);
		data.location1 = NULL;
		data.location2 = NULL;
	}
	else
	{
		xfree(data.location1);
		data.location1 = strdup(line);
	}

	if(data.location1)
	{
		// Prompt location 2
		line = readline_noac("Location 2", data.location2 ? data.location2 : "");
		if(!line)
			goto out;
		else if(!*line)
		{
			xfree(data.location2);
			data.location2 = NULL;
		}
		else
		{
			xfree(data.location2);
			data.location2 = strdup(line);
		}
	}

	// Prompt provider name
	line = readline_noac("Provider (F:PROVIDER)", data.provider ? data.provider : "");
	if(!line)
		goto out;
	else if(!*line)
	{
		xfree(data.provider);
		data.provider = NULL;
	}
	else
	{
		xfree(data.provider);
		data.provider = strdup(line);
	}

	// Prompt SNO_CONNEXIT setting
	while(1)
	{
		line = readline_noac("Allow CONNEXIT SNotices?", data.sno_connexit ? "Yes" : "No");
		if(true_string(line))
		{
			data.sno_connexit = 1;
			break;
		}
		else if(false_string(line))
		{
			data.sno_connexit = 0;
			break;
		}
	}

	// Prompt SSH username
	while(1)
	{
		line = readline_noac("SSH Username", data.ssh_user ? data.ssh_user : "gsircd");
		if(!line)
			goto out;

		if(!*line)
			continue;

		xfree(data.ssh_user);
		data.ssh_user = strdup(line);
		break;
	}

	// Prompt SSH host
	while(1)
	{
		line = readline_noac("SSH Host/IP", data.ssh_host);
		if(!line)
			goto out;

		if(!*line)
			continue;

		xfree(data.ssh_host);
		data.ssh_host = strdup(line);
		break;
	}

	// Prompt SSH port
	while(1)
	{
		line = readline_noac("SSH Port", data.ssh_port ? data.ssh_port : "22");
		if(!line)
			goto out;

		if(!*line)
			continue;

		int val = atoi(line);
		if(val < 1)
		{
			error("Invalid port, must be a positive number");
			continue;
		}

		xfree(data.ssh_port);
		data.ssh_port = strdup(line);
		break;
	}

	// Verify data
#define SHOW_OPTIONAL(FIELD) ((data.FIELD) ? (data.FIELD) : "\033[30;1mn/a\033[0m")
#define FIELD_CHANGED_I(FIELD)	((current && data.FIELD != current->FIELD) ? "\033[32;1m*\033[0m" : " ")
#define FIELD_CHANGED(FIELD)	((current && (\
					(data.FIELD && !current->FIELD) || \
					(!data.FIELD && current->FIELD) || \
					(data.FIELD && current->FIELD && strcmp(data.FIELD, current->FIELD))) \
				) ? "\033[32;1m*\033[0m" : " ")
	putc('\n', stdout);
	out("Please verify if the following information is correct:");
	out(" Name:        %s", data.name);
	out("%sType:        %s", FIELD_CHANGED_I(type), server_types[data.type].name);
	out("%sNumeric:     %s", FIELD_CHANGED(numeric), data.numeric);
	out("%sLink Pass:   %s", FIELD_CHANGED(link_pass), data.link_pass);
	out("%sPriv IP:     %s", FIELD_CHANGED(irc_ip_priv), data.irc_ip_priv);
	out("%sLocal IP:    %s", FIELD_CHANGED(irc_ip_priv_local), SHOW_OPTIONAL(irc_ip_priv_local));
	out("%sPub IP:      %s", FIELD_CHANGED(irc_ip_pub), data.irc_ip_pub);
	out("%sServer Port: %s", FIELD_CHANGED(server_port), data.server_port);
	out("%sDescr:       %s", FIELD_CHANGED(description), SHOW_OPTIONAL(description));
	out("%sContact:     %s", FIELD_CHANGED(contact), SHOW_OPTIONAL(contact));
	out("%sLocation 1:  %s", FIELD_CHANGED(location1), SHOW_OPTIONAL(location1));
	out("%sLocation 2:  %s", FIELD_CHANGED(location2), SHOW_OPTIONAL(location2));
	out("%sProvider:    %s", FIELD_CHANGED(provider), SHOW_OPTIONAL(provider));
	out("%sCONNEXIT:    %s", FIELD_CHANGED_I(sno_connexit), data.sno_connexit ? "Yes" : "No");
	const char *tmp = FIELD_CHANGED(ssh_host);
	if(*tmp == ' ') // If ssh_host was not changed, check ssh_user
		tmp = FIELD_CHANGED(ssh_user);
	out("%sSSH Login:   %s@%s", tmp, data.ssh_user, data.ssh_host);
	out("%sSSH Port:    %s", FIELD_CHANGED(ssh_port), data.ssh_port);
#undef FIELD_CHANGED
#undef FIELD_CHANGED_I
#undef SHOW_OPTIONAL

	while(1)
	{
		line = readline_noac("Is everything correct?", "Yes");
		if(true_string(line))
			break;
		else if(false_string(line))
		{
			putc('\n', stdout);
			goto again;
		}
	}

	return &data;
out:
	serverinfo_free(&data);
	return NULL;
}

void serverinfo_show(struct server_info *info)
{
#define SHOW_OPTIONAL(FIELD) ((info->FIELD) ? (info->FIELD) : "\033[30;1mn/a\033[0m")
	out("Name:        %s", info->name);
	out("Type:        %s", server_types[info->type].name);
	out("Numeric:     %s", info->numeric);
	out("Link Pass:   %s", info->link_pass);
	out("Priv IP:     %s", info->irc_ip_priv);
	out("Local IP:    %s", SHOW_OPTIONAL(irc_ip_priv_local));
	out("Pub IP:      %s", info->irc_ip_pub);
	out("Server Port: %s", info->server_port);
	out("Descr:       %s", SHOW_OPTIONAL(description));
	out("Contact:     %s", SHOW_OPTIONAL(contact));
	out("Location 1:  %s", SHOW_OPTIONAL(location1));
	out("Location 2:  %s", SHOW_OPTIONAL(location2));
	out("Provider:    %s", SHOW_OPTIONAL(provider));
	out("CONNEXIT:    %s", info->sno_connexit ? "Yes" : "No");
	out("SSH Login:   %s@%s", info->ssh_user, info->ssh_host);
	out("SSH Port:    %s", info->ssh_port);
#undef SHOW_OPTIONAL
}

void serverinfo_free(struct server_info *info)
{
	xfree(info->name);
	xfree(info->numeric);
	xfree(info->link_pass);
	xfree(info->irc_ip_priv);
	xfree(info->irc_ip_priv_local);
	xfree(info->irc_ip_pub);
	xfree(info->server_port);
	xfree(info->description);
	xfree(info->contact);
	xfree(info->location1);
	xfree(info->location2);
	xfree(info->provider);
	xfree(info->ssh_user);
	xfree(info->ssh_host);
	xfree(info->ssh_port);
	if(info->free_self)
		free(info);
}
