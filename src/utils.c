#include "utils.h"

/*
 * @brief Removes leading whitespaces.
 *
 * @param[in] msg
 *
 * @return MSG without leading whitespaces.
 */
char *
ltrim(char *msg)
{
	if (!msg)
		return NULL;

	if (!*msg)
		return msg;

	while (isspace(*msg))
		++msg;

	return msg;
}

/*
 * @brief Removes trailing whitespaces.

 * @param[in] msg
 *
 * @return MSG without trailing whitespaces.
 */
char *
rtrim(char *msg)
{
	if (!msg)
		return NULL;

	if (!*msg)
		return msg;

	char *tmp = msg + strlen(msg);

	while (isspace(*--tmp));
	*(tmp + 1) = '\0';

	return msg;
}

/*
 * @brief Removes trailing and leading whitespaces.
 *
 * @param[in] msg
 *
 * @return MSG trimmed.
 */
char *
trim(char *msg)
{
	return rtrim(ltrim(msg));
}

/*
 * @brief Flush to end of line. This is done in cases where the
 * input is too large to hold in a char array and we want to recover.
 *
 * Why recover? because in this case, we won't have a newline
 * (since the input is too big) and that will affect subsequent calls
 * making a buggy behavior.
 */
void
flush_endl(void)
{
	int ch;

	while (((ch = getchar()) != '\n') && (ch != EOF))
		;
}
