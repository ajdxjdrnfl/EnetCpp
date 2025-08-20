#pragma once
#include <stdint.h>
#include <cstddef>

using enet_uint8	= uint8_t;
using enet_uint16	= uint16_t;
using enet_uint32	= uint32_t;

#include <winsock2.h>
#include <stdlib.h>

#define ENET_SOCKET_NULL INVALID_SOCKET

#define ENET_HOST_TO_NET_16(value) (htons (value))
#define ENET_HOST_TO_NET_32(value) (htonl (value))

#define ENET_NET_TO_HOST_16(value) (ntohs (value))
#define ENET_NET_TO_HOST_32(value) (ntohl (value))

struct ENetBuffer
{
    size_t dataLength;
    void* data;
};

#define ENET_CALLBACK __cdecl

#ifdef ENET_DLL
#ifdef ENET_BUILDING_LIB
#define ENET_API __declspec( dllexport )
#else
#define ENET_API __declspec( dllimport )
#endif /* ENET_BUILDING_LIB */
#else /* !ENET_DLL */
#define ENET_API extern
#endif /* ENET_DLL */

using ENetSocketSet = fd_set;

#define ENET_SOCKETSET_EMPTY(sockset)          FD_ZERO (& (sockset))
#define ENET_SOCKETSET_ADD(sockset, socket)    FD_SET (socket, & (sockset))
#define ENET_SOCKETSET_REMOVE(sockset, socket) FD_CLR (socket, & (sockset))
#define ENET_SOCKETSET_CHECK(sockset, socket)  FD_ISSET (socket, & (sockset))