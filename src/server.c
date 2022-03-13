#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Constants. */
#define PORTNO 6969
#define MAX_CLIENTS 10

/* User-defined types. */
typedef struct {
	char *name;
	unsigned int id;
	int fd;
} client_t;

/* Mutexes. */
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Globals. */
static _Atomic unsigned int g_clients_connected = 0;
static client_t *g_clients[MAX_CLIENTS];
static _Atomic unsigned int g_client_id = 1;

/* Functions. */
client_t *create_client(char *, unsigned int, int);
void add_client(client_t *);
void remove_client(unsigned int);
void *manage_client(void *);

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
	sa6.sin6_port = PORTNO;
	sa6.sin6_addr = in6addr_any;

	if ((bind(fd, (struct sockaddr*) &sa6,
		  sizeof(sa6))) == -1) {
		perror("Error binding: ");
		return EXIT_FAILURE;
	}

	if ((listen(fd, MAX_CLIENTS)) == -1) {
		perror("Error listening: ");
		return EXIT_FAILURE;
	}

	/* Server ready and running. */
	puts("Server started.");

	/*
	 * Main loop: listen for new connections and store new clients
	 * in G_CLIENTS.
	 * No more than MAX_CLIENTS can be connected to the server.
	 */
	char *name = "test";

	while (1) {
		pthread_t tid;
		struct sockaddr_in6 ca6; /* Client address. */
		socklen_t ca6_sz = sizeof(ca6);
		int cfd = accept(fd, (struct sockaddr*)&ca6,
			(socklen_t *)&ca6_sz);

		if ((g_clients_connected + 1) > MAX_CLIENTS) {
			puts("Server is full.");
			close(cfd);
			continue;
		}

		puts("A new client has connected.");
		++g_clients_connected;

		client_t *c = create_client(name, g_client_id, cfd);
		add_client(c);
		++g_client_id;
		pthread_create(&tid, NULL, manage_client,
			       (void *) c);
	}

	/* Cleanup. */
	close(fd);

	return EXIT_SUCCESS;
}

/*
 * @brief Find the first NULL slot in the array of current clients
 * connected and assign it to the new client.
 *
 * @param[in] c New client connected.
 */
void
add_client(client_t *c)
{
	pthread_mutex_lock(&client_mutex);

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (g_clients[i] == NULL) {
			g_clients[i] = c;
			break;
		}
	}

	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief Find the client that has the id passed as parameter
 * in the array of clients connected.
 * Then, close its fd and free everything.
 *
 * @param[in] id Id of the client to remove.
 */
void
remove_client(unsigned int id)
{
	pthread_mutex_lock(&client_mutex);

	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (g_clients[i]->id == id) {
			close(g_clients[i]->fd);
			free(g_clients[i]->name);
			free(g_clients[i]);
			g_clients[i] = NULL;
			break;
		}
	}

	pthread_mutex_unlock(&client_mutex);
}

/*
 * @brief malloc a new client with the given parameters and return it.
 *
 * @param[in] name Client name in the chatroom.
 * @param[in] id Client id.
 * @param[in] fd Client file descriptor.
 */
client_t *
create_client(char *name, unsigned int id, int fd)
{
	client_t *c = (client_t *) malloc(sizeof(client_t));
	c->name = name;
	c->id = id;
	c->fd = fd;

	return c;
}

/*
 * @brief Each client connected will be managed by this function. It
 * basically handles incoming messages from the client and broadcasts
 * them to everyone.
 *
 * @param[in] c New client connected to the chatroom.
 */
void *
manage_client(void *c)
{
	client_t *client = (client_t *) c;

	char *msg;

	while (1) {

	}

	remove_client(client->id);
	--g_clients_connected;

	return NULL;
}
