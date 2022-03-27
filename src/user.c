#include "user.h"

/*
 * @brief Returns NAME_OK if all validations were successful.
 * Otherwise, the corresponding enumerator indicating which error NAME has.
 *
 * @param[in] name
 */
Name_status_codes
validate_name(const char *name)
{
	int len = strlen(name);

	if (len > NAME_SIZE - 1)
		return NAME_ERR_MAX_LEN;
	else if (len < MIN_NAME_LEN)
		return NAME_ERR_MIN_LEN;

	if (strstr(name, " ") != NULL)
		return NAME_ERR_WSPACE;

	return NAME_OK;
}

/*
 * @brief Waits for user input and validates it.
 *
 * @param[in out] name Name typed by the user.
 */
Name_status_codes_wrapper
get_name(char *name)
{
	Name_status_codes_wrapper necw;

	if ((fgets(name, NAME_SIZE - 1, stdin)) == NULL) {
		necw.name_err = NAME_SYSTEM_ERR;
		necw.system_errno = errno;
		return necw;
	}

	if (name[strlen(name) - 1] != '\n') { /* Name too long. */
		flush_endl();
	}

	trim(name);
	name[strcspn(name, "\n")] = '\0'; /* Get rid of the newline. */

	Name_status_codes ne = validate_name(name);
	necw.name_err = ne;
	necw.system_errno = 0;

	return necw;
}
