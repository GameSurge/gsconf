#include "common.h"
#include "conf.h"
#include "pgsql.h"
#include "stringlist.h"

static PGconn *conn = NULL;

int pgsql_init()
{
	const char *conn_info = conf_get("pg_conn", DB_STRING);
	if(!conn_info || !*conn_info)
	{
		error("No pgsql connection string set");
		return 1;
	}

	conn = PQconnectdb(conn_info);
	if(PQstatus(conn) != CONNECTION_OK)
	{
		error("Connection to database failed: %s", PQerrorMessage(conn));
		PQfinish(conn);
		return 1;
	}

	debug("Connected to pgsql database");
	return 0;
}

void pgsql_fini()
{
	PQfinish(conn);
	conn = NULL;
}

void pgsql_free(PGresult *res)
{
	if(res)
		PQclear(res);
}

int pgsql_num_rows(PGresult *res)
{
	return PQntuples(res);
}

const char *pgsql_value(PGresult *res, int row, int col)
{
	if(PQgetisnull(res, row, col))
		return NULL;
	return PQgetvalue(res, row, col);
}

const char *pgsql_nvalue(PGresult *res, int row, const char *col)
{
	int fnum = PQfnumber(res, col);
	if(fnum == -1)
	{
		error("Field `%s' does not exist", col);
		exit(1);
	}

	if(PQgetisnull(res, row, fnum))
		return NULL;
	return PQgetvalue(res, row, fnum);
}

PGresult *pgsql_query(const char *query, int want_result, struct stringlist *params)
{
	PGresult *res = NULL;

	res = PQexecParams(conn, query, params ? params->count : 0, NULL, params ? (const char*const*)params->data : NULL, NULL, NULL, 0);
	switch(PQresultStatus(res))
	{
		case PGRES_COMMAND_OK:
		case PGRES_TUPLES_OK:
			break;

		default:
			error("Unexpected PG result status (%s): %s", PQresStatus(PQresultStatus(res)), PQresultErrorMessage(res));
			exit(1);
	}

	if(params)
		stringlist_free(params);

	if(!want_result)
	{
		PQclear(res);
		return NULL;
	}

	return res;
}

int pgsql_query_int(const char *query, struct stringlist *params)
{
	int val = 0;
	const char *tmp;
	PGresult *res = pgsql_query(query, 1, params);
	if(pgsql_num_rows(res) > 0 && (tmp = pgsql_value(res, 0, 0)))
		val = atoi(tmp);
	pgsql_free(res);
	return val;
}

int pgsql_query_bool(const char *query, struct stringlist *params)
{
	return !strcasecmp(pgsql_query_str(query, params), "t");
}

char *pgsql_query_str(const char *query, struct stringlist *params)
{
	static char buf[1024];
	const char *tmp;
	memset(buf, 0, sizeof(buf));
	PGresult *res = pgsql_query(query, 1, params);
	if(pgsql_num_rows(res) > 0 && (tmp = pgsql_value(res, 0, 0)))
		strlcpy(buf, tmp, sizeof(buf));
	pgsql_free(res);
	return buf;
}

int pgsql_valid_for_type(const char *value, const char *type)
{
	const char *tmp;
	int valid = 0;
	PGresult *res = pgsql_query("SELECT valid_for_type($1, $2)", 1, stringlist_build(value, type, NULL));
	assert(pgsql_num_rows(res) == 1);
	assert((tmp = pgsql_value(res, 0, 0)));
	valid = !strcasecmp(tmp, "t");
	pgsql_free(res);
	return valid;
}

void pgsql_begin()
{
	pgsql_query("BEGIN TRANSACTION", 0, NULL);
}

void pgsql_commit()
{
	pgsql_query("COMMIT", 0, NULL);
}

void pgsql_rollback()
{
	pgsql_query("ROLLBACK", 0, NULL);
}
