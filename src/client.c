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
#include "user.h"

#define SERVER_IP "::1"
#define PORTNO 6969
#define QUIT_CMD "!quit"

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
	User_t *user;
	int sfd; /* Server to which the client is connected. */
} Client_data_t;

static Connection_status_codes_wrapper connect_to_server(struct sockaddr_in6 *,
							 size_t, int *);
static Register_user_status_codes_wrapper register_user(User_t *, const int);
static void *listen_from_server(void *);
static void *prompt_user(void *);
static void sig_quit_program(int);
static void print_welcome(void);
static int setup_signals(void);
static void cleanup(int);

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
			fprintf(stderr, "Your name has to be at least %d characters long.\n", MIN_NAME_LEN);
			continue;
			break;
		case NAME_ERR_MAX_LEN:
			fprintf(stderr, "Your name can't exceed %d characters long.\n", NAME_SIZE);
			continue;
			break;
		case NAME_ERR_WSPACE:
			fprintf(stderr, "Your name can't contain a whitespace in between.\n");
			continue;
			break;
		case NAME_SYSTEM_ERR:
			fprintf(stderr, "Error reading user name%s\n.", strerror(necw.system_errno));
			continue;
			break;
		case NAME_OK:
			break;
		}

		Connection_status_codes_wrapper cecw = connect_to_server(&sa6, sizeof(sa6), &sfd);

		switch (cecw.conn_err) {
		case CONN_SOCKET_ERR:
			fprintf(stderr, "Error creating socket%s\n", strerror(cecw.system_errno));
			continue;
			break;
		case CONN_PTON_ERR:
			fprintf(stderr, "Error calling inet_pton()%s\n", strerror(cecw.system_errno));
			continue;
			break;
		case CONN_CONNECT_ERR:
			fprintf(stderr, "Error connecting to server%s\n", strerror(cecw.system_errno));
			continue;
			break;
		case CONN_RECV_ERR:
			fprintf(stderr, "Error receiving data from server%s\n", strerror(cecw.system_errno));
			continue;
			break;
		case CONN_SV_FULL_ERR:
			fprintf(stderr, "The server is full. Please try again.\n");
			continue;
			break;
		case CONN_OK:
			break;
		}

		strcpy(user.name, name);
                Register_user_status_codes_wrapper ruscw = register_user(&user, sfd);

		switch (ruscw.reg_err) {
		case REGUSR_SEND_ERR:
			fprintf(stderr, "Error sending name to server%s\n", strerror(ruscw.system_errno));
			continue;
			break;
		case REGUSR_RECV_ERR:
			fprintf(stderr, "Error receiving data to server%s\n", strerror(ruscw.system_errno));
			continue;
			break;
		case REGUSR_NAME_EXISTS_ERR:
			fprintf(stderr, "Your name already exists in the server. Please, try again.\n");
			continue;
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
	Client_data_t cdata;
	cdata.user = &user;
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

	puts("Goodbye.");
	cleanup(sfd);

	return EXIT_SUCCESS;
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
	Client_data_t *cdata = (Client_data_t *) arg;

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
 * @brief Gets the user message, checks for overflow and trims it.
 *
 * @param[in out] msg Sanitized message.
 * @param[in] size Size of the message.
 *
 * @return 0 ok; -1 error.
 */
int
get_message(char *msg, size_t size)
{
	memset(msg, 0, size);

	printf("> ");
	fflush(stdout);

	if (fgets(msg, MSG_SIZE - 1, stdin) == NULL)
		return -1;

	if (msg[strlen(msg) - 1] != '\n') /* Message too long; avoid overflow. */
		flush_endl();

	trim(msg);
	msg[strcspn(msg, "\n")] = '\0';

	return 0;
}

/*
 * @brief Prompt the user to write a message.
 * The message will get formatted to something like this: "username: message"
 *
 * @return NULL
 */
static void *
prompt_user(void *arg)
{
	Client_data_t *cdata = (Client_data_t *) arg;

	char msg[MSG_SIZE];

	while (1) {
		if (get_message(msg, sizeof(msg)) == -1) {
			perror("Error getting user message: ");
			continue;
		}

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
 *
 * @param[in] signo We don't do anything with it at the moment.
 */
static void
sig_quit_program(int signo)
{
	g_quit = 1;
	printf("Catched signal %d\n.", signo);
}

/*
 * @brief Prepare the connection and connect to the server identified by SFD.
 *
 * @param[in out] sa6 Server's socket data to be filled.
 * @param[in] sa6_size sizeof(sa6)
 * @param[in out] sfd Server's file descriptor.
 *
 * @return The corresponding enumerator indicating success or error.
 */
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
 *
 * @param[in] user
 * @param[in] sfd Server's file descriptor.
 *
 * @return The corresponding enumerator indicating success or error.
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

/*
 * @brief Prints welcome message and instructions.
 */
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

static void
cleanup(int sfd)
{
	close(sfd);
}
