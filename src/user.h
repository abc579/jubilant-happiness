#pragma once

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "common.h"
#include "utils.h"

typedef enum {
	NAME_ERR_MIN_LEN,
	NAME_ERR_MAX_LEN,
	NAME_ERR_WSPACE,
	NAME_SYSTEM_ERR,
	NAME_OK
} Name_status_codes;

typedef struct {
	Name_status_codes name_err;
	int system_errno;
} Name_status_codes_wrapper;

typedef struct {
	char name[NAME_SIZE];
} User_t;

Name_status_codes validate_name(const char *);
Name_status_codes_wrapper get_name(char *);
