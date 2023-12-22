#ifndef LAB3_PROXY_LOGGER_H
#define LAB3_PROXY_LOGGER_H

#include <string.h>
#include <pthread.h>
#include <stdio.h>

#define LOG_BUFFER_SIZE 4096

#define RESET "\033[0m"
#define INFO_COLOR "\x1b[94m"
#define ERROR_COLOR "\x1b[91m"
#define WARNING_COLOR "\x1b[93m"
#define DEBUG_COLOR "\x1b[37m"
#define CRITICAL_ERROR_COLOR "\x1b[95m"

void logg(char *msg, char *color) {
  pthread_t thread_id = pthread_self();
  if (strcmp(color, CRITICAL_ERROR_COLOR) == 0 || strcmp(color, ERROR_COLOR) == 0) {
    char buf[LOG_BUFFER_SIZE];
    sprintf(buf, "%sThread-ID: %ld\t %s%s", color, thread_id, msg, RESET);
    perror(buf);
  } else {
    printf("%sThread-ID: %ld\t %s%s\n", color, thread_id, msg, RESET);
  }
  fflush(stdout);
}

void logg_char(char *msg, char *info, char *color) {
  char buf[LOG_BUFFER_SIZE + 100];
  sprintf(buf, "%s %s", msg, info);
  logg(buf, color);
}

void logg_int(char *msg, long info, char *color) {
  char buf[LOG_BUFFER_SIZE + 100];
  sprintf(buf, "%s%ld", msg, info);
  logg(buf, color);
}

#endif
