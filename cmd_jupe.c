#include "common.h"
#include "cmd.h"
#include "pgsql.h"
#include "stringlist.h"
#include "serverinfo.h"
#include "input.h"
#include "table.h"

static char *jupe_generator(const char *text, int state);
CMD_FUNC(jupe_list);
CMD_FUNC(jupe_add);
CMD_FUNC(jupe_del);
CMD_TAB_FUNC(jupe_del);
CMD_FUNC(jupe_edit);
CMD_TAB_FUNC(jupe_edit);

static struct command commands[] = {
	CMD_STUB("jupe", "Nick Jupe Management"),
	CMD_LIST_END
};

static struct command subcommands[] = {
	// "jupe" subcommands
	CMD("list", jupe_list, "Show jupes"),
	CMD("add", jupe_add, "Add a jupe"),
	CMD_TC("del", jupe_del, "Remove a jupe"),
	CMD_TC("edit", jupe_edit, "Edit a jupe"),
	CMD_LIST_END
};



void cmd_jupe_init()
{
	cmd_register_list(commands, NULL);
	cmd_register_list(subcommands, "jupe");
	cmd_alias("jupes", "jupe", "list");
	cmd_alias("addjupe", "jupe", "add");
	cmd_alias("deljupe", "jupe", "del");
	cmd_alias("editjupe", "jupe", "edit");
}

CMD_FUNC(jupe_list)
{
	PGresult *res;
	int rows;
	struct table *table;

	res = pgsql_query("SELECT * FROM jupes ORDER BY name ASC", 1, NULL);
	rows = pgsql_num_rows(res);

	table = table_create(2, rows);
	table_set_header(table, "Name", "Nicks");

	for(int i = 0; i < rows; i++)
	{
		table_col_str(table, i, 0, (char*)pgsql_nvalue(res, i, "name"));
		table_col_str(table, i, 1, (char*)pgsql_nvalue(res, i, "nicks"));
	}

	table_send(table);
	table_free(table);
	pgsql_free(res);
}

CMD_FUNC(jupe_add)
{
	char *line, *tmp;
	char *name = NULL, *nicks = NULL;

	while(1)
	{
		line = readline_noac("Jupe name", NULL);
		if(!line || !*line)
			return;

		int cnt = pgsql_query_int("SELECT COUNT(*) FROM jupes WHERE lower(name) = lower($1)", stringlist_build(line, NULL));
		if(cnt)
		{
			error("A jupe with this name already exists");
			continue;
		}

		name = strdup(line);
		break;
	}

	out("You can enter multiple nicks by separating them with a comma");
	while(1)
	{
		line = readline_noac("Nicks", NULL);
		if(!line)
			goto out;
		else if(!*line)
			continue;

		// Fix dumb user mistakes (space/semicolon instead of comma)
		while((tmp = strchr(line, ' ')) || (tmp = strchr(line, ';')))
			*tmp = ',';

		// Fix empty nicks (double commas)
		while((tmp = strstr(line, ",,")))
			memmove(tmp, tmp + 1, strlen(tmp + 1) + 1);

		// Strip leading commas
		while(*line == ',')
			line++;

		// Strip trailing commas
		for(size_t len = strlen(line); len && line[len - 1] == ','; len--)
			line[len - 1] = '\0';

		if(!*line)
			continue;

		nicks = strdup(line);
		break;
	}

	pgsql_query("INSERT INTO jupes (name, nicks) VALUES ($1, $2)", 0, stringlist_build(name, nicks, NULL));
	out("Jupe `%s' added successfully", name);

	if(readline_yesno("Add this jupe to all non-hub servers?", "Yes"))
	{
		pgsql_query("INSERT INTO jupes2servers (jupe, server) SELECT $1, name FROM servers WHERE type != 'HUB'", 0, stringlist_build(name, NULL));
		out("Jupe added to servers successfully");
	}

out:
	xfree(name);
	xfree(nicks);
}

CMD_FUNC(jupe_del)
{
	if(argc < 2)
	{
		out("Usage: deljupe <name>");
		return;
	}

	int cnt = pgsql_query_int("SELECT COUNT(*) FROM jupes WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!cnt)
	{
		error("There is no jupe named `%s'", argv[1]);
		return;
	}

	pgsql_query("DELETE FROM jupes WHERE lower(name) = lower($1)", 0, stringlist_build(argv[1], NULL));
	out("Jupe `%s' deleted successfully", argv[1]);
}

CMD_FUNC(jupe_edit)
{
	char *line;
	char *tmp, *nicks;

	if(argc < 2)
	{
		out("Usage: editjupe <name>");
		return;
	}

	tmp = pgsql_query_str("SELECT nicks FROM jupes WHERE lower(name) = lower($1)", stringlist_build(argv[1], NULL));
	if(!*tmp)
	{
		error("There is no jupe named `%s'", argv[1]);
		return;
	}

	readline_default_text = tmp;
	while(1)
	{
		line = readline_noac("Nicks", NULL);
		if(!line)
			return;
		else if(!*line)
			continue;

		// Fix dumb user mistakes (space/semicolon instead of comma)
		while((tmp = strchr(line, ' ')) || (tmp = strchr(line, ';')))
			*tmp = ',';

		// Fix empty nicks (double commas)
		while((tmp = strstr(line, ",,")))
			memmove(tmp, tmp + 1, strlen(tmp + 1) + 1);

		// Strip leading commas
		while(*line == ',')
			line++;

		// Strip trailing commas
		for(size_t len = strlen(line); len && line[len - 1] == ','; len--)
			line[len - 1] = '\0';

		if(!*line)
			continue;

		nicks = strdup(line);
		break;
	}

	pgsql_query("UPDATE jupes SET nicks = $1 WHERE lower(name) = lower($2)", 0, stringlist_build(nicks, argv[1], NULL));
	out("Jupe `%s' updated successfully", argv[1]);

	free(nicks);
}

// Tab completion stuff
CMD_TAB_FUNC(jupe_del)
{
	if(CAN_COMPLETE_ARG(1))
		return jupe_generator(text, state);
	return NULL;
}

CMD_TAB_FUNC(jupe_edit)
{
	if(CAN_COMPLETE_ARG(1))
		return jupe_generator(text, state);
	return NULL;
}

static char *jupe_generator(const char *text, int state)
{
	static int row, rows;
	static size_t len;
	static PGresult *res;
	const char *name;

	if(!state) // New word
	{
		row = 0;
		len = strlen(text);
		res = pgsql_query("SELECT name FROM jupes WHERE name ILIKE $1||'%'", 1, stringlist_build(text, NULL));
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

