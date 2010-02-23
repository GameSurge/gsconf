#ifndef SSH_H
#define SSH_H

#include <libssh2.h>
#include <libssh2_sftp.h>

struct server_info;

struct ssh_session
{
	LIBSSH2_SESSION *session;
	LIBSSH2_SFTP *sftp;
	int fd;
};

struct ssh_exec
{
	struct ssh_session *session;
	LIBSSH2_CHANNEL *channel;
	char *buf;
	size_t getlen, len, size;
	int finished : 1;
};

void ssh_init();
void ssh_fini();
void ssh_set_passphrase(const char *passphrase);
struct ssh_session *ssh_open(struct server_info *server);
void ssh_close(struct ssh_session *session);
int ssh_scp_get(struct ssh_session *session, const char *remote_file, const char *local_file);
int ssh_scp_put(struct ssh_session *session, const char *local_file, const char *remote_file, int mode);
int ssh_exec(struct ssh_session *session, const char *command, char **output);
struct ssh_exec *ssh_exec_async(struct ssh_session *session, const char *command);
int ssh_exec_read(struct ssh_exec *exec, const char **output);
int ssh_exec_close(struct ssh_exec *exec);
int ssh_exec_live(struct ssh_session *session, const char *command);
int ssh_file_exists(struct ssh_session *session, const char *file);

#endif
