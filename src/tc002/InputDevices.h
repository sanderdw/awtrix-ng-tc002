#pragma once
/* Finds the clock's button and rotary input nodes by what they can do, not by their number.
 * Shared by the application (C++) and the launcher (C). */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TC002_INPUT_BITS(n) (((n) + 7) / 8)

static inline int tc002BitSet(const unsigned char* bits, unsigned bit) {
  return (bits[bit / 8] >> (bit % 8)) & 1;
}

/* 1 for a button device (the -/+ rocker and knob keys), 2 for the rotary encoder (EV_ABS without
 * touch coordinates), 0 for anything else such as the touch panel the vendor GUI lists. */
static inline int tc002ClassifyInput(const unsigned char* evBits, const unsigned char* keyBits,
                                     const unsigned char* absBits) {
  const int touch = tc002BitSet(evBits, EV_KEY) && tc002BitSet(keyBits, BTN_TOUCH);
  const int multitouch = tc002BitSet(evBits, EV_ABS) && tc002BitSet(absBits, ABS_MT_POSITION_X);
  if (touch || multitouch) return 0;
  if (tc002BitSet(evBits, EV_KEY) &&
      (tc002BitSet(keyBits, KEY_UP) || tc002BitSet(keyBits, KEY_DOWN) ||
       tc002BitSet(keyBits, KEY_LEFT) || tc002BitSet(keyBits, KEY_RIGHT)))
    return 1;
  if (tc002BitSet(evBits, EV_ABS)) return 2;
  return 0;
}

static inline int tc002ClassifyInputFd(int fd) {
  unsigned char evBits[TC002_INPUT_BITS(EV_MAX + 1)] = {0};
  unsigned char keyBits[TC002_INPUT_BITS(KEY_MAX + 1)] = {0};
  unsigned char absBits[TC002_INPUT_BITS(ABS_MAX + 1)] = {0};
  if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0) return 0;
  ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);
  ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
  return tc002ClassifyInput(evBits, keyBits, absBits);
}

/* Opens up to `max` qualifying nodes into fds[] and returns how many were found. The nodes the
 * tested firmware uses (event67, event68) are tried first so the known device keeps its order. */
static inline int tc002OpenInputDevices(int* fds, int max, int flags) {
  int found = 0, i;
  char path[32];
  for (i = 0; i < 100 + 2 && found < max; ++i) {
    const int number = i < 2 ? 67 + i : i - 2;
    if (i >= 2 && (number == 67 || number == 68)) continue;
    snprintf(path, sizeof(path), "/dev/input/event%d", number);
    const int fd = open(path, flags);
    if (fd < 0) continue;
    if (tc002ClassifyInputFd(fd)) fds[found++] = fd;
    else close(fd);
  }
  return found;
}

/* True when the knob (select) is currently pressed on any open input node. */
static inline int tc002SelectHeld(const int* fds, int count) {
  unsigned char keys[TC002_INPUT_BITS(KEY_MAX + 1)];
  int i;
  for (i = 0; i < count; ++i) {
    memset(keys, 0, sizeof(keys));
    if (ioctl(fds[i], EVIOCGKEY(sizeof(keys)), keys) < 0) continue;
    if (tc002BitSet(keys, KEY_LEFT) || tc002BitSet(keys, KEY_UP)) return 1;
  }
  return 0;
}
