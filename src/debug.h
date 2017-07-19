#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#define warn(fmt, ...) printf(__FILE__ ":%3d: [WARN] " fmt "\n", __LINE__, ##__VA_ARGS__)

#if defined(DEBUG)
#define dbg(fmt, ...) printf(__FILE__ ":%3d: " fmt "\n", __LINE__, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)
#endif

#endif
