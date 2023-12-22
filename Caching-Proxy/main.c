#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include "cache.h"
#include "logger.h"

#define FAIL_CODE (-1)
#define PORT 93
#define MAX_USERS_COUNT 10
#define BUFFER_SIZE 4096
#define MAX_HOST_INFO_LENGTH 50

typedef struct {
  int client_socket;
  char *request;
} Context;

char is_server_running = 1;
Cache *cache;
sem_t thread_semaphore;

void sigint_handler(int signo) {
  if (SIGINT == signo) {
    logg("Shutting down the server", INFO_COLOR);
    is_server_running = 0;
  }
}

int create_cache() {
  logg("Initializing cache", DEBUG_COLOR);
  cache = malloc(sizeof(Cache));
  if (NULL == cache) {
    return EXIT_FAILURE;
  }
  cache->next = NULL;
  return EXIT_SUCCESS;
}

void destroy_cache() {
  logg("Destroying cache", DEBUG_COLOR);
  pthread_mutex_lock(&cache_mutex);
  Cache *cur = cache;
  while (NULL != cur) {
    delete_cache_record(cur);
    Cache *next = cur->next;
    free(cur);
    cur = next;
  }
  pthread_mutex_unlock(&cache_mutex);
}

int create_server_socket() {
  struct sockaddr_in server_addr;
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (FAIL_CODE == server_socket) {
    logg("Error while creating server socket", CRITICAL_ERROR_COLOR);
    return FAIL_CODE;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_UNSPEC;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);
  logg("Server socket created", DEBUG_COLOR);

  int err = bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr));
  if (FAIL_CODE == err) {
    logg("Failed to bind server socket", CRITICAL_ERROR_COLOR);
    close(server_socket);
    return FAIL_CODE;
  }

  logg_int("Server socket bound to ", server_addr.sin_addr.s_addr, INFO_COLOR);

  err = listen(server_socket, MAX_USERS_COUNT);
  if (FAIL_CODE == err) {
    logg("Server socket failed to listen", CRITICAL_ERROR_COLOR);
    close(server_socket);
    return FAIL_CODE;
  }
  return server_socket;
}

int read_request(int client_socket, char *request) {
  ssize_t bytes_read = read(client_socket, request, BUFFER_SIZE);
  if (bytes_read < 0) {
    logg("Error while read request_hash", ERROR_COLOR);
    close(client_socket);
    return EXIT_FAILURE;
  }
  if (bytes_read == 0) {
    logg("Connection closed from client", INFO_COLOR);
    close(client_socket);
    return EXIT_FAILURE;
  }
  request[bytes_read] = '\0';
  logg_char("Received request_hash:\n", request, INFO_COLOR);
  return EXIT_SUCCESS;
}

int send_full_request_from_cache(char *request, int client_socket) {
  char *cache_record = calloc(CACHE_BUFFER_SIZE, sizeof(char));
  ssize_t length = find_in_cache(cache, request, cache_record);

  if (length != -1) {
    ssize_t send_bytes = write(client_socket, cache_record, length);
    if (FAIL_CODE == send_bytes) {
      logg("Error while sending cached data", ERROR_COLOR);
      close(client_socket);
      free(cache_record);
      return EXIT_FAILURE;
    }
    free(cache_record);
    logg_int("Send cached response to the client, length = ", send_bytes, INFO_COLOR);
    printf("\n");
    close(client_socket);
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}

int connect_to_host(char *host) {
  struct addrinfo hints, *res0;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo((char *) host, "http", &hints, &res0);
  if (status != 0) {
    logg("getaddrinfo error", ERROR_COLOR);
    freeaddrinfo(res0);
    return -1;
  }
  int dest_socket = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol);
  if (FAIL_CODE == dest_socket) {
    logg("Error while creating remote server socket", ERROR_COLOR);
    return FAIL_CODE;
  }

  int err = connect(dest_socket, res0->ai_addr, res0->ai_addrlen);
  if (FAIL_CODE == err) {
    logg("Error while connecting to remote server", ERROR_COLOR);
    close(dest_socket);
    freeaddrinfo(res0);
    return FAIL_CODE;
  }
  return dest_socket;
}

void *client_handler_routine(void *arg) {
  Context *context = (Context *) arg;
  int client_socket = context->client_socket;
  char *request_from_context = context->request;
  char request[BUFFER_SIZE];
  strcpy(request, request_from_context);

  Cache *record = malloc(sizeof(Cache));
  init_cache_record(record);
  add_request_hash(record, request_from_context);

  unsigned char host[MAX_HOST_INFO_LENGTH];
  const int skip_length = 6;
  const unsigned char *host_result = memccpy(host, strstr((char *) request, "Host:") + skip_length, '\r', sizeof(host));
  host[host_result - host - 1] = '\0';
  logg_char("Remote server host name: ", (char *) host, INFO_COLOR);

  int dest_socket = connect_to_host((char *) host);
  if (FAIL_CODE == dest_socket) {
    close(client_socket);
  }
  logg("Create new connection with remote server", DEBUG_COLOR);

  ssize_t bytes_sent = write(dest_socket, request, strlen(request));
  if (FAIL_CODE == bytes_sent) {
    logg("Error while sending request_hash to remote server", ERROR_COLOR);
    close(client_socket);
    close(dest_socket);
    return NULL;
  }
  logg_int("Send request_hash to remote server, length [B] = ", bytes_sent, INFO_COLOR);

  char *buffer = calloc(BUFFER_SIZE, sizeof(char));
  ssize_t bytes_read, all_bytes_read = 0;
  while ((bytes_read = read(dest_socket, buffer, BUFFER_SIZE)) > 0) {
    bytes_sent = write(client_socket, buffer, bytes_read);
    if (FAIL_CODE == bytes_sent) {
      logg("Error while sending data to client", ERROR_COLOR);
      close(client_socket);
      close(dest_socket);
      return NULL;
    } else {
      add_response_to_cache(record, buffer, all_bytes_read, bytes_read);
    }
    all_bytes_read += bytes_read;
  }
  add_size_of_response(record, all_bytes_read);
  push_cache_record(cache, record);

  close(client_socket);
  close(dest_socket);
  free(buffer);
  free(request_from_context);

  sem_post(&thread_semaphore);

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc ) {

  }
  int port = argv[0];
  logg("SERVER START", INFO_COLOR);
  signal(SIGINT, sigint_handler);

  sem_init(&thread_semaphore, 0, MAX_USERS_COUNT);

  int server_socket = create_server_socket();
  if (FAIL_CODE == server_socket) {
    logg("Error to create server socket", CRITICAL_ERROR_COLOR);
    exit(EXIT_FAILURE);
  }

  int err_code = create_cache();
  if (EXIT_FAILURE == err_code) {
    logg("Error to init cache", CRITICAL_ERROR_COLOR);
    destroy_cache();
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  logg_int("Server listening on port ", PORT, INFO_COLOR);

  while (is_server_running) {
    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_addr_size);
    if (FAIL_CODE == client_socket) {
      logg("Failed to accept", ERROR_COLOR);
      close(server_socket);
      destroy_cache();
      exit(EXIT_FAILURE);
    }
    char *buff = calloc(BUFFER_SIZE, sizeof(char));
    sprintf(buff, "Client connected from %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    logg(buff, INFO_COLOR);
    free(buff);

    char *request = calloc(BUFFER_SIZE, sizeof(char));
    err_code = read_request(client_socket, request);
    if (EXIT_FAILURE == err_code) {
      logg("Failed to read request_hash", ERROR_COLOR);
      free(request);
      close(client_socket);
      continue;
    }

    if (EXIT_SUCCESS != send_full_request_from_cache(request, client_socket)) {
      sem_wait(&thread_semaphore);
      logg("Init new connection", DEBUG_COLOR);
      Context ctx = {client_socket, request};
      pthread_t handler_thread;
      err_code = pthread_create(&handler_thread, NULL, &client_handler_routine, &ctx);
      if (FAIL_CODE == err_code) {
        logg("Failed to create thread", ERROR_COLOR);
        close(client_socket);
        close(server_socket);
        destroy_cache();
        exit(EXIT_FAILURE);
      }
    } else {
      free(request);
      close(client_socket);
      continue;
    }
  }

  close(server_socket);
  destroy_cache();
  sem_destroy(&thread_semaphore);
  exit(EXIT_SUCCESS);
}