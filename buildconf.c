#include "common.h"
#include "buildconf.h"
#include "serverinfo.h"
#include "ssh.h"
#include "configs.h"
#include "pgsql.h"
#include "stringlist.h"
#include "mtrand.h"

#define IRCD_PRIV(PRIV) \
	do { \
		int tmp = atoi(pgsql_nvalue(res, i, "priv_" #PRIV)); \
		if(tmp != -1) \
			fprintf(file, "\t%s = %s;\n", #PRIV, tmp ? "yes" : "no"); \
	} while(0)

static void config_build_header(struct server_info *server, FILE *file)
{
	fprintf(file, "# GameSurge %s - %s\n", serverinfo_name_from_type(server), server->name);
	fprintf(file, "# THIS FILE IS AUTO-GENERATED - DO NOT EDIT\n");
	fprintf(file, "# IF YOU NEED ANYTHING CHANGED, CONTACT NETOPS\n");
	fprintf(file, "# vim:set ft=cfg noet sw=8 ts=8\n");
}

static void config_build_general(struct server_info *server, FILE *file)
{
	fprintf(file, "# General information\n");
	fprintf(file, "General {\n");
	fprintf(file, "\tname = \"%s\";\n", server->name);
	fprintf(file, "\tnumeric = %u;\n", atoi(server->numeric));
	fprintf(file, "\tvhost = \"%s\";\n", server->irc_ip_priv);
	if(server->description)
		fprintf(file, "\tdescription = \"%s\";\n", server->description);
	fprintf(file, "};\n");
}

static void config_build_admin(struct server_info *server, FILE *file)
{
	fprintf(file, "# Admin information\n");
	fprintf(file, "Admin {\n");
	if(server->contact)
		fprintf(file, "\tcontact = \"%s\"\n", server->contact);
	if(server->location1)
		fprintf(file, "\tlocation = \"%s\";\n", server->location1);
	if(server->location2)
		fprintf(file, "\tlocation = \"%s\";\n", server->location2);
	fprintf(file, "};\n");
}

static void config_build_classes_servers(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Server connection classes\n");

	res = pgsql_query("SELECT	c.name,\
					c.pingfreq,\
					c.connectfreq,\
					c.maxlinks,\
					c.sendq\
			   FROM		connclasses_servers c\
			   WHERE 	(c.server_type = '*' AND NOT EXISTS (\
					   SELECT	*\
					   FROM		connclasses_servers c2\
					   WHERE	c2.name = c.name AND\
							c2.server_type = $1\
					)) OR\
					c.server_type = $1\
			   ORDER BY	c.name ASC",
			  1, stringlist_build(serverinfo_db_from_type(server), NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		unsigned int maxlinks = atoi(pgsql_nvalue(res, i, "maxlinks"));

		if(i != 0)
			fputc('\n', file);

		fprintf(file, "Class {\n");
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "\tpingfreq = %s;\n", pgsql_nvalue(res, i, "pingfreq"));
		fprintf(file, "\tconnectfreq = %s;\n", pgsql_nvalue(res, i, "connectfreq"));
		if(maxlinks)
			fprintf(file, "\tmaxlinks = %u;\n", maxlinks);
		fprintf(file, "\tsendq = %lu;\n", strtoul(pgsql_nvalue(res, i, "sendq"), NULL, 10));

		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_classes_clients(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;
	const char *last_class = NULL;

	fprintf(file, "# Client connection classes\n");

	res = pgsql_query("SELECT	CASE\
						WHEN NOT cg.class_maxlinks ISNULL\
						THEN (cg.connclass || '::' || cg.name)\
						ELSE cg.connclass\
					END AS class_name,\
					c.*,\
					cg.class_maxlinks AS maxlinks_override\
			   FROM		clientgroups cg\
			   JOIN		connclasses_users c ON (c.name = cg.connclass)\
			   WHERE	cg.server = $1 AND\
			   		EXISTS (\
						SELECT	*\
						FROM	clients cl\
						WHERE	cl.group = cg.name AND\
							cl.server = cg.server\
					)\
			   ORDER BY	c.class_name ASC\
			   \
			   UNION\
			   \
			   SELECT	o.connclass AS class_name,\
					c.*,\
					NULL AS maxlinks_override\
			   FROM		opers2servers o2s\
			   JOIN		opers o ON (o.name = o2s.oper AND o.active)\
			   JOIN		operhosts oh ON (oh.oper = o.name)\
			   JOIN		connclasses_users c ON (c.name = o.connclass)\
			   WHERE	o2s.server = $1\
			   ORDER BY	c.class_name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		if(last_class && !strcmp(last_class, pgsql_nvalue(res, i, "class_name")))
			continue;

		const char *tmp = pgsql_nvalue(res, i, "maxlinks_override");
		unsigned int maxlinks = tmp ? atoi(tmp) : atoi(pgsql_nvalue(res, i, "maxlinks"));
		const char *usermode = pgsql_nvalue(res, i, "usermode");
		const char *fakehost = pgsql_nvalue(res, i, "fakehost");

		if(i != 0)
			fputc('\n', file);

		fprintf(file, "Class {\n");
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "class_name"));
		fprintf(file, "\tpingfreq = %s;\n", pgsql_nvalue(res, i, "pingfreq"));
		if(maxlinks)
			fprintf(file, "\tmaxlinks = %u;\n", maxlinks);
		fprintf(file, "\tsendq = %lu;\n", strtoul(pgsql_nvalue(res, i, "sendq"), NULL, 10));
		fprintf(file, "\trecvq = %lu;\n", strtoul(pgsql_nvalue(res, i, "recvq"), NULL, 10));
		if(usermode && *usermode)
			fprintf(file, "\tusermode = \"%s\";\n", usermode);
		if(fakehost && *fakehost)
			fprintf(file, "\tfakehost = \"%s\";\n", fakehost);

		IRCD_PRIV(local);
		IRCD_PRIV(die);
		IRCD_PRIV(restart);
		IRCD_PRIV(chan_limit);
		IRCD_PRIV(notargetlimit);
		if(server->type == SERVER_STAFF || server->type == SERVER_BOTS)
		{
			IRCD_PRIV(umode_nochan);
			IRCD_PRIV(umode_noidle);
			IRCD_PRIV(umode_chserv);
			IRCD_PRIV(flood);
			IRCD_PRIV(pseudoflood);
			IRCD_PRIV(gline_immune);
		}

		fprintf(file, "};\n");
		last_class = pgsql_nvalue(res, i, "class_name");
	}

	pgsql_free(res);
}

static void config_build_clients(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Client authorizations\n");

	res = pgsql_query("SELECT	cg.name,\
					cl.ident,\
					cl.ip,\
					cl.host,\
					cg.password,\
					CASE\
						WHEN NOT cg.class_maxlinks ISNULL\
						THEN (cg.connclass || '::' || cg.name)\
						ELSE cg.connclass\
					END AS class_name\
			   FROM		clientgroups cg\
			   JOIN		clients cl ON (cl.group = cg.name AND cl.server = cg.server)\
			   WHERE	cg.server = $1\
			   ORDER BY	(cl.ip ISNULL AND cl.host ISNULL) DESC,\
					strpos(cl.host, '*') >= 1 DESC,\
					COALESCE(masklen(cl.ip), 0) ASC,\
					cg.password = '' DESC,\
					cg.connclass ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		const char *tmp;

		if(i != 0)
			fputc('\n', file);

		fprintf(file, "# %s\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "Client {\n");
		fprintf(file, "\tclass = \"%s\";\n", pgsql_nvalue(res, i, "class_name"));
		if((tmp = pgsql_nvalue(res, i, "ident")))
			fprintf(file, "\tusername = \"%s\";\n", tmp);
		if((tmp = pgsql_nvalue(res, i, "password")))
			fprintf(file, "\tpassword = \"%s\";\n", tmp);
		if((tmp = pgsql_nvalue(res, i, "ip")))
			fprintf(file, "\tip = \"%s\";\n", tmp);
		if((tmp = pgsql_nvalue(res, i, "host")))
			fprintf(file, "\thost = \"%s\";\n", tmp);

		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_operators(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Operators\n");

	res = pgsql_query("SELECT	o.*,\
					oh.mask\
			   FROM		opers2servers o2s\
			   JOIN		opers o ON (o.name = o2s.oper AND o.active)\
			   JOIN		operhosts oh ON (oh.oper = o.name)\
			   WHERE	o2s.server = $1\
			   ORDER BY	o.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		const char *name = pgsql_nvalue(res, i, "name");

		if(i != 0)
			fputc('\n', file);
		fprintf(file, "# %s\n", name);
		fprintf(file, "Operator {\n");
		while(1)
		{
			fprintf(file, "\thost = \"%s\";\n", pgsql_nvalue(res, i, "mask"));
			// Check if next row exists and belongs to the same oper
			if(i >= (rows - 1) || strcmp(name, pgsql_nvalue(res, i + 1, "name")))
				break;
			i++;
		}
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "username"));
		fprintf(file, "\tpassword = \"%s\";\n", pgsql_nvalue(res, i, "password"));
		fprintf(file, "\tclass = \"%s\";\n", pgsql_nvalue(res, i, "connclass"));

		IRCD_PRIV(local);
		IRCD_PRIV(die);
		IRCD_PRIV(restart);
		IRCD_PRIV(notargetlimit);
		if(server->type == SERVER_STAFF || server->type == SERVER_BOTS)
		{
			IRCD_PRIV(umode_nochan);
			IRCD_PRIV(umode_noidle);
			IRCD_PRIV(umode_chserv);
			IRCD_PRIV(flood);
			IRCD_PRIV(pseudoflood);
			IRCD_PRIV(gline_immune);
		}

		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_connects(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Uplinks\n");

	res = pgsql_query("SELECT	s.name,\
					COALESCE(p.ip, server_private_ip(l.server, l.hub, true)) AS irc_ip_priv,\
					COALESCE(p.port, s.server_port) AS server_port,\
					server_private_ip(l.server, l.hub, false) AS vhost,\
					l.autoconnect\
			   FROM		links l\
			   JOIN		servers s ON (s.name = l.hub)\
			   LEFT JOIN	ports p ON (p.id = l.port)\
			   WHERE	l.server = $1",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		const char *vhost;
		int autoconnect = !strcasecmp(pgsql_nvalue(res, i, "autoconnect"), "t");
		char *connclass = "LeafToHub";
		if(server->type == SERVER_HUB)
			connclass = "HubToHub";

		if(i != 0)
			fputc('\n', file);
		fprintf(file, "Connect {\n");
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "\thost = \"%s\";\n", pgsql_nvalue(res, i, "irc_ip_priv"));
		if((vhost = pgsql_nvalue(res, i, "vhost")) && strcmp(vhost, server->irc_ip_priv))
			fprintf(file, "\tvhost = \"%s\";\n", vhost);
		fprintf(file, "\tpassword = \"%s\";\n", server->link_pass);
		fprintf(file, "\tport = %u;\n", atoi(pgsql_nvalue(res, i, "server_port")));
		fprintf(file, "\tclass = \"%s\";\n", connclass);
		fprintf(file, "\tautoconnect = %s;\n", autoconnect ? "yes" : "no");
		fprintf(file, "\thub;\n");
		fprintf(file, "};\n");
	}

	pgsql_free(res);

	if(server->type != SERVER_HUB)
		return;

	// Connect blocks for servers to connect to this hub
	res = pgsql_query("SELECT	s.name,\
					server_private_ip(l.server, l.hub, false) AS irc_ip_priv,\
					s.link_pass,\
					s.server_port,\
					s.type,\
					COALESCE(p.ip, server_private_ip(l.server, l.hub, true)) AS vhost\
			   FROM		links l\
			   JOIN		servers s ON (s.name = l.server)\
			   LEFT JOIN	ports p ON (p.id = l.port)\
			   WHERE	l.hub = $1\
			   ORDER BY	s.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	if(rows)
	{
		fputc('\n', file);
		fprintf(file, "# Uplink for\n");
	}

	for(int i = 0; i < rows; i++)
	{
		const char *vhost;
		char *connclass = "HubToLeaf";
		int type = serverinfo_type_from_db(pgsql_nvalue(res, i, "type"));
		if(type == SERVER_HUB)
			connclass = "HubToHub";

		if(i != 0)
			fputc('\n', file);
		fprintf(file, "Connect {\n");
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "\thost = \"%s\";\n", pgsql_nvalue(res, i, "irc_ip_priv"));
		if((vhost = pgsql_nvalue(res, i, "vhost")) && strcmp(vhost, server->irc_ip_priv))
			fprintf(file, "\tvhost = \"%s\";\n", vhost);
		fprintf(file, "\tpassword = \"%s\";\n", pgsql_nvalue(res, i, "link_pass"));
		fprintf(file, "\tport = %u;\n", atoi(pgsql_nvalue(res, i, "server_port")));
		fprintf(file, "\tclass = \"%s\";\n", connclass);
		fprintf(file, "\tautoconnect = no;\n");
		fprintf(file, "\t%s;\n", ((type == SERVER_HUB) ? "hub" : "leaf"));
		fprintf(file, "};\n");
	}

	pgsql_free(res);

	// Connect blocks for services to connect to this hub
	res = pgsql_query("SELECT	s.name,\
					service_private_ip(sl.service, sl.hub, false) AS ip,\
					s.link_pass,\
					s.flag_hub,\
					service_private_ip(sl.service, sl.hub, true) AS vhost\
			   FROM		servicelinks sl\
			   JOIN		services s ON (s.name = sl.service)\
			   WHERE	sl.hub = $1\
			   ORDER BY	s.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	if(rows)
	{
		fputc('\n', file);
		fprintf(file, "# Service uplink for\n");
	}

	for(int i = 0; i < rows; i++)
	{
		const char *vhost;
		char *connclass = "HubToService";

		if(i != 0)
			fputc('\n', file);
		fprintf(file, "Connect {\n");
		fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "\thost = \"%s\";\n", pgsql_nvalue(res, i, "ip"));
		if((vhost = pgsql_nvalue(res, i, "vhost")) && strcmp(vhost, server->irc_ip_priv))
			fprintf(file, "\tvhost = \"%s\";\n", vhost);
		fprintf(file, "\tpassword = \"%s\";\n", pgsql_nvalue(res, i, "link_pass"));
		fprintf(file, "\tclass = \"%s\";\n", connclass);
		fprintf(file, "\tautoconnect = no;\n");
		fprintf(file, "\t%s;\n", (!strcasecmp(pgsql_nvalue(res, i, "flag_hub"), "t") ? "hub" : "leaf"));
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_ports(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Ports\n");
	// Default server port
	fprintf(file, "Port { port = %u; vhost = \"%s\"; server = yes; hidden = yes; };\n",
		atoi(server->server_port), server->irc_ip_priv);
	// Default server port on local IP
	if(server->irc_ip_priv_local)
	{
		char ip[45], *tmp;
		strlcpy(ip, server->irc_ip_priv_local, sizeof(ip));
		// Get rid of cidr part. We want the plain ip
		if((tmp = strchr(ip, '/')))
			*tmp = '\0';
		fprintf(file, "Port { port = %u; vhost = \"%s\"; server = yes; hidden = yes; };\n",
			atoi(server->server_port), ip);
	}

	res = pgsql_query("SELECT	port,\
					ip,\
					flag_server,\
					flag_hidden\
			   FROM		ports\
			   WHERE	server = $1\
			   ORDER BY	flag_server DESC,\
			   		ip ASC,\
					port ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		int flag_server = !strcasecmp(pgsql_nvalue(res, i, "flag_server"), "t");
		int flag_hidden = !strcasecmp(pgsql_nvalue(res, i, "flag_hidden"), "t");
		unsigned int port = atoi(pgsql_nvalue(res, i, "port"));
		const char *ip = pgsql_nvalue(res, i, "ip");

		if(!ip)
			ip = flag_server ? server->irc_ip_priv : server->irc_ip_pub;

		fprintf(file, "Port { port = %u; vhost = \"%s\"; ", port, ip);
		if(flag_server)
			fprintf(file, "server = yes; ");
		if(flag_hidden)
			fprintf(file, "hidden = yes; ");
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_webirc(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;
	const char *tmp;

	fprintf(file, "# WebIRC\n");

	res = pgsql_query("SELECT	w.*\
			   FROM		webirc2servers w2s\
			   JOIN		webirc w ON (w.name = w2s.webirc)\
			   WHERE	w2s.server = $1\
			   ORDER BY	w.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		const char *ident = pgsql_nvalue(res, i, "ident");

		fprintf(file, "# %s\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "WebIRC {\n");
		fprintf(file, "\thost = \"%s\";\n", pgsql_nvalue(res, i, "ip"));
		fprintf(file, "\tpassword = \"%s\";\n", pgsql_nvalue(res, i, "password"));
		if(ident)
			fprintf(file, "\tident = \"%s\";\n", ident);
		if(!strcasecmp(pgsql_nvalue(res, i, "hmac"), "t"))
		{
			fprintf(file, "\thmac = yes;\n");
			if((tmp = pgsql_nvalue(res, i, "hmac_time")) && atoi(tmp) > 0)
				fprintf(file, "\thmac_time = %s;\n", tmp);
		}
		fprintf(file, "\tdescription = \"%s\";\n", pgsql_nvalue(res, i, "description"));
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_uworld(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Services\n");

	res = pgsql_query("SELECT name FROM services WHERE flag_uworld = true ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	if(rows)
	{
		fprintf(file, "UWorld {\n");
		for(int i = 0; i < rows; i++)
			fprintf(file, "\tname = \"%s\";\n", pgsql_nvalue(res, i, "name"));
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_jupes(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Nick jupes\n");

	res = pgsql_query("SELECT	j.nicks\
			   FROM		jupes2servers j2s\
			   JOIN		jupes j ON (j.name = j2s.jupe)\
			   WHERE	j2s.server = $1\
			   ORDER BY	j.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	if(rows)
	{
		fprintf(file, "Jupe {\n");
		for(int i = 0; i < rows; i++)
			fprintf(file, "\tnick = \"%s\";\n", pgsql_nvalue(res, i, "nicks"));
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_pseudos(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Pseudo commands\n");

	res = pgsql_query("SELECT	p.command,\
					p.name,\
					p.target,\
					p.prepend\
			   FROM		pseudos p\
			   WHERE 	(p.server IS NULL AND NOT EXISTS (\
					   SELECT	*\
					   FROM		pseudos p2\
					   WHERE	p2.command = p.command AND\
							p2.server = $1\
					)) OR\
					p.server = $1\
			   ORDER BY	p.command ASC,\
					p.name ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	for(int i = 0; i < rows; i++)
	{
		const char *prepend = pgsql_nvalue(res, i, "prepend");
		if(prepend)
		{
			// We put a space after the prepent value here; trailing spaces in the DB are ugly
			fprintf(file, "Pseudo \"%s\" { name = \"%s\"; nick = \"%s\"; prepend = \"%s \"; };\n",
				pgsql_nvalue(res, i, "command"), pgsql_nvalue(res, i, "name"),
				pgsql_nvalue(res, i, "target"), pgsql_nvalue(res, i, "prepend"));
		}
		else
		{
			fprintf(file, "Pseudo \"%s\" { name = \"%s\"; nick = \"%s\"; };\n",
				pgsql_nvalue(res, i, "command"), pgsql_nvalue(res, i, "name"),
				pgsql_nvalue(res, i, "target"));
		}
	}

	pgsql_free(res);
}

static void config_build_forwards(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;

	fprintf(file, "# Off-Channel forwards\n");

	res = pgsql_query("SELECT	f.prefix,\
					f.target\
			   FROM		forwards f\
			   WHERE 	(f.server IS NULL AND NOT EXISTS (\
					   SELECT	*\
					   FROM		forwards f2\
					   WHERE	f2.prefix = f.prefix AND\
							f2.server = $1\
					)) OR\
					f.server = $1\
			   ORDER BY	f.prefix ASC",
			  1, stringlist_build(server->name, NULL));
	rows = pgsql_num_rows(res);

	if(rows)
	{
		fprintf(file, "Forwards {\n");
		for(int i = 0; i < rows; i++)
		{
			fprintf(file, "\t\"%c\" = \"%s\";\n",
				pgsql_nvalue(res, i, "prefix")[0],
				pgsql_nvalue(res, i, "target"));
		}
		fprintf(file, "};\n");
	}

	pgsql_free(res);
}

static void config_build_features(struct server_info *server, FILE *file)
{
	PGresult *res;
	int rows;
	FILE *oldconf;
	char line[256];
	char rnd[17];

	fprintf(file, "# ircd features\n");

	res = pgsql_query("SELECT	f.name,\
					f.value\
			   FROM		features f\
			   WHERE 	(f.server_type = '*' AND NOT EXISTS (\
					   SELECT	*\
					   FROM		features f2\
					   WHERE	f2.name = f.name AND\
							f2.server_type = $1\
					)) OR\
					f.server_type = $1\
			   ORDER BY	f.server_type DESC,\
					f.name ASC",
			  1, stringlist_build(serverinfo_db_from_type(server), NULL));
	rows = pgsql_num_rows(res);

	fprintf(file, "Features {\n");
	for(int i = 0; i < rows; i++)
	{
		fprintf(file, "\t\"%s\" = \"%s\";\n",
			pgsql_nvalue(res, i, "name"),
			pgsql_nvalue(res, i, "value"));
	}

	rnd[0] = '\0';

	if(file_exists(config_filename(server, CONFIG_LIVE)) &&
	   (oldconf = fopen(config_filename(server, CONFIG_LIVE), "r")))
	{
		while(fgets(line, sizeof(line), oldconf))
		{
			char *tmp;
			unsigned int i;

			if(!(tmp = strstr(line, "\"RANDOM_SEED\"")))
				continue;

			tmp += 13; // "RANDOM_SEED" - yes, with the quotes
			if(!(tmp = strchr(tmp, '"')) || !*(tmp + 1))
				continue;
			tmp++; // Skip over "

			i = 0;
			while(*tmp && *tmp != '"' && i < sizeof(rnd) - 1)
				rnd[i++] = *tmp++;

			rnd[sizeof(rnd) - 1] = '\0';
			break;
		}

		fclose(oldconf);
	}

	if(!*rnd)
	{
		// Generate a random string
		for(unsigned int i = 0; i < sizeof(rnd) - 1; i++)
		{
			switch(mt_rand(0, 2))
			{
				case 0: rnd[i] = (char)('a' + mt_rand(0, 25)); break;
				case 1: rnd[i] = (char)('a' + mt_rand(0, 25)); break;
				case 2: rnd[i] = (char)('0' + mt_rand(0, 9)); break;
			}
		}

		rnd[sizeof(rnd) - 1] = '\0';
	}

	fprintf(file, "\t\"RANDOM_SEED\" = \"%s\";\n", rnd);
	fprintf(file, "\t\"CONNEXIT_NOTICES\" = \"%s\";\n", server->sno_connexit ? "TRUE" : "FALSE");
	if(server->provider)
		fprintf(file, "\t\"PROVIDER\" = \"%s\";\n", server->provider);

	fprintf(file, "};\n");

	pgsql_free(res);
}

int config_build(struct server_info *server)
{
	FILE *file;

	if(!(file = fopen(config_filename(server, CONFIG_TEMP), "w")))
	{
		error("Could not open temporary file `%s' for writing", config_filename(server, CONFIG_TEMP));
		return 1;
	}

	out("Building config for %s `%s'", serverinfo_name_from_type(server), server->name);
	config_build_header(server, file);
	fputc('\n', file);
	config_build_general(server, file);
	fputc('\n', file);
	config_build_classes_servers(server, file);
	fputc('\n', file);
	config_build_classes_clients(server, file);
	fputc('\n', file);
	config_build_clients(server, file);
	fputc('\n', file);
	config_build_operators(server, file);
	fputc('\n', file);
	config_build_connects(server, file);
	fputc('\n', file);
	config_build_ports(server, file);
	fputc('\n', file);
	config_build_webirc(server, file);
	fputc('\n', file);
	config_build_uworld(server, file);
	fputc('\n', file);
	if(server->type != SERVER_HUB)
	{
		config_build_jupes(server, file);
		fputc('\n', file);
		config_build_pseudos(server, file);
		fputc('\n', file);
	}
	config_build_forwards(server, file);
	fputc('\n', file);
	config_build_features(server, file);
	fputc('\n', file);

	fclose(file);
	rename(config_filename(server, CONFIG_TEMP), config_filename(server, CONFIG_NEW));
	return 0;
}
