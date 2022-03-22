#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "common.h"

/* Constants. */
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

/* User-defined types. */
typedef struct {
	char name[NAME_SIZE];
	unsigned int id;
	int fd;
	char colour[COLOUR_SIZE];
} client_t;

typedef struct {
	char colour[COLOUR_SIZE];
	int used;
} chat_colours_t;

typedef enum {
	SRC_SERVER,
	SRC_CLIENT
} msg_src;

/* Mutexes. */
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Globals. */
static _Atomic unsigned int g_clients_connected = 0;
static client_t *g_clients[MAX_CLIENTS];
static _Atomic unsigned int g_client_id = 1;
static FILE *g_log_file;
static int g_quit = 0;
static chat_colours_t g_colours_used[TOTAL_COLOURS] =
{
	{RED, 0},
	{GREEN, 0},
	{YELLOW, 0},
	{BLUE, 0},
	{MAGENTA, 0},
	{CYAN, 0},
	{WHITE, 0}
};

/* Functions. */
static client_t *create_client(char *, unsigned int, int);
static void add_client(client_t *);
static void remove_client(const unsigned int);
static void *manage_client(void *);
static int client_exists(const char *);
static void broadcast_message(const char*, client_t *, const msg_src);
static void send_whisper(char *, client_t *);
static void send_list_clients(client_t *);
static void log_message(const char *);
static void sig_quit_program(int);

int
main(void)
{
	int fd = 0;

	if ((fd = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		perror("Error creating the socket(): ");
		return EXIT_FAILURE;
	}

	struct sockaddr_in6 sa6; /* Server address IPv6. */
	memset(&sa6, 0, sizeof(sa6));
	sa6.sin6_family = AF_INET6;
	sa6.sin6_port = htons(PORTNO);
	sa6.sin6_addr = in6addr_any;

	if ((bind(fd, (struct sockaddr*) &sa6, sizeof(sa6))) == -1) {
		perror("Error binding: ");
		return EXIT_FAILURE;
	}

	if ((listen(fd, MAX_CLIENTS)) == -1) {
		perror("Error listening: ");
		return EXIT_FAILURE;
	}

	/* Handle SIGINT. */
	struct sigaction sact;
	sact.sa_handler = sig_quit_program;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;

	if (sigaction(SIGINT, &sact, NULL) == -1) {
		perror("Error creating signal handler: ");
		return EXIT_FAILURE;
	}

	/* Open the file to save a log of public messages. */
	g_log_file = fopen(LOG_FILE_NAME, "a");

	/* Server ready and running. */
	puts("Server started.");

	while (!g_quit) {
		pthread_t tid;
		struct sockaddr_in6 ca6; /* Client address. */
		socklen_t ca6_len = sizeof(ca6);
		int cfd = accept(fd, (struct sockaddr*) &ca6, (socklen_t*) &ca6_len);

		char buff[BUFF_SIZE];

		/* Check if server is full. */
		if ((g_clients_connected + 1) > MAX_CLIENTS) {
			strcpy(buff, ERR_STATUS);

			if ((send(cfd, buff, strlen(buff), 0)) == -1)
				perror("Error sending msg server full: ");

			close(cfd);
			continue;
		}

		/* Send OK status to client. */
		strcpy(buff, OK_STATUS);

		if ((send(cfd, buff, strlen(buff), 0)) == -1)
			perror("Error sending ok msg: ");

		char name[NAME_SIZE];
		memset(name, '\0', sizeof(name));

		/* Get client's name and check if it exists. */
		if (recv(cfd, &name, sizeof(name), 0) == -1) {
			perror("Error recv'ing client name: ");
			close(cfd);
			continue;
		}

		if (client_exists(name)) {
			strcpy(buff, ERR_STATUS);

			if ((send(cfd, buff, strlen(buff), 0)) == -1)
				perror("Error sending msg client exists: ");

			close(cfd);
			continue;
		}

		/* Send OK status to client. */
		strcpy(buff, OK_STATUS);

		if ((send(cfd, buff, strlen(buff), 0)) == -1)
			perror("Error sending ok msg: ");

		client_t *c = create_client(name, g_client_id, cfd);
		add_client(c);

		++g_clients_connected;
		++g_client_id;

		/* Notify everyone that someone has connected. */
		snprintf(buff, sizeof(buff), "%s has connected.", c->name);
		printf("%s\n", buff);
		broadcast_message(buff, c, SRC_SERVER);
		log_message(buff);

		pthread_create(&tid, NULL, manage_client, (void *) c);
	}

	/* Cleanup. */
	close(fd);
	fclose(g_log_file);

	return EXIT_SUCCESS;
}

/*
 * @brief Find the first NULL slot in the array of current clients
 * connected and assign it to the new client.
 *
 * @param[in] c New client connected.
 */
static void
add_client(client_t *c)
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
static client_t *
create_client(char *name, unsigned int id, int fd)
{
	client_t *c = (client_t *) malloc(sizeof(client_t));
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
	client_t *client = (client_t *) c;

	char msg[BUFF_SIZE];
	int response = 0;

	while (1) {
		memset(msg, '\0', sizeof(msg));

		if ((response = recv(client->fd, msg, sizeof(msg), 0)) > 0) {
			if (strcmp(msg, LIST_CMD) == 0) {
				send_list_clients(client);
			} else if (strstr(msg, WHISP_CMD) != NULL) {
				send_whisper(msg, client);
			} else {
				broadcast_message(msg, client, SRC_CLIENT);
				log_message(msg);
			}
		} else if (response == 0) {
			snprintf(msg, sizeof(msg), "%s has quit.", client->name);
			broadcast_message(msg, client, SRC_SERVER);
			log_message(msg);
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
broadcast_message(const char *msg, client_t *sender, const msg_src ms)
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
send_whisper(char *msg, client_t *sender)
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
send_list_clients(client_t *client)
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
 *
 * @note The file has to be opened already.
 */
static void
log_message(const char *msg)
{
	(void)fprintf(g_log_file, "%s\n", msg);
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
