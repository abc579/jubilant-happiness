#include "utils.h"

/*
 * @brief Removes leading whitespaces.
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
 */
char *
trim(char *msg)
{
	return rtrim(ltrim(msg));
}
