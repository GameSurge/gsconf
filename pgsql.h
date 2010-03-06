#ifndef PGSQL_H
#define PGSQL_H

#include <libpq-fe.h>

struct stringlist;

int pgsql_init();
void pgsql_fini();

void pgsql_free(PGresult *res);
int pgsql_num_rows(PGresult *res);
int pgsql_num_affected(PGresult *res);
const char *pgsql_value(PGresult *res, int row, int col);
const char *pgsql_nvalue(PGresult *res, int row, const char *col);
PGresult *pgsql_query(const char *query, int want_result, struct stringlist *params);
int pgsql_query_int(const char *query, struct stringlist *params);
int pgsql_query_bool(const char *query, struct stringlist *params);
char *pgsql_query_str(const char *query, struct stringlist *params);
int pgsql_valid_for_type(const char *value, const char *type);
void pgsql_begin();
void pgsql_commit();
void pgsql_rollback();

#endif
