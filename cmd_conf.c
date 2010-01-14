#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "configs.h"
#include "ssh.h"
#include "input.h"
#include "conf.h"

static char *conf_sync_arg_generator(const char *text, int state);
CMD_FUNC(conf_get);
CMD_TAB_FUNC(conf_get);
CMD_FUNC(conf_put);
CMD_TAB_FUNC(conf_put);
CMD_FUNC(conf_rehash);
CMD_TAB_FUNC(conf_rehash);
CMD_FUNC(conf_check);
CMD_TAB_FUNC(conf_check);
CMD_FUNC(conf_sync);
CMD_TAB_FUNC(conf_sync);
CMD_FUNC(conf_build);
CMD_TAB_FUNC(conf_build);
CMD_FUNC(conf_quicksync);
CMD_TAB_FUNC(conf_quicksync);
CMD_FUNC(conf_get_missing);

static struct command commands[] = {
	CMD_STUB("conf", "Config Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "conf" subcommands
	CMD_TC("get", conf_get, "Download a server's config"),
	CMD_TC("put", conf_put, "Upload a server's config"),
	CMD_TC("rehash", conf_rehash, "Rehash a server's config"),
	CMD_TC("check", conf_check, "Check remote configs for modifications"),
	CMD_TC("sync", conf_sync, "Upload modified configs to the servers"),
	CMD_TC("build", conf_build, "Generate local config files"),
	CMD_TC("quicksync", conf_quicksync, "Generate local config files and then upload them to the servers"),
	CMD("get-missing", conf_get_missing, "Fetch missing configs from remote"),
	CMD_LIST_END
};



void cmd_conf_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "conf");
	cmd_alias("getconf", "conf", "get");
	cmd_alias("putconf", "conf", "put");
	cmd_alias("rehash", "conf", "rehash");
	cmd_alias("checkconf", "conf", "check");
	cmd_alias("syncconfs", "conf", "sync");
	cmd_alias("buildconfs", "conf", "build");
	cmd_alias("quicksync", "conf", "quicksync");
	cmd_alias("commit", "conf", "quicksync");
}

CMD_FUNC(conf_get)
{
	struct server_info *server;

	if(argc < 2)
	{
		out("Usage: getconf <server>");
		return;
	}

	server = serverinfo_load(argv[1]);
	if(!server)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	if(config_download(server, NULL) != 0)
	{
		serverinfo_free(server);
		return;
	}

	out_color(COLOR_LIME, "Config downloaded successfully");
	serverinfo_free(server);
}

CMD_FUNC(conf_put)
{
	struct server_info *server;
	struct ssh_session *session;
	int rehash_manually = 1;

	if(argc < 2)
	{
		out("Usage: putconf <server>");
		return;
	}

	server = serverinfo_load(argv[1]);
	if(!server)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	if(!(session = ssh_open(server)))
	{
		serverinfo_free(server);
		return;
	}

	if(config_upload(server, session, CONFIG_LIVE) != 0)
	{
		ssh_close(session);
		serverinfo_free(server);
		return;
	}

	out_color(COLOR_LIME, "Config uploaded successfully");
	if(readline_yesno("Do you want to rehash the server?", "Yes"))
	{
		char *ircd_path, buf[PATH_MAX];
		if(!(ircd_path = conf_str("ircd_path")))
			ircd_path = "ircu";
		snprintf(buf, sizeof(buf), "%s/lib/ircd.pid", ircd_path);
		if(ssh_file_exists(session, buf))
		{
			snprintf(buf, sizeof(buf), "kill -HUP `cat ~/%s/lib/ircd.pid`", ircd_path);
			rehash_manually = ssh_exec_live(session, buf);
		}
		else
			error("%s/lib/ircd.pid not found", ircd_path);
	}

	if(rehash_manually)
		out_color(COLOR_YELLOW, "Use `rehash %s' to rehash the server", server->name);

	ssh_close(session);
	serverinfo_free(server);
}

CMD_FUNC(conf_rehash)
{
	struct server_info *server;
	struct ssh_session *session;
	char *ircd_path, pidfile[PATH_MAX], cmd[PATH_MAX];

	if(argc < 2)
	{
		out("Usage: rehash <server>");
		return;
	}

	server = serverinfo_load(argv[1]);
	if(!server)
	{
		error("A server named `%s' does not exist", argv[1]);
		return;
	}

	if(!(session = ssh_open(server)))
	{
		serverinfo_free(server);
		return;
	}

	if(!(ircd_path = conf_str("ircd_path")))
		ircd_path = "ircu";
	snprintf(pidfile, sizeof(pidfile), "%s/lib/ircd.pid", ircd_path);
	snprintf(cmd, sizeof(cmd), "kill -HUP `cat ~/%s/lib/ircd.pid`", ircd_path);

	if(!ssh_file_exists(session, pidfile))
		error("Could not rehash `%s': %s/lib/ircd.pid not found", server->name, ircd_path);
	else if(ssh_exec_live(session, cmd) != 0)
		error("Could not rehash `%s'", server->name);
	else
		out_color(COLOR_LIME, "Rehashed `%s'", server->name);

	ssh_close(session);
	serverinfo_free(server);
}

CMD_FUNC(conf_check)
{
	const char *server = argc > 1 ? argv[1] : NULL;
	config_check_remote(server);
}

CMD_FUNC(conf_sync)
{
	int check_remote = 0;
	int auto_update = 0;
	int auto_rehash = 0;
	const char *server = NULL;

	for(int i = 1; i < argc; i++)
	{
		if(!strcmp(argv[i], "--check-remote"))
			check_remote = 1;
		else if(!strcmp(argv[i], "--update"))
			auto_update = 1;
		else if(!strcmp(argv[i], "--rehash"))
			auto_rehash = 1;
		else if(!server)
			server = argv[i];
	}

	config_check_local(server, check_remote, auto_update, auto_rehash);
}

CMD_FUNC(conf_build)
{
	const char *server = argc > 1 ? argv[1] : NULL;
	config_generate(server);
}

CMD_FUNC(conf_quicksync)
{
	int check_remote = 0;
	int auto_update = 0;
	int auto_rehash = 0;
	const char *server = NULL;

	for(int i = 1; i < argc; i++)
	{
		if(!strcmp(argv[i], "--check-remote"))
			check_remote = 1;
		else if(!strcmp(argv[i], "--update"))
			auto_update = 1;
		else if(!strcmp(argv[i], "--rehash"))
			auto_rehash = 1;
		else if(!server)
			server = argv[i];
	}

	config_generate(server);
	config_check_local(server, check_remote, auto_update, auto_rehash);
}

CMD_FUNC(conf_get_missing)
{
	config_get_missing();
}

// Tab completion stuff
CMD_TAB_FUNC(conf_get)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(conf_put)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(conf_rehash)
{
	if(CAN_COMPLETE_ARG(1))
		return server_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(conf_sync)
{
	return conf_sync_arg_generator(text, state);
}

CMD_TAB_FUNC(conf_build)
{
	return server_generator(text, state);
}

CMD_TAB_FUNC(conf_check)
{
	return server_generator(text, state);
}

CMD_TAB_FUNC(conf_quicksync)
{
	return conf_sync_arg_generator(text, state);
}

static char *conf_sync_arg_generator(const char *text, int state)
{
	static const char *values[] = { "--check-remote", "--update", "--rehash", NULL };
	static int idx, chain_state;
	static size_t len;
	const char *val;

	if(!state) // New word
	{
		len = strlen(text);
		idx = 0;
		chain_state = 0;
	}
	else if(state == -1) // Cleanup
	{
		if(idx == -1)
			server_generator(text, -1);
		return NULL;
	}

	// Return the next name which partially matches from the command list.
	while(idx != -1 && (val = values[idx]))
	{
		idx++;
		if(!strncasecmp(val, text, len))
			return strdup(val);
	}

	// Chain in server_generator
	idx = -1;
	return server_generator(text, chain_state++);
}
