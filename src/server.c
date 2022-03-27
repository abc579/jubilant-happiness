#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "common.h"

#define PORTNO 6969
#define MAX_CLIENTS 7
#define LOG_FILE_NAME "log.txt"

#define COLOUR_SIZE 20
#define TOTAL_COLOURS 7
#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define YELLOW "\x1B[33m"
#define BLUE "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN "\x1B[36m"
#define WHITE "\x1B[37m"
#define RESET "\x1B[0m"

typedef struct {
	char name[NAME_SIZE];
	unsigned int id;
	int fd;
	char colour[COLOUR_SIZE];
} Client_t;

typedef struct {
	char colour[COLOUR_SIZE];
	int used;
} Chat_colours_t;

typedef enum {
	SRC_SERVER,
	SRC_CLIENT
} Message_source;

typedef enum {
	NEW_CONN_SV_FULL_ERR,
	NEW_CONN_SYSTEM_ERR,
	NEW_CONN_OK
} New_connection_status_codes;

typedef struct {
	New_connection_status_codes nconn_err;
	int system_err;
} New_connection_status_codes_wrapper;

typedef enum {
	CL_NAME_EXISTS_ERR,
	CL_NAME_SYSTEM_ERR,
	CL_NAME_OK
} Client_name_status_codes;

typedef struct {
	Client_name_status_codes cname_err;
	int system_err;
} Client_name_status_codes_wrapper;

pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

static _Atomic unsigned int g_clients_connected = 0;
static Client_t *g_clients[MAX_CLIENTS];
static _Atomic unsigned int g_client_id = 1;
static FILE *g_log_file;
static int g_quit = 0;
static Chat_colours_t g_colours_used[TOTAL_COLOURS] =
{
	{RED, 0},
	{GREEN, 0},
	{YELLOW, 0},
	{BLUE, 0},
	{MAGENTA, 0},
	{CYAN, 0},
	{WHITE, 0}
};

static Client_t *create_client(char *, unsigned int, int);
static void add_client(Client_t *);
static void remove_client(const unsigned int);
static void *manage_client(void *);
static int client_exists(const char *);
static void broadcast_message(const char*, Client_t *, const Message_source);
static void send_whisper(char *, Client_t *);
static void send_client_list(Client_t *);
static void log_message(const char *, Client_t *, const Message_source);
static void sig_quit_program(int);
static int setup_signals(void);
static int prepare_server(struct sockaddr_in6 *, size_t, int *);
static New_connection_status_codes_wrapper process_new_connection(const int);
static Client_name_status_codes_wrapper process_client_name(const int, char *,
                                                            const size_t);
static void cleanup(int, FILE *);

int
main(void)
{
	int fd = 0;
	struct sockaddr_in6 sa6;

	if (prepare_server(&sa6, sizeof(sa6), &fd) == -1) {
		perror("Error preparing the server to listen for connections: ");
		exit(EXIT_FAILURE);
	}

	if (setup_signals() == -1) {
		perror("Error setting up signals: ");
		exit(EXIT_FAILURE);
	}

	/* Open the file to save a log of public messages. */
	g_log_file = fopen(LOG_FILE_NAME, "a");

	puts("Server started.");

	while (!g_quit) {
		pthread_t tid;
		struct sockaddr_in6 ca6; /* Client address IPv6. */
		socklen_t ca6_len = sizeof(ca6);

		int cfd = accept(fd, (struct sockaddr*) &ca6, (socklen_t*) &ca6_len);
		New_connection_status_codes_wrapper ncscw = process_new_connection(cfd);

		switch (ncscw.nconn_err) {
		case NEW_CONN_SYSTEM_ERR:
			perror("Error processing new connection: ");
			close(cfd);
			continue;
		case NEW_CONN_SV_FULL_ERR:
			close(cfd);
			continue;
		case NEW_CONN_OK:
			break;
		}

		char name[NAME_SIZE];
		Client_name_status_codes_wrapper cnscw = process_client_name(cfd, name, sizeof(name));

		switch (cnscw.cname_err) {
		case CL_NAME_SYSTEM_ERR:
			perror("Error processing client's name: ");
			close(cfd);
			continue;
		case CL_NAME_EXISTS_ERR:
			close(cfd);
			continue;
		case CL_NAME_OK:
			break;
		}

		Client_t *c = create_client(name, g_client_id, cfd);
		add_client(c);

		++g_clients_connected;
		++g_client_id;

		/* Notify everyone that someone has connected. */
		char buff[BUFF_SIZE];
		snprintf(buff, sizeof(buff), "%s has connected.", c->name);
		printf("%s\n", buff);
		broadcast_message(buff, c, SRC_SERVER);
		log_message(buff, c, SRC_SERVER);

		pthread_create(&tid, NULL, manage_client, (void *) c);
	}

	cleanup(fd, g_log_file);

	return EXIT_SUCCESS;
}

/*
 * @brief Find the first NULL slot in the array of current clients
 * connected and assign it to the new client.
 *
 * @param[in] c New client connected.
 */
static void
add_client(Client_t *c)
{
	pthread_mutex_lock(&client_mutex);

	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (g_clients[i] == NULL) {
			g_clients[i] = c;
			break;
		}

	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief Find the client that has the id passed as parameter.
 * Then, close its fd and free everything.
 *
 * @param[in] id Id of the client to remove.
 */
static void
remove_client(const unsigned int id)
{
	pthread_mutex_lock(&client_mutex);

	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (g_clients[i] && g_clients[i]->id == id) {
			close(g_clients[i]->fd);

			/* Release colour. */
			for (int j = 0; j < TOTAL_COLOURS; ++j)
				if (strcmp(g_clients[i]->colour, g_colours_used[j].colour) == 0)
					g_colours_used[i].used = 0;

			free(g_clients[i]);
			g_clients[i] = NULL;
			break;
		}

	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief malloc a new client with the given parameters and return it.
 *
 * @param[in] name Client name in the chatroom.
 * @param[in] id Client id.
 * @param[in] fd Client file descriptor.
 *
 * @return New allocated client.
 */
static Client_t *
create_client(char *name, unsigned int id, int fd)
{
	Client_t *c = (Client_t *) malloc(sizeof(Client_t));
	strcpy(c->name, name);
	c->id = id;
	c->fd = fd;
	strcpy(c->colour, RESET);

	/* Assign a colour that is not yet used. */
	for (int i = 0; i < TOTAL_COLOURS; ++i)
		if (!g_colours_used[i].used) {
			strcpy(c->colour, g_colours_used[i].colour);
			g_colours_used[i].used = 1;
			break;
		}

	return c;
}

/*
 * @brief Each client connected will be managed by this function. It
 * basically handles incoming messages from the client.
 *
 * @param[in] c New client connected to the chatroom.
 */
static void *
manage_client(void *c)
{
	Client_t *client = (Client_t *) c;

	char msg[BUFF_SIZE];
	int response = 0;

	while (1) {
		memset(msg, '\0', sizeof(msg));

		if ((response = recv(client->fd, msg, sizeof(msg), 0)) > 0) {
			if (strcmp(msg, LIST_CMD) == 0) {
				send_client_list(client);
			} else if (strstr(msg, WHISP_CMD) != NULL) {
				send_whisper(msg, client);
			} else {
				broadcast_message(msg, client, SRC_CLIENT);
				log_message(msg, client, SRC_CLIENT);
			}
		} else if (response == 0) {
			snprintf(msg, sizeof(msg), "%s has quit.", client->name);
			broadcast_message(msg, client, SRC_SERVER);
			log_message(msg, client, SRC_SERVER);
			printf("%s\n", msg);
			break;
		} else {
			perror("Error recv'ing data from client: ");
			break;
		}

	}

	remove_client(client->id);
	--g_clients_connected;

	return NULL;
}

/*
 * @brief Checks if NAME is already in G_CLIENTS.
 *
 * @param[in] name Client's name.
 *
 * @return 1 if the NAME already exists; 0 otherwise.
 */
static int
client_exists(const char *name)
{
	pthread_mutex_lock(&client_mutex);

	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (g_clients[i] && strcmp(g_clients[i]->name, name) == 0) {
			pthread_mutex_unlock(&client_mutex);
			return 1;
		}

	pthread_mutex_unlock(&client_mutex);

	return 0;
}

/*
 * @brief Broadcasts message to everyone connected to the chat room
 * except the sender.
 *
 * @param[in] msg
 * @param[in] sender
 * @param[in] cmsg Client message: 1 or 0.
 */
static void
broadcast_message(const char *msg, Client_t *sender, const Message_source ms)
{
	pthread_mutex_lock(&client_mutex);

	char buff[BUFF_SIZE];

	if (ms == SRC_SERVER)
		snprintf(buff, sizeof(buff), "%s\n", msg);
	else
		snprintf(buff, sizeof(buff), "%s%s%s: %s\n",
			 sender->colour,
			 sender->name,
			 RESET,
			 msg);

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (g_clients[i] && g_clients[i]->fd != sender->fd) {
			if ((send(g_clients[i]->fd, buff, strlen(buff), 0)) == -1)
				perror("Error broadcasting msg: ");
		}
	}

	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief Parse MSG to extract the client that has to receive the message
 * and the actual message.
 *
 * @param[in] msg format: "Clientname: !whisp receivername message"
 * @param[in] sender
 *
 */
static void
send_whisper(char *msg, Client_t *sender)
{
	/* Copy MSG To TMP because strtok modifies it. */
	char tmp[MSG_SIZE] = "";

	for (size_t i = 0; i < strlen(msg); ++i)
		tmp[i] = msg[i];

	char name[NAME_SIZE] = "";
	char contents[MSG_SIZE] = "";

	int i = 0;
	const int name_pos = 1;
	char *tok = strtok(tmp, " ");

	while (tok) {
		if (i == name_pos)
			strncpy(name, tok, NAME_SIZE - 1);
		else if (i > name_pos) {
			strcat(contents, tok);
			strcat(contents, " ");
		}

		tok = strtok(NULL, " ");
		++i;
	}

	/* Find the client fd and send the message. */
	pthread_mutex_lock(&client_mutex);

	int found = 0;
	char buff[BUFF_SIZE] = "Client not found.\n";
	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (g_clients[i] && strcmp(g_clients[i]->name, name) == 0) {
			snprintf(buff, sizeof(buff), "%s\x1B[3m%s\x1B%s: %s\n", /* Print name with italic style. */
				 sender->colour,
				 sender->name,
				 RESET,
				 contents);

			if (send(g_clients[i]->fd, buff, strlen(buff), 0) == -1)
				perror("Error sending whisper: ");

			found = 1;
			break;
		}

	if (!found)
		if (send(sender->fd, buff, strlen(buff), 0) == -1)
			perror("Error sending whisper, client not found: ");


	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief Sends a message with the client list to CLIENT.
 *
 * @param[in] client Message receiver.
 */
static void
send_client_list(Client_t *client)
{
	char msg[BUFF_SIZE] = "\n";

	for (int i = 0; i < MAX_CLIENTS && strlen(msg) < MSG_SIZE; ++i)
		if (g_clients[i]) {
			strcat(msg, g_clients[i]->name);
			strcat(msg, "\n");
		}

	if (send(client->fd, msg, strlen(msg), 0) == -1)
		perror("Error sending list of clients: ");
}

/*
 * @brief Append MSG to the log file.
 *
 * @param[in] MSG to be appended.
 * @param[in] SENDER of the message.
 * @param[in] MS source of the message (server/client).
 *
 * @note The file has to be opened already.
 */
static void
log_message(const char *msg, Client_t *sender, const Message_source ms)
{
	time_t t;
	time(&t);
	struct tm *date = gmtime(&t);
	char datestr[50];
	snprintf(datestr, sizeof(datestr), "[%d-%d-%d %d:%d:%d]",
		 date->tm_year + 1900,
		 date->tm_mon + 1,
		 date->tm_mday,
		 date->tm_hour,
		 date->tm_min,
		 date->tm_sec);

	if (ms == SRC_SERVER)
		(void)fprintf(g_log_file, "%s %s\n", datestr, msg);
	else
		(void)fprintf(g_log_file, "%s %s: %s\n", datestr, sender->name, msg);

	fflush(g_log_file);
}

/*
 * @brief Sets G_QUIT to 1 and thus the program terminates if someone
 * presses Ctrl+C.
 *
 * @param[in] signo Signal number.
 */
static void
sig_quit_program(int signo)
{
	g_quit = 1;
	printf("Catched signal %d\n.", signo);
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

/*
 * @brief Prepares the server to listen for client connections by
 * creating the socket, binding, etc.
 *
 * @param[in out] sa6 Server address IPv6.
 * @param[in] sa6_size sizeof(sa6)
 * @param[in out] fd Server's file descriptor.
 *
 * @return 0 ok; -1 otherwise.
 */
static int
prepare_server(struct sockaddr_in6 *sa6, size_t sa6_size, int *fd)
{
	if ((*fd = socket(AF_INET6, SOCK_STREAM, 0)) == -1)
		return -1;

	memset(sa6, 0, sa6_size);
	sa6->sin6_family = AF_INET6;
	sa6->sin6_port = htons(PORTNO);
	sa6->sin6_addr = in6addr_any;

	if ((bind(*fd, (struct sockaddr*) sa6, sa6_size)) == -1)
		return -1;

	if ((listen(*fd, MAX_CLIENTS)) == -1)
		return -1;

	return 0;
}

/*
 * @brief Check if the server is full. If it is, send an ERR_STATUS to the
 * client and close its fd.
 *
 * If it's not, send OK_STATUS to the client.
 *
 * @param[in] fd Client's file descriptor.
 *
 * @return A struct containing the corresponding status.
 */
static New_connection_status_codes_wrapper
process_new_connection(const int cfd)
{
	char buff[BUFF_SIZE];
	New_connection_status_codes_wrapper ncscw;

	if ((g_clients_connected + 1) > MAX_CLIENTS) {
		strcpy(buff, ERR_STATUS);

		if ((send(cfd, buff, strlen(buff), 0)) == -1) {
			ncscw.nconn_err = NEW_CONN_SYSTEM_ERR;
			ncscw.system_err = errno;
			return ncscw;
		} else {
			ncscw.nconn_err = NEW_CONN_SV_FULL_ERR;
			ncscw.system_err = 0;
			return ncscw;
		}
	}

	strcpy(buff, OK_STATUS);

	if ((send(cfd, buff, strlen(buff), 0)) == -1) {
		ncscw.nconn_err = NEW_CONN_SYSTEM_ERR;
		ncscw.system_err = errno;
		return ncscw;
	}

	ncscw.nconn_err = NEW_CONN_OK;
	ncscw.system_err = 0;

	return ncscw;
}

/*
 * @brief Get client's name. If it exists already in the server, then send a
 * message to the client saying that the server is full.
 * If it doesn't exist, send an OK status to the client.
 *
 * @param[in] cfd Client's file descriptor.
 * @param[in out] name Name of the client to be fetched.
 * @param[in] size sizeof(name).
 *
 * @return A struct containing the corresponding status.
 */
static Client_name_status_codes_wrapper
process_client_name(const int cfd, char *name, const size_t size)
{
	Client_name_status_codes_wrapper cnscw;

	memset(name, '\0', size);

	if (recv(cfd, name, size, 0) == -1) {
		cnscw.cname_err = CL_NAME_SYSTEM_ERR;
		cnscw.system_err = errno;
		return cnscw;
	}

	/* If the client name exists, send an ERR_STATUS message to the client. */
	char buff[BUFF_SIZE];
	if (client_exists(name)) {
		strcpy(buff, ERR_STATUS);

		if ((send(cfd, buff, strlen(buff), 0)) == -1) {
			cnscw.cname_err = CL_NAME_SYSTEM_ERR;
			cnscw.system_err = errno;
			return cnscw;
		}

		cnscw.cname_err = CL_NAME_EXISTS_ERR;
		cnscw.system_err = 0;
		return cnscw;
	}

	/* Send OK status to client. */
	strcpy(buff, OK_STATUS);

	if ((send(cfd, buff, strlen(buff), 0)) == -1) {
		cnscw.cname_err = CL_NAME_SYSTEM_ERR;
		cnscw.system_err = errno;
		return cnscw;
	}

	cnscw.cname_err = CL_NAME_OK;
	cnscw.system_err = 0;

	return cnscw;
}

static void
cleanup(int fd, FILE *file)
{
	close(fd);
	fclose(file);
}
