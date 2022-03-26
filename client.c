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
#include <time.h>
#include <errno.h>
#include "common.h"
#include "utils.h"

#define SERVER_IP "::1"
#define PORTNO 6969
#define QUIT_CMD "!quit"

/* User-defined types. */
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

typedef enum {
	CONN_SOCKET_ERR,
	CONN_PTON_ERR,
	CONN_CONNECT_ERR,
	CONN_RECV_ERR,
	CONN_SV_FULL_ERR,
	CONN_OK
} Connection_status_codes;

typedef struct {
	Connection_status_codes conn_err;
	int system_errno;
} Connection_status_codes_wrapper;

typedef enum {
	REGUSR_SEND_ERR,
	REGUSR_RECV_ERR,
	REGUSR_NAME_EXISTS_ERR,
	REGUSR_OK
} Register_user_status_codes;

typedef struct {
	Register_user_status_codes reg_err;
	int system_errno;
} Register_user_status_codes_wrapper;

typedef struct {
	char name[NAME_SIZE];
} User_t;

typedef struct {
	char name[NAME_SIZE];
	int sfd; /* Server to which the client is connected. */
} client_data_t;

/* Functions. */
static Name_status_codes validate_name(const char *);
static Name_status_codes_wrapper get_name(char *);
static Connection_status_codes_wrapper connect_to_server(struct sockaddr_in6 *,
                                                     size_t, int *);
static Register_user_status_codes_wrapper register_user(User_t *, const int);
static void *listen_from_server(void *);
static void *prompt_user(void *);
static void sig_quit_program(int);
static void print_welcome(void);
static int setup_signals(void);

/* Globals. */
volatile sig_atomic_t g_quit = 0;

int
main(void)
{
	if (system("clear") == -1) {
		perror("Couldn't execute clear: ");
	}

	int sfd = 0;
	struct sockaddr_in6 sa6;
	User_t user;

	int username_ok = 0;
	do {
		puts("Please type your name: ");
		char name[NAME_SIZE] = "";
		Name_status_codes_wrapper necw = get_name(name);

		switch (necw.name_err) {
		case NAME_ERR_MIN_LEN:
			fprintf(stderr, "Your name has to be at least %d characters long.", MIN_NAME_LEN);
			break;
		case NAME_ERR_MAX_LEN:
			fprintf(stderr, "Your name can't exceed %d characters long.", NAME_SIZE);
			break;
		case NAME_ERR_WSPACE:
			fprintf(stderr, "Your name can't contain a whitespace in between.");
			break;
		case NAME_SYSTEM_ERR:
			fprintf(stderr, "Error reading user name: %s\n.", strerror(necw.system_errno));
			break;
		case NAME_OK:
			break;
		}

		Connection_status_codes_wrapper cecw = connect_to_server(&sa6, sizeof(sa6), &sfd);

		switch (cecw.conn_err) {
		case CONN_SOCKET_ERR:
			fprintf(stderr, "Error creating socket: %s\n", strerror(cecw.system_errno));
			break;
		case CONN_PTON_ERR:
			fprintf(stderr, "Error calling inet_pton(): %s\n", strerror(cecw.system_errno));
			break;
		case CONN_CONNECT_ERR:
			fprintf(stderr, "Error connecting to server: %s\n", strerror(cecw.system_errno));
			break;
		case CONN_RECV_ERR:
			fprintf(stderr, "Error receiving data from server: %s\n", strerror(cecw.system_errno));
			break;
		case CONN_SV_FULL_ERR:
			fprintf(stderr, "The server is full. Please try again.\n");
			break;
		case CONN_OK:
			break;
		}

		strcpy(user.name, name);

		Register_user_status_codes_wrapper ruscw = register_user(&user, sfd);

		switch (ruscw.reg_err) {
		case REGUSR_SEND_ERR:
			fprintf(stderr, "Error sending name to server: %s\n", strerror(ruscw.system_errno));
			break;
		case REGUSR_RECV_ERR:
			fprintf(stderr, "Error receiving data to server: %s\n", strerror(ruscw.system_errno));
			break;
		case REGUSR_NAME_EXISTS_ERR:
			fprintf(stderr, "Your name already exists in the server. Please, try again.");
			break;
		case REGUSR_OK:
			username_ok = 1;
			break;
		}

	} while (!username_ok);

	if (setup_signals() == -1) {
		perror("Error setting up signals: ");
		exit(EXIT_FAILURE);
	}

	print_welcome();

	pthread_t tid_server;
	client_data_t cdata;
	strcpy(cdata.name, user.name);
	cdata.sfd = sfd;

	if (pthread_create(&tid_server, NULL, listen_from_server, (void*) &cdata) != 0) {
		fprintf(stderr, "Error creating thread to listen to the server.\n");
		exit(EXIT_FAILURE);
	}

	pthread_t tid_user;

	if (pthread_create(&tid_user, NULL, prompt_user, (void*) &cdata) != 0) {
		fprintf(stderr, "Error creating thread to prompt the user.\n");
		exit(EXIT_FAILURE);
	}

	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = 500000000L;

	while (!g_quit) {
		struct timespec t2;
		if (nanosleep(&t , &t2) < 0)
			perror("nanosleep system call failed: ");
	}

	close(sfd);

	puts("Goodbye.");

	return EXIT_SUCCESS;
}


/* @TODO: create new file user.h */
/*
 * @brief Returns NAME_OK if validations were successful. Otherwise, it will
 * return the corresponding enumerator indicating which error NAME has.
 *
 * @param[in] name
 */
static Name_status_codes
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
static void *
listen_from_server(void *arg)
{
	client_data_t *cdata = (client_data_t *) arg;

	char msg[BUFF_SIZE];
	int res_recv = 0;

	while (1) {
		memset(msg, 0, sizeof(msg));

		if ((res_recv = recv(cdata->sfd, msg, BUFF_SIZE, 0)) > 0) {
			printf("%s", msg);
			printf("> ");
			fflush(stdout);
		} else if (res_recv == 0) {
			printf("Lost connection with the server.\n");
			g_quit = 1;
			break;
		} else {
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
	client_data_t *cdata = (client_data_t *) arg;

	char msg[MSG_SIZE];

	while (1) {
		memset(msg, 0, sizeof(msg));

		printf("> ");
		fflush(stdout);

		if (fgets(msg, MSG_SIZE - 1, stdin) == NULL) {
			perror("Error getting user input: ");
			continue;
		}

		if (msg[strlen(msg) - 1] != '\n') { /* Message too long. */
			flush_endl();
		}

		trim(msg);
		msg[strcspn(msg, "\n")] = '\0';

		if (strcmp(msg, QUIT_CMD) == 0) {
			g_quit = 1;
			break;
		}

		if (send(cdata->sfd, msg, strlen(msg), 0) == -1)
			perror("Error sending list message to the server: ");
	}

	return NULL;
}

/*
 * @brief Sets G_QUIT to 1 and thus the program terminates if someone
 * presses Ctrl+C.
 */
static void
sig_quit_program(int signo)
{
	g_quit = 1;
	printf("Catched signal %d\n.", signo);
}

/* @TODO: create new file user.h */
static Name_status_codes_wrapper
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

	name[strcspn(name, "\n")] = '\0'; /* Get rid of the newline. */

	Name_status_codes ne = validate_name(name);
	necw.name_err = ne;
	necw.system_errno = 0;

	return necw;
}

static Connection_status_codes_wrapper
connect_to_server(struct sockaddr_in6 *sa6, size_t sa6_size, int *sfd)
{
	Connection_status_codes_wrapper cecw;
	*sfd = 0;

	if ((*sfd = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		cecw.conn_err = CONN_SOCKET_ERR;
		cecw.system_errno = errno;
		return cecw;
	}

	memset(sa6, 0, sa6_size);
	sa6->sin6_family = AF_INET6;
	sa6->sin6_port = htons(PORTNO);
	sa6->sin6_addr = in6addr_any;

	if ((inet_pton(AF_INET6, SERVER_IP, &(sa6->sin6_addr))) <= 0) {
		cecw.conn_err = CONN_PTON_ERR;
		cecw.system_errno = errno;
		return cecw;
	}

	if ((connect(*sfd, (struct sockaddr*) sa6, sa6_size)) == -1) {
		cecw.conn_err = CONN_CONNECT_ERR;
		cecw.system_errno = errno;
		return cecw;
	}

	char buff[BUFF_SIZE] = "";

	/* Know if server rejected our connection. */
	if ((recv(*sfd, &buff, sizeof(buff), 0)) == -1) {
		cecw.conn_err = CONN_RECV_ERR;
		cecw.system_errno = errno;
		return cecw;
	}

	if (strstr(buff, ERR_STATUS) != NULL) {
		cecw.conn_err = CONN_SV_FULL_ERR;
		cecw.system_errno = 0;
		return cecw;
	}

	cecw.conn_err = CONN_OK;
	cecw.system_errno = 0;

	return cecw;
}

/*
 * @brief Send the name to the server so it can validate that no other
 * client exists with the same name.
 */
static Register_user_status_codes_wrapper
register_user(User_t *user, const int sfd)
{
	Register_user_status_codes_wrapper ruscw;

	if ((send(sfd, user->name, strlen(user->name), 0)) == -1) {
		ruscw.reg_err = REGUSR_SEND_ERR;
		ruscw.system_errno = errno;
		return ruscw;
	}

	char buff[BUFF_SIZE] = "";
	memset(buff, 0, sizeof(buff));

	if ((recv(sfd, &buff, sizeof(buff), 0)) == -1) {
		ruscw.reg_err = REGUSR_RECV_ERR;
		ruscw.system_errno = errno;
		return ruscw;
	}

	if (strstr(buff, ERR_STATUS) != NULL) {
		ruscw.reg_err = REGUSR_NAME_EXISTS_ERR;
		ruscw.system_errno = 0;
		return ruscw;
	}

	ruscw.reg_err = REGUSR_OK;
	ruscw.system_errno = 0;

	return ruscw;
}

static void
print_welcome(void)
{
	puts("\n****************************");
	puts("****************************");
	puts("* Welcome to the Chat Room *");
	puts("****************************");
	puts("****************************");
	puts("\nType !quit to leave the chatroom.");
	puts("Type !list to show all clients connected to the chatroom.");
	puts("Type !whisp and the client name to send a private message.\n");
}

/*
 * @brief We're only handling the SIGINT signal at the moment.
 *
 * @return 0 ok; -1 error.
 */
static int
setup_signals(void)
{
	struct sigaction sact;
	sact.sa_handler = sig_quit_program;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;

	return sigaction(SIGINT, &sact, NULL);
}
