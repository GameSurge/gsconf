#ifndef SERVERINFO_H
#define SERVERINFO_H

#include <libpq-fe.h>

struct server_type
{
	const char *idx;
	const char *db_name;
	const char *name;
};

enum
{
	SERVER_LEAF = 1,
	SERVER_HUB,
	SERVER_STAFF,
	SERVER_BOTS
};

struct server_info
{
	char *name;
	int type;
	char *numeric;
	char *link_pass;
	char *irc_ip_priv;
	char *irc_ip_pub;
	char *server_port;
	char *description;
	char *contact;
	char *location1;
	char *location2;
	char *provider;
	int sno_connexit;
	char *ssh_user;
	char *ssh_host;
	char *ssh_port;

	PGresult *db_res;
	int free_self;
};

const char *serverinfo_db_from_type(struct server_info *info);
const char *serverinfo_name_from_type(struct server_info *info);
int serverinfo_type_from_db(const char *db_type);
struct server_info *serverinfo_load_pg(PGresult *res, int row);
struct server_info *serverinfo_load(const char *server);
struct server_info *serverinfo_prompt(struct server_info *current);
void serverinfo_show(struct server_info *info);
void serverinfo_free(struct server_info *info);

#endif
