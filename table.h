#ifndef TABLE_H
#define TABLE_H

struct table
{
	unsigned int	cols;
	unsigned int	rows;
	unsigned long	bold_cols;
	unsigned long	free_cols;
	unsigned long	ralign_cols;
	const char	**header;
	char		***data;
	const char 	*prefix;
};

struct table *table_create(unsigned int cols, unsigned int rows);
void table_set_header(struct table *table, const char *str, ...);
void table_bold_column(struct table *table, unsigned int col, unsigned char enable);
void table_free_column(struct table *table, unsigned int col, unsigned char enable);
void table_ralign_column(struct table *table, unsigned int col, unsigned char enable);
void table_free(struct table *table);
void table_send(struct table *table);

void table_col_str(struct table *table, unsigned int row, unsigned int col, char *val);
void table_col_num(struct table *table, unsigned int row, unsigned int col, unsigned int val);
void table_col_fmt(struct table *table, unsigned int row, unsigned int col, const char *fmt, ...) PRINTF_LIKE(4,5);

#endif
