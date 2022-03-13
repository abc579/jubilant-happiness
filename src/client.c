#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "::1"
#define PORTNO 6969

/* #define MAX_NAME_LEN 16 */
/* #define MIN_NAME_LEN 3 */

/* typedef enum { */
/* 	/\* Minimum length violated. *\/ */
/* 	VAL_MIN_LEN, */
/* 	/\* Maximum length violated. *\/ */
/* 	VAL_MAX_LEN, */
/* 	/\* Name contains whitespace in between. *\/ */
/* 	VAL_WSPACE */
/* } Name_val_codes; */

/* Name_val_codes validate_name(const char *); */

int
main(void)
{
	char buf[256] = "abc";

	int sfd = 0;		/* Server file descriptor. */

	if ((sfd = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		perror("Error creating the socket(): ");
		return EXIT_FAILURE;
	}

	struct sockaddr_in6 sa6; /* Server address IPv6. */
	memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_port = htons(PORTNO);
	sa6.sin6_addr = in6addr_any;

	if ((inet_pton(AF_INET6, SERVER_IP, &sa6.sin6_addr)) <= 0) {
		perror("Error calling inet_pton(): ");
		return EXIT_FAILURE;
	}

	if ((connect(sfd, (struct sockaddr *) &sa6,
		     sizeof(sa6))) == -1) {
		perror("Error connecting to server: ");
		return EXIT_FAILURE;
	}

	if (send(sfd, buf, strlen(buf), 0) == -1) {
		perror("Error sending username to server: ");
		return EXIT_FAILURE;
	}

	close(sfd);

	return EXIT_SUCCESS;
}
