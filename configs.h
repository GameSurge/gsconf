#ifndef CONFIGS_H
#define CONFIGS_H

struct server_info;
struct ssh_session;

enum config_type
{
	CONFIG_LIVE,	// Live version (which will be uploaded)
	CONFIG_NEW,	// New version (generated; before moving to live)
	CONFIG_REMOTE,	// Remote version (downloaded to check for changes)
	CONFIG_TEMP,	// Temporary version (while generating new config)
	CONFIG_NUM_TYPES
};

const char *config_filename(struct server_info *server, enum config_type type);
int config_download(struct server_info *server, struct ssh_session *session);
int config_upload(struct server_info *server, struct ssh_session *session, enum config_type type);
void config_generate(const char *server);
int config_check_remote_server(struct server_info *server, enum config_type local_conf, int silent, int keep_remote, struct ssh_session *session);
void config_check_remote(const char *server);
void config_check_local(const char *server, int check_remote, int auto_update, int auto_rehash);
void config_get_missing();

#endif
