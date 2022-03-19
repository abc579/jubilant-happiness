#define _XOPEN_SOURCE 500

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"

#define SERVER_IP "::1"
#define PORTNO 6969
#define QUIT_CMD "!quit"
#define MIN_NAME_LEN 3

/* User-defined types. */
typedef enum
{
	/* Minimum length violated. */
	NAME_ERR_MIN_LEN,
	/* Maximum length violated. */
	NAME_ERR_MAX_LEN,
	/* Name contains whitespace in between. */
	NAME_ERR_WSPACE,
	NAME_OK
} Name_err_codes;

typedef struct
{
	char name[NAME_SIZE];
	int sfd; /* Server to which the client is connected. */
} client_data_t;

/* Functions. */
static Name_err_codes validate_name(const char*);
static void *listen_from_server(void*);
static void *prompt_user(void*);
static void sig_quit_program(int signo);

/* Globals. */
volatile sig_atomic_t g_quit = 0;

int
main(void)
{
	if (system("clear") < 0)
	{
		perror("Couldn't clear the screen: ");
	}

	char name[NAME_SIZE] = "";

	puts("Please type your name: ");

	if ((fgets(name, NAME_SIZE, stdin)) == NULL)
	{
		perror("Error reading name: ");
		return EXIT_FAILURE;
	}

	name[strcspn(name, "\n")] = '\0'; /* Get rid of the newline. */

	Name_err_codes ne = validate_name(name);

	switch (ne)
	{
	case NAME_ERR_MIN_LEN:
		fprintf(stderr, "Your name has to be at least %d "
				"characters long.", MIN_NAME_LEN);
		break;
	case NAME_ERR_MAX_LEN:
		fprintf(stderr, "Your name can't exceed %d characters"
				" long.", NAME_SIZE);
		break;
	case NAME_ERR_WSPACE:
		fprintf(stderr, "Your name can't contain a whitespace in between.");
		break;
	default:
		break;
	}

	int sfd = 0; /* Server file descriptor. */

	if ((sfd = socket(AF_INET6, SOCK_STREAM, 0)) == -1)
	{
		perror("Error creating the socket(): ");
		return EXIT_FAILURE;
	}

	struct sockaddr_in6 sa6; /* Server address IPv6. */
	memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_port = htons(PORTNO);
	sa6.sin6_addr = in6addr_any;

	if ((inet_pton(AF_INET6, SERVER_IP, &sa6.sin6_addr)) <= 0)
	{
		perror("Error calling inet_pton(): ");
		return EXIT_FAILURE;
	}

	if ((connect(sfd, (struct sockaddr*) &sa6, sizeof(sa6))) == -1)
	{
		perror("Error connecting to server: ");
		fprintf(stderr, "Try again later.\n");
		return EXIT_FAILURE;
	}

	char buff[BUFF_SIZE] = "";

	/* Know if server rejected our connection. */
	if ((recv(sfd, &buff, sizeof(buff), 0)) == -1)
	{
		perror("Error recv'ing status from server after "
				"connecting: ");
		return EXIT_FAILURE;
	}

	if (strstr(buff, ERR_STATUS) != NULL)
	{
		fprintf(stderr, "The server is full. Please try "
				"again later.\n");
		return EXIT_FAILURE;
	}

	/*
	 * Send the name to the server so it can validate that no other
	 * client exists with the same name.
	 */
	if ((send(sfd, name, strlen(name), 0)) == -1)
	{
		perror("Error sending name to the server: ");
		return EXIT_FAILURE;
	}

	memset(buff, 0, sizeof(buff));
	if ((recv(sfd, &buff, sizeof(buff), 0)) == -1)
	{
		perror("Error recv'ing status from server after sending the name: ");
		return EXIT_FAILURE;
	}

	if (strstr(buff, ERR_STATUS) != NULL)
	{
		fprintf(stderr, "Your name already exists. Try again.\n");
		return EXIT_FAILURE;
	}

	puts("\n****************************");
	puts("****************************");
	puts("* Welcome to the Chat Room *");
	puts("****************************");
	puts("****************************");
	puts("\nType !quit to leave the chatroom.");
	puts("Type !list to show all clients connected to the chatroom.");
	puts("Type !whisp and the client name to send a private message.\n");

	/* Handle SIGINT signal. */
	struct sigaction sact;
	sact.sa_handler = sig_quit_program;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
	if (sigaction(SIGINT, &sact, NULL) == -1)
	{
		perror("Error creating signal handler: ");
		return EXIT_FAILURE;
	}

	pthread_t tid_server;
	client_data_t cdata;
	strcpy(cdata.name, name);
	cdata.sfd = sfd;

	if (pthread_create(&tid_server, NULL, listen_from_server, (void*) &cdata)
			!= 0)
	{
		fprintf(stderr, "Error creating thread to listen to the server.\n");
		return EXIT_FAILURE;
	}

	pthread_t tid_user;

	if (pthread_create(&tid_user, NULL, prompt_user, (void*) &cdata) != 0)
	{
		fprintf(stderr, "Error creating thread to prompt the user.\n");
		return EXIT_FAILURE;
	}

	while (!g_quit)
	{
		/* TODO: Change this to nanoseconds. */
		sleep(1);
	}

	close(sfd);

	puts("Goodbye.");

	return EXIT_SUCCESS;
}

/*
 * @brief Returns NAME_OK if validations were successful. Otherwise, it will
 * return the corresponding enumerator indicating which error NAME has.
 *
 * @param[in] name
 */
Name_err_codes
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
 * @brief Listens to server's messages.
 * Every message received will get printed to client's console.
 * If we lose connection with the server G_QUIT gets set to 1 and the program
 * terminates.
 *
 * @return NULL
 */
static void*
listen_from_server(void *arg)
{
	client_data_t *cdata = (client_data_t*) arg;

	char msg[MSG_SIZE];
	int res_recv = 0;

	while (1)
	{
		memset(msg, 0, sizeof(msg));

		if ((res_recv = recv(cdata->sfd, msg, BUFF_SIZE, 0)) > 0)
		{
			printf("%s", msg);
			printf("> ");
			fflush(stdout);
		}
		else if (res_recv == 0)
		{
			printf("Lost connection with the server.\n");
			g_quit = 1;
			break;
		}
		else
		{
			perror("Error receiving message from server: ");
			g_quit = 1;
			break;
		}
	}

	return NULL;
}

/*
 * @brief Prompt the user to write a message.
 * The message will get formatted to something like this:
 * "username: message"
 *
 * @return NULL
 */
static void*
prompt_user(void *arg)
{
	client_data_t *cdata = (client_data_t*) arg;

	char msg[MSG_SIZE];
	char buff[BUFF_SIZE];

	while (1)
	{
		memset(msg, 0, sizeof(msg));
		memset(buff, 0, sizeof(buff));

		printf("> ");
		fflush(stdout);

		if (fgets(msg, MSG_SIZE, stdin) == NULL)
		{
			perror("Error getting user input: ");
			continue;
		}

		msg[strcspn(msg, "\n")] = '\0';

		if (strcmp(msg, QUIT_CMD) == 0)
		{
			g_quit = 1;
			break;
		}
		else if (strcmp(msg, LIST_CMD) == 0)
		{
			if (send(cdata->sfd, msg, strlen(msg),
				 0) == -1)
			{
				perror("Error sending list message to"
				       "the server: ");
			}
		}
		else
		{
			snprintf(buff, sizeof(buff), "%s: %s\n",
				 cdata->name, msg);

			if (send(cdata->sfd, buff, strlen(buff), 0)
			    == -1)
			{
				perror("Error sending message to the "
				       "server: ");
			}
		}
	}

	return NULL;
}

static void
sig_quit_program(int signo)
{
	g_quit = 1;
	printf("Catched signal %d\n.", signo);
}
