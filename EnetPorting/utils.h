#pragma once
/**
 @file  time.h
 @brief ENet time constants and macros
*/
#ifndef __ENET_TIME_H__
#define __ENET_TIME_H__

#define ENET_TIME_OVERFLOW 86400000

#define ENET_TIME_LESS(a, b) ((a) - (b) >= ENET_TIME_OVERFLOW)
#define ENET_TIME_GREATER(a, b) ((b) - (a) >= ENET_TIME_OVERFLOW)
#define ENET_TIME_LESS_EQUAL(a, b) (! ENET_TIME_GREATER (a, b))
#define ENET_TIME_GREATER_EQUAL(a, b) (! ENET_TIME_LESS (a, b))

#define ENET_TIME_DIFFERENCE(a, b) ((a) - (b) >= ENET_TIME_OVERFLOW ? (b) - (a) : (a) - (b))

#endif /* __ENET_TIME_H__ */

/**
 @file  utility.h
 @brief ENet utility header
*/
#ifndef __ENET_UTILITY_H__
#define __ENET_UTILITY_H__

#ifdef HAS_OFFSETOF
#include <stddef.h>
#endif

#define ENET_MAX(x, y) ((x) > (y) ? (x) : (y))
#define ENET_MIN(x, y) ((x) < (y) ? (x) : (y))
#define ENET_DIFFERENCE(x, y) ((x) < (y) ? (y) - (x) : (x) - (y))

#ifdef HAS_OFFSETOF
#define ENET_OFFSETOF(str, field) (offsetof(str, field))
#else
#define ENET_OFFSETOF(str, field) ((size_t) & ((str *) 0) -> field)
#endif

#endif /* __ENET_UTILITY_H__ */