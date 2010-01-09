#ifndef TABLE_H
#define TABLE_H

typedef size_t (table_strlen_f)(const char *str, unsigned int col);

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
	table_strlen_f	*field_len;
};

size_t table_strlen(const char *str, unsigned int col);
size_t table_strlen_colors(const char *str, unsigned int col);
struct table *table_create(unsigned int cols, unsigned int rows);
void table_set_header(struct table *table, const char *str, ...);
void table_bold_column(struct table *table, unsigned int col, unsigned char enable);
void table_free_column(struct table *table, unsigned int col, unsigned char enable);
void table_ralign_column(struct table *table, unsigned int col, unsigned char enable);
void table_free(struct table *table);
void table_send(struct table *table);
void table_sort(struct table *table, unsigned int col);

void table_col_str(struct table *table, unsigned int row, unsigned int col, char *val);
void table_col_num(struct table *table, unsigned int row, unsigned int col, unsigned int val);
void table_col_fmt(struct table *table, unsigned int row, unsigned int col, const char *fmt, ...) PRINTF_LIKE(4,5);

#endif
