#pragma once
/* Boot attempt accounting for the launcher. Every start that does not reach a healthy state
 * within a minute counts; after three in a row the launcher runs the vendor application instead,
 * so a clock whose AWTRIX build crashes at start-up is never left without a working UI. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TC002_BOOT_ATTEMPT_LIMIT 3

static inline int tc002ReadBootAttempts(const char* path) {
  FILE* f = fopen(path, "r");
  int attempts = 0;
  if (!f) return 0;
  if (fscanf(f, "%d", &attempts) != 1 || attempts < 0 || attempts > 1000) attempts = 0;
  fclose(f);
  return attempts;
}

/* Forces a file's directory entry to flash: the counter exists precisely for boots that end in a
 * power cut seconds later, and jffs2 only commits what has been synced by then. */
static inline void tc002SyncDirectoryOf(const char* path) {
  char dir[300];
  const char* slash = strrchr(path, '/');
  if (!slash || (size_t)(slash - path) >= sizeof(dir)) return;
  memcpy(dir, path, (size_t)(slash - path));
  dir[slash - path] = 0;
  const int fd = open(*dir ? dir : "/", O_RDONLY | O_DIRECTORY);
  if (fd < 0) return;
  fsync(fd);
  close(fd);
}

/* Increments the counter on disk, synced to flash, and returns the new value; a write failure
 * counts as one attempt. */
static inline int tc002RecordBootAttempt(const char* path) {
  const int attempts = tc002ReadBootAttempts(path) + 1;
  FILE* f = fopen(path, "w");
  if (!f) return attempts;
  fprintf(f, "%d\n", attempts);
  fflush(f);
  fsync(fileno(f));
  fclose(f);
  tc002SyncDirectoryOf(path);
  sync();
  return attempts;
}

static inline void tc002ClearBootAttempts(const char* path) {
  unlink(path);
  tc002SyncDirectoryOf(path);
}

/* The fourth consecutive start without a healthy run falls back to the vendor application. */
static inline int tc002ShouldFallBack(int attempts) { return attempts > TC002_BOOT_ATTEMPT_LIMIT; }
