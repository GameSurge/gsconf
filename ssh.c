#include "common.h"
#include "ssh.h"
#include "conf.h"
#include "input.h"
#include "serverinfo.h"
#include "main.h"

static int ssh_socket(struct server_info *server);
static int ssh_auth(struct ssh_session *session, struct server_info *server);
static const char *ssh_error(struct ssh_session *session);
static void ssh_waitsocket(struct ssh_session *session);
static int ssh_sftp(struct ssh_session *session);

static char *last_passphrase = NULL;

void ssh_init()
{

}

void ssh_fini()
{
	xfree(last_passphrase);
}

void ssh_set_passphrase(const char *passphrase)
{
	xfree(last_passphrase);
	last_passphrase = strdup(passphrase);
}

static int ssh_socket(struct server_info *server)
{
	struct hostent *hp;
	struct sockaddr_in sin;
	int sock;

	if((hp = gethostbyname2(server->ssh_host, AF_INET)) == NULL)
	{
		error("[%s] Could not resolve %s", server->name, server->ssh_host);
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(sock >= 0);

	memset(&sin, 0, sizeof(struct sockaddr_in));

	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(server->ssh_port));
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(struct in_addr));

	if(connect(sock, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) < 0)
	{
		error("[%s] Could not connect to %s:%s (IPv4): %s (%d)", server->name, server->ssh_host, server->ssh_port, strerror(errno), errno);
		close(sock);
		return -2;
	}

	return sock;
}

static int ssh_auth(struct ssh_session *session, struct server_info *server)
{
	const char *methods;
	const char *pubkey, *privkey;
	const char *password = NULL;

	// Cancel SSH auth with SIGINT
	if(sigsetjmp(sigint_jmp_buf, 1) != 0)
		return 2;
	sigint_received = 0;
	sigint_jmp_on = 1;

	assert((methods = libssh2_userauth_list(session->session, server->ssh_user, strlen(server->ssh_user))));
	//debug("Supported authentication methods: %s", methods);

	if(strstr(methods, "publickey") && (pubkey = conf_str("sshkey/pub")) && (privkey = conf_str("sshkey/priv")))
	{
		//debug("Trying publickey auth");
		int ask_passphrase = conf_bool("sshkey/ask_passphrase");
		for(int i = 0; i < 3; i++)
		{
			if(ask_passphrase && !last_passphrase)
				password = readline_noecho("SSH key passphrase");
			else if(last_passphrase)
				password = last_passphrase;
			else
				password = conf_str("sshkey/passphrase");

			if(libssh2_userauth_publickey_fromfile(session->session, server->ssh_user, pubkey, privkey, password ? password : "") == 0)
			{
				if(last_passphrase != password)
				{
					xfree(last_passphrase);
					last_passphrase = xstrdup(password);
				}

				sigint_jmp_on = 0;
				return 0;
			}

			out("Pubkey auth failed: %s", ssh_error(session));
			xfree(last_passphrase);
			last_passphrase = NULL;
			ask_passphrase = 1;
		}
	}

	// TODO: Maybe implement keyboard-interactive authentication

	// Do not try password auth in batch mode. We cannot ask for a password and sending 3 empty passwords is stupid
	if(!batch_mode && strstr(methods, "password"))
	{
		//debug("Trying password auth");
		for(int i = 0; i < 3; i++)
		{
			password = readline_noecho("SSH password");
			if(libssh2_userauth_password(session->session, server->ssh_user, password ? password : "") == 0)
			{
				sigint_jmp_on = 0;
				return 0;
			}
			out("Password auth failed");
		}
	}

	sigint_jmp_on = 0;
	return 1;
}

static const char *ssh_error(struct ssh_session *session)
{
	static char buf[256];
	char *err;
	libssh2_session_last_error(session->session, &err, NULL, 0);
	strlcpy(buf, err, sizeof(buf));
	return trim(buf);
}

static void ssh_waitsocket(struct ssh_session *session)
{
	struct timeval timeout;
	fd_set fd;
	fd_set *writefd = NULL;
	fd_set *readfd = NULL;
	int dir;

	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	FD_ZERO(&fd);
	FD_SET(session->fd, &fd);

	dir = libssh2_session_block_directions(session->session);

	if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
		readfd = &fd;
	if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
		writefd = &fd;

	select(session->fd + 1, readfd, writefd, NULL, &timeout);
}

struct ssh_session *ssh_open(struct server_info *server)
{
	int sock = 0;
	struct ssh_session *session;

	// Create socket
	if((sock = ssh_socket(server)) < 0)
		return NULL;

	session = malloc(sizeof(struct ssh_session));
	memset(session, 0, sizeof(struct ssh_session));
	session->fd = sock;

	// Init ssh session
	if(!(session->session = libssh2_session_init()))
	{
		error("Could not init ssh session");
		free(session);
		return NULL;
	}

	// Startup ssh session (handshake etc.)
	if(libssh2_session_startup(session->session, sock) != 0)
	{
		error("Could not startup ssh session: %s", ssh_error(session));
		libssh2_session_disconnect(session->session, "Session startup failed");
		libssh2_session_free(session->session);
		close(sock);
		free(session);
		return NULL;
	}

	// Authenticate
	if(ssh_auth(session, server) != 0)
	{
		libssh2_session_disconnect(session->session, "Authentication failed");
		libssh2_session_free(session->session);
		close(sock);
		free(session);
		return NULL;
	}

	// Authenticated successfully
	return session;
}

static int ssh_sftp(struct ssh_session *session)
{
	// Already initialized?
	if(session->sftp)
		return 0;

	// Initialize new sftp session
	if(!(session->sftp = libssh2_sftp_init(session->session)))
	{
		error("Could not init sftp session: %s", ssh_error(session));
		return -1;
	}

	return 0;
}

void ssh_close(struct ssh_session *session)
{
	if(session->sftp)
		libssh2_sftp_shutdown(session->sftp);
	libssh2_session_disconnect(session->session, "Finished");
	libssh2_session_free(session->session);
	close(session->fd);
	free(session);
}

int ssh_scp_get(struct ssh_session *session, const char *remote_file, const char *local_file)
{
	LIBSSH2_CHANNEL *channel;
	struct stat fileinfo;
	long long received = 0;
	FILE *outfile;

	if(!(outfile = fopen(local_file, "w")))
	{
		error("Could not open `%s' for writing: %s (%d)", local_file, strerror(errno), errno);
		return 1;
	}

	if(!(channel = libssh2_scp_recv(session->session, remote_file, &fileinfo)))
	{
		error("Could not create scp receive channel: %s", ssh_error(session));
		fclose(outfile);
		unlink(local_file);
		return 2;
	}

	while(received < fileinfo.st_size)
	{
		char buf[10240];
		int res;
		int readlen = min(fileinfo.st_size - received, sizeof(buf));

		res = libssh2_channel_read(channel, buf, readlen);
		if(res > 0)
		{
			assert(res == readlen);
			fwrite(buf, 1, res, outfile);
		}
		else
		{
			if(received)
				putc('\n', stdout);
			error("Could not read from ssh channel: %s", ssh_error(session));
			fclose(outfile);
			unlink(local_file);
			libssh2_channel_free(channel);
			return 2;
		}

		received += res;
	}

	debug("Downloaded %llu/%llu bytes", received, fileinfo.st_size);

	libssh2_channel_free(channel);
	fclose(outfile);
	return 0;
}

int ssh_scp_put(struct ssh_session *session, const char *local_file, const char *remote_file)
{
	LIBSSH2_CHANNEL *channel;
	struct stat fileinfo;
	long long sent = 0;
	FILE *infile;

	if(stat(local_file, &fileinfo) != 0)
	{
		error("Could not stat() `%s': %s (%d)", local_file, strerror(errno), errno);
		return 1;
	}

	if(!(infile = fopen(local_file, "r")))
	{
		error("Could not open `%s' for reading: %s (%d)", local_file, strerror(errno), errno);
		return 1;
	}

	if(!(channel = libssh2_scp_send(session->session, remote_file, 0600, fileinfo.st_size)))
	{
		error("Could not create scp send channel: %s", ssh_error(session));
		fclose(infile);
		return 2;
	}

	libssh2_channel_set_blocking(channel, 1);
	while(sent < fileinfo.st_size)
	{
		char buf[10240];
		char *ptr;
		int res, block;
		int readlen = min(fileinfo.st_size - sent, sizeof(buf));

		block = fread(buf, 1, readlen, infile);
		if(block <= 0)
		{
			error("Could not read from file");
			break;
		}

		assert(block == readlen);

		ptr = buf;
		while(block)
		{
			res = libssh2_channel_write(channel, ptr, block);
			if(res >= 0)
			{
				ptr += res;
				sent += res;
				block -= res;
			}
			else
			{
				if(sent)
					putc('\n', stdout);
				error("Could not write to ssh channel: %s", ssh_error(session));
				libssh2_channel_send_eof(channel);
				libssh2_channel_wait_eof(channel);
				libssh2_channel_wait_closed(channel);
				libssh2_channel_free(channel);
				fclose(infile);
				return 2;
			}
		}
	}

	debug("Uploaded %llu/%llu bytes", sent, fileinfo.st_size);

	libssh2_channel_send_eof(channel);
	libssh2_channel_wait_eof(channel);
	libssh2_channel_wait_closed(channel);
	libssh2_channel_free(channel);
	fclose(infile);

	return 0;
}

int ssh_exec(struct ssh_session *session, const char *command, char **output)
{
	LIBSSH2_CHANNEL *channel;
	char *buf;
	int len, size;
	int res, exitcode;

	if(!(channel = libssh2_channel_open_session(session->session)))
	{
		error("Could not create session channel: %s", ssh_error(session));
		return -1;
	}

	if(libssh2_channel_exec(channel, command) != 0)
	{
		error("Unable to execute command: %s", ssh_error(session));
		libssh2_channel_free(channel);
		return -1;
	}

	libssh2_channel_set_blocking(channel, 0);

	len = 0;
	size = 256;
	buf = malloc(size);
	while(1)
	{
		if(size - len < 2)
		{
			size <<= 1; // double size
			buf = realloc(buf, size);
		}

		res = libssh2_channel_read(channel, buf + len, size - len - 1);
		if(res > 0)
		{
			// If output is not wanted, simply read into the same buffer and overwrite old contents
			if(output)
				len += res;
			continue;
		}
		else if(res == 0)
			break;
		else if(res != LIBSSH2_ERROR_EAGAIN)
		{
			error("Could not read from ssh channel: %s %d", ssh_error(session), res);
			free(buf);
			libssh2_channel_free(channel);
			libssh2_session_set_blocking(session->session, 1);
			return -1;
		}

		ssh_waitsocket(session);
	}

	buf[len] = '\0';
	if(!output)
		free(buf);

	exitcode = 127;
	while((res = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN)
		ssh_waitsocket(session);

	if(res == 0)
		exitcode = libssh2_channel_get_exit_status(channel);

	if(output)
		*output = buf;
	libssh2_channel_free(channel);
	libssh2_session_set_blocking(session->session, 1);
	return exitcode;
}

struct ssh_exec *ssh_exec_async(struct ssh_session *session, const char *command)
{
	LIBSSH2_CHANNEL *channel;
	struct ssh_exec *exec;

	if(!(channel = libssh2_channel_open_session(session->session)))
	{
		error("Could not create session channel: %s", ssh_error(session));
		return NULL;
	}

	libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);
	if(libssh2_channel_exec(channel, command) != 0)
	{
		error("Unable to execute command: %s", ssh_error(session));
		libssh2_channel_free(channel);
		return NULL;
	}

	libssh2_channel_set_blocking(channel, 0);

	exec = malloc(sizeof(struct ssh_exec));
	memset(exec, 0, sizeof(struct ssh_exec));
	exec->session = session;
	exec->channel = channel;
	return exec;
}

int ssh_exec_read(struct ssh_exec *exec, const char **line)
{
	assert(line);

	*line = NULL;

	if(exec->finished)
		return 1;

	if(!exec->buf)
	{
		exec->size = 256;
		exec->len = 0;
		exec->buf = malloc(exec->size);
	}

	if(exec->getlen)
	{
		size_t retlen;

		// Get rid of fetched data
		memmove(exec->buf, exec->buf + exec->getlen, exec->size - exec->getlen);
		exec->len -= exec->getlen;
		exec->buf[exec->len] = '\0';
		exec->getlen = 0;

		// Check if there's more data to fetch
		if((retlen = strcspn(exec->buf, "\n")) > 0 && *(exec->buf + retlen) == '\n')
		{
			exec->getlen = retlen + 1;
			exec->buf[retlen] = '\0';
			*line = exec->buf;
			return 0;
		}
		else if(*exec->buf == '\n') // empty line
		{
			exec->getlen = 1;
			exec->buf[0] = '\0';
			*line = exec->buf; // empty string
			return 0;
		}
	}

	while(1)
	{
		int res;
		size_t retlen;

		if(exec->size - exec->len < 2)
		{
			exec->size <<= 1; // double size
			exec->buf = realloc(exec->buf, exec->size);
		}

		res = libssh2_channel_read(exec->channel, exec->buf + exec->len, exec->size - exec->len - 1);
		if(res > 0)
		{
			exec->len += res;
			exec->buf[exec->len] = '\0';

			// Return everything until \n and remove the read string and its \n from the buffer
			if(((retlen = strcspn(exec->buf, "\n")) > 0 && *(exec->buf + retlen) == '\n') || (*exec->buf == '\n'))
			{
				exec->getlen = retlen + 1;
				exec->buf[retlen] = '\0';
				*line = exec->buf;
				return 0;
			}
			else if(*exec->buf == '\n') // empty line
			{
				exec->getlen = 1;
				exec->buf[0] = '\0';
				*line = exec->buf; // empty string
				return 0;
			}

			continue;
		}
		else if(res == 0)
		{
			// EOF (Channel closed)
			break;
		}
		else if(res != LIBSSH2_ERROR_EAGAIN)
		{
			error("Could not read from ssh channel: %s", ssh_error(exec->session));
			return -1;
		}

		ssh_waitsocket(exec->session);
	}

	if(!exec->len)
		return 1;

	exec->finished = 1;
	exec->buf[exec->len] = '\0';
	*line = exec->buf;
	return 0;
}

int ssh_exec_close(struct ssh_exec *exec)
{
	int res, exitcode = 127;

	while((res = libssh2_channel_close(exec->channel)) == LIBSSH2_ERROR_EAGAIN)
		ssh_waitsocket(exec->session);

	if(res == 0)
		exitcode = libssh2_channel_get_exit_status(exec->channel);

	libssh2_channel_free(exec->channel);
	libssh2_session_set_blocking(exec->session->session, 1);
	if(exec->buf)
		free(exec->buf);
	free(exec);
	return exitcode;
}

int ssh_exec_live(struct ssh_session *session, const char *command)
{
	struct ssh_exec *exec;
	const char *buf;
	int ret;

	if(!(exec = ssh_exec_async(session, command)))
		return -1;

	while(ssh_exec_read(exec, &buf) == 0)
	{
		if(buf)
			out("%s", buf);
	}

	ret = ssh_exec_close(exec);
	if(ret == 127)
		error("Could not execute `%s'", command);
	else if(ret != 0)
		out_color(COLOR_BROWN, "Command exited with code %d", ret);
	return ret;
}

int ssh_file_exists(struct ssh_session *session, const char *file)
{
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	assert(ssh_sftp(session) == 0);
	return (libssh2_sftp_stat(session->sftp, file, &attrs) == 0);
}
