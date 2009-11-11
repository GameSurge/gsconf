#include "common.h"
#include "diff.h"
#include "conf.h"

int diff(const char *file1, const char *file2, int silent)
{
	char cmd[512];
	const char *tmp;
	size_t len = 0;
	int ret;

	tmp = conf_str(silent ? "diff_silent" : "diff");
	if(!tmp || strlen(tmp) >= 124 || !strstr(tmp, "$1") || !strstr(tmp, "$2"))
	{
		error("Diff command must contain $1 and $2 and shorter than 124 chars");
		tmp = silent ? "diff -Nuwq $1 $2 >/dev/null" : "diff -Nuw $1 $2";
	}

	expand_num_args(cmd, sizeof(cmd), tmp, 2, file1, file2);
	ret = system(cmd);

	if(ret == -1)
	{
		error("Could not execute diff: %s (%d)", strerror(errno), errno);
		return -1;
	}
	else if(WEXITSTATUS(ret) == 127)
	{
		error("Could not execute diff command: %s", cmd);
		return -1;
	}
	else if(WEXITSTATUS(ret) != 0 && WEXITSTATUS(ret) != 1)
	{
		error("Got unexpected exit code from diff: %d", WEXITSTATUS(ret));
		return -1;
	}

	return WEXITSTATUS(ret);
}
