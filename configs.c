#include "common.h"
#include "configs.h"
#include "serverinfo.h"
#include "conf.h"
#include "ssh.h"
#include "diff.h"
#include "pgsql.h"
#include "input.h"
#include "buildconf.h"
#include "stringlist.h"

static int config_generate_server(struct server_info *server);

const char *config_filename(struct server_info *server, enum config_type type)
{
	static char filenames[CONFIG_NUM_TYPES][PATH_MAX];
	const char *fmt = NULL;

	assert(type < CONFIG_NUM_TYPES);

	switch(type)
	{
		case CONFIG_LIVE:
			fmt = conf_str("ircd_conf/live");
			break;
		case CONFIG_NEW:
			fmt = conf_str("ircd_conf/new");
			break;
		case CONFIG_REMOTE:
			fmt = conf_str("ircd_conf/remote");
			break;
		case CONFIG_TEMP:
			fmt = conf_str("ircd_conf/temp");
			break;
		default:
			assert(0 && "invalid config type");
	}

	assert(fmt);
	assert(strstr(fmt, "$1"));

	expand_num_args(filenames[type], sizeof(filenames[type]), fmt, 1, server->name);
	return filenames[type];
}

int config_download(struct server_info *server, struct ssh_session *session)
{
	int close_session = 0;
	const char *filename;

	if(!session)
	{
		close_session = 1;
		if(!(session = ssh_open(server)))
			return 1;
	}

	if(!ssh_file_exists(session, "ircu/lib/"))
	{
		error("ircu doesn't seem to be installed on `%s': ~/ircu/lib/ doesn't exist", server->name);
		if(close_session)
			ssh_close(session);
		return 1;
	}

	filename = config_filename(server, CONFIG_REMOTE);
	debug("Downloading config from `%s' to `%s'", server->name, filename);
	if(ssh_scp_get(session, "ircu/lib/ircd.conf", filename) != 0)
	{
		if(close_session)
			ssh_close(session);
		return 1;
	}

	if(close_session)
		ssh_close(session);
	return 0;
}

int config_upload(struct server_info *server, struct ssh_session *session, enum config_type type)
{
	int close_session = 0;

	if(!session)
	{
		close_session = 1;
		if(!(session = ssh_open(server)))
			return 1;
	}

	if(!ssh_file_exists(session, "ircu/lib/"))
	{
		error("ircu doesn't seem to be installed on `%s': ~/ircu/lib/ doesn't exist", server->name);
		if(close_session)
			ssh_close(session);
		return 1;
	}

	debug("Uploading config to `%s'", server->name);
	if(ssh_scp_put(session, config_filename(server, type), "ircu/lib/ircd.conf") != 0)
	{
		if(close_session)
			ssh_close(session);
		return 1;
	}

	if(close_session)
		ssh_close(session);
	return 0;
}

// Regenerate local configs
void config_generate(const char *server)
{
	PGresult *res;
	int rows;

	if(server)
		res = pgsql_query("SELECT * FROM servers WHERE lower(name) = lower($1)", 1, stringlist_build(server, NULL));
	else
		res = pgsql_query("SELECT * FROM servers ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);
	for(int i = 0; i < rows; i++)
	{
		struct server_info *server = serverinfo_load_pg(res, i);
		config_build(server);
		serverinfo_free(server);
	}

	pgsql_free(res);
}

int config_check_remote_server(struct server_info *server, enum config_type local_conf, int silent, int keep_remote, struct ssh_session *session)
{
	int ret;

	if(!file_exists(config_filename(server, local_conf)))
	{
		out_color(COLOR_LIGHT_RED, "Local ircd.conf for `%s' does not exist; run `buildconfs' to generate config files", server->name);
		return 0;
	}

	config_download(server, session);

	if(!file_exists(config_filename(server, CONFIG_REMOTE)))
	{
		ret = 0;
		if(!silent)
			out_color(COLOR_LIGHT_RED, "ircd.conf on `%s' does not exist", server->name);
	}
	else if(diff(config_filename(server, local_conf), config_filename(server, CONFIG_REMOTE), silent) == 0)
	{
		ret = 1;
		if(!silent)
			out_color(COLOR_LIME, "ircd.conf on `%s' matches the local version", server->name);
	}
	else
	{
		ret = 0;
		if(!silent)
			out_color(COLOR_YELLOW, "ircd.conf on `%s' differs", server->name);
	}

	if(!keep_remote)
		unlink(config_filename(server, CONFIG_REMOTE));
	return ret;
}

// Check if remote files differ from local files
void config_check_remote(const char *server)
{
	PGresult *res;
	int rows;

	if(server)
		res = pgsql_query("SELECT * FROM servers WHERE lower(name) = lower($1)", 1, stringlist_build(server, NULL));
	else
		res = pgsql_query("SELECT * FROM servers ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);
	for(int i = 0; i < rows; i++)
	{
		struct server_info *server = serverinfo_load_pg(res, i);
		config_check_remote_server(server, CONFIG_LIVE, 0, 0, NULL);
		serverinfo_free(server);
	}

	pgsql_free(res);
}

// Check if new local files differ from old local files
void config_check_local(const char *server, int check_remote, int auto_update, int auto_rehash)
{
	PGresult *res;
	int rows;

	if(server)
		res = pgsql_query("SELECT * FROM servers WHERE lower(name) = lower($1)", 1, stringlist_build(server, NULL));
	else
		res = pgsql_query("SELECT * FROM servers ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);
	for(int i = 0; i < rows; i++)
	{
		struct server_info *server = serverinfo_load_pg(res, i);
		struct ssh_session *session = NULL;
		int rehash_manually = 1;
		int update_conf = 1;

		out_prefix("\033[" COLOR_BROWN "m[%s]\033[0m ", server->name);

		if(!file_exists(config_filename(server, CONFIG_NEW)))
		{
			out_color(COLOR_LIME, "New ircd.conf does not exist");
			serverinfo_free(server);
			continue;
		}

		if(file_exists(config_filename(server, CONFIG_LIVE)) &&
		   diff(config_filename(server, CONFIG_LIVE), config_filename(server, CONFIG_NEW), 1) == 0)
			update_conf = 0;

		// Open SSH session if necessary
		if(update_conf || check_remote)
		{
			debug("Connecting via SSH");
			if(!(session = ssh_open(server)))
			{
				serverinfo_free(server);
				continue;
			}
		}

		if(!update_conf && !check_remote)
		{
			// Local file matches and no remote check requested
			out_color(COLOR_LIME, "ircd.conf matches the old version (checked local)");
			unlink(config_filename(server, CONFIG_NEW));
		}
		else if(!update_conf && config_check_remote_server(server, CONFIG_NEW, 1, 1, session))
		{
			// Locale file matches but remote check requested and passed
			out_color(COLOR_LIME, "ircd.conf matches the old version (checked remote)");
			unlink(config_filename(server, CONFIG_NEW));
			unlink(config_filename(server, CONFIG_REMOTE));
		}
		else
		{
			// Local file different or local file matching but remote file differed
			out_color(COLOR_YELLOW, "ircd.conf needs to be updated");

			if(!update_conf) // Remote conf differed -> show diff
			{
				diff(config_filename(server, CONFIG_REMOTE), config_filename(server, CONFIG_NEW), 0);
				unlink(config_filename(server, CONFIG_REMOTE));
			}
			else
				diff(config_filename(server, CONFIG_LIVE), config_filename(server, CONFIG_NEW), 0);

			if(auto_update || readline_yesno("Update now?", "Yes"))
			{
				if(config_upload(server, session, CONFIG_NEW) == 0)
				{
					out_color(COLOR_LIME, "ircd.conf uploaded successfully");

					if(rename(config_filename(server, CONFIG_NEW), config_filename(server, CONFIG_LIVE)) != 0)
						error("Could not rename new config file: %s (%d)", strerror(errno), errno);

					if(auto_rehash || readline_yesno("Rehash the ircd?", "Yes"))
					{
						if(ssh_file_exists(session, "ircu/lib/ircd.pid"))
							rehash_manually = ssh_exec_live(session, "kill -HUP `cat ~/ircu/lib/ircd.pid`");
						else
							error("ircu/lib/ircd.pid not found");
					}

					// Rehash failed or not rehashed
					if(rehash_manually)
						out_color(COLOR_YELLOW, "Use `rehash %s' to rehash the server", server->name);
					else
						out_color(COLOR_LIME, "ircd rehashed successfully");
				}
			}
		}

		if(session)
			ssh_close(session);
		serverinfo_free(server);
	}

	out_prefix(NULL);
	pgsql_free(res);
}

// Get remote configs if local config is missing
void config_get_missing()
{
	PGresult *res;
	int rows;

	res = pgsql_query("SELECT * FROM servers ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);
	for(int i = 0; i < rows; i++)
	{
		struct server_info *server = serverinfo_load_pg(res, i);
		out_prefix("\033[" COLOR_BROWN "m[%s]\033[0m ", server->name);
		if(file_exists(config_filename(server, CONFIG_LIVE)))
		{
			out_color(COLOR_LIME, "Local config exists; no need to fetch");
			serverinfo_free(server);
			continue;
		}

		if(config_download(server, NULL) == 0)
		{
			rename(config_filename(server, CONFIG_REMOTE), config_filename(server, CONFIG_LIVE));
			out_color(COLOR_GREEN, "Fetched remote config");
		}
		serverinfo_free(server);
	}

	out_prefix(NULL);
	pgsql_free(res);
}
