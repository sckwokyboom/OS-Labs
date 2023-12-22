#ifndef LAB3_PROXY_CACHE_LIST_H
#define LAB3_PROXY_CACHE_LIST_H

#include <pthread.h>
#include <stdlib.h>
#include "logger.h"

#define CACHE_BUFFER_SIZE (1024 * 1024 * 500)

typedef struct cache {
  char *response;
  ssize_t response_len;
  unsigned long request_hash;
  struct cache *next;
} Cache;

pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned long hash(const char *str) {
  unsigned long hash = 0;
  while (*str != '\0') {
    hash = ((hash << 5) + hash) + (unsigned long) (*str);
    str++;
  }
  return hash;
}

int init_cache_record(Cache *record) {
  record->response = (char *) calloc(CACHE_BUFFER_SIZE, sizeof(char));
  if (record->response == NULL) {
    logg("Failed to allocate memory to new response array", ERROR_COLOR);
    return EXIT_FAILURE;
  }
  record->next = NULL;
  return EXIT_SUCCESS;
}

ssize_t find_in_cache(Cache *start, char *req, char *copy) {
  Cache *cur = start;
  pthread_mutex_lock(&cache_mutex);
  unsigned long req_hash = hash(req);
  while (cur != NULL) {
    if (cur->request_hash == req_hash) {
      strncpy(copy, cur->response, cur->response_len);
      pthread_mutex_unlock(&cache_mutex);
      return cur->response_len;
    }
    cur = cur->next;
  }
  pthread_mutex_unlock(&cache_mutex);
  return -1;
}

void add_request_hash(Cache *record, char *req) {
  unsigned long hash_value = hash(req);
  logg_int("Current hash = ", (long) hash_value, DEBUG_COLOR);
  record->request_hash = hash_value;
}

void add_response_to_cache(Cache *record, char *resp, unsigned long cur_position, unsigned long resp_size) {
  memcpy(record->response + cur_position, resp, resp_size);
}

void add_size_of_response(Cache *record, ssize_t size) {
  record->response_len = size;
}

void delete_cache_record(Cache *record) {
  free(record->response);
}

void push_cache_record(Cache *start, Cache *record) {
  Cache *cur = start;
  pthread_mutex_lock(&cache_mutex);
  logg("Starting caching", INFO_COLOR);
  while (cur->next != NULL) {
    cur = cur->next;
    if (cur->request_hash == record->request_hash) {
      logg("Find duplicate while pushing cache record", WARNING_COLOR);
      delete_cache_record(record);
      free(record);
      pthread_mutex_unlock(&cache_mutex);
      return;
    }
  }
  cur->next = record;
  logg_int("Cached the result, length [B] = ", record->response_len, INFO_COLOR);
  printf("\n");
  pthread_mutex_unlock(&cache_mutex);
}

#endif
