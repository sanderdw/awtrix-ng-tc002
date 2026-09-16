#pragma once
// Upstream's UDP socket is written against lwIP's BSD-style API; Linux provides the same names.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
