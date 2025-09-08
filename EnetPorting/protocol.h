#pragma once
/**
 @file  protocol.h
 @brief ENet protocol
*/
#ifndef __ENET_PROTOCOL_H__
#define __ENET_PROTOCOL_H__

#include "Types.h"

enum
{
	ENET_PROTOCOL_MINIMUM_MTU = 576,
	ENET_PROTOCOL_MAXIMUM_MTU = 4096,
	ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS = 32,
	ENET_PROTOCOL_MINIMUM_WINDOW_SIZE = 4096,
	ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE = 65536,
	ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT = 1,
	ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT = 255,
	ENET_PROTOCOL_MAXIMUM_PEER_ID = 0xFFF,
	ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT = 1024 * 1024
};

// 패킷 종류
enum ENetProtocolCommand
{
	ENET_PROTOCOL_COMMAND_NONE = 0,
	ENET_PROTOCOL_COMMAND_ACKNOWLEDGE = 1,
	ENET_PROTOCOL_COMMAND_CONNECT = 2,
	ENET_PROTOCOL_COMMAND_VERIFY_CONNECT = 3,
	ENET_PROTOCOL_COMMAND_DISCONNECT = 4,
	ENET_PROTOCOL_COMMAND_PING = 5,
	ENET_PROTOCOL_COMMAND_SEND_RELIABLE = 6,
	ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE = 7,
	ENET_PROTOCOL_COMMAND_SEND_FRAGMENT = 8,
	ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED = 9, // Unreliable + Unsequenced
	ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT = 10,
	ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE = 11,
	ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT = 12,
	ENET_PROTOCOL_COMMAND_COUNT = 13,

	ENET_PROTOCOL_COMMAND_MASK = 0x0F
};

// 패킷에 대한 메타 데이터의 종류
enum ENetProtocolFlag
{
	ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE = (1 << 7), // 이 플래그가 있다면, 다음에 받은 사람은 Ack를 보내줘야 한다
	ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED = (1 << 6),

	ENET_PROTOCOL_HEADER_FLAG_COMPRESSED = (1 << 14),
	ENET_PROTOCOL_HEADER_FLAG_SENT_TIME = (1 << 15),
	ENET_PROTOCOL_HEADER_FLAG_MASK = ENET_PROTOCOL_HEADER_FLAG_COMPRESSED | ENET_PROTOCOL_HEADER_FLAG_SENT_TIME,

	ENET_PROTOCOL_HEADER_SESSION_MASK = (3 << 12),
	ENET_PROTOCOL_HEADER_SESSION_SHIFT = 12
};

#ifdef _MSC_VER
#pragma pack(push, 1)
#define ENET_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define ENET_PACKED __attribute__ ((packed))
#else
#define ENET_PACKED
#endif

struct ENET_PACKED ENetProtocolHeader
{
	enet_uint16 peerID;
	enet_uint16 sentTime;
};

struct ENET_PACKED ENetProtocolCommandHeader
{
	enet_uint8 command;
	enet_uint8 channelID;
	enet_uint16 reliableSequenceNumber;
};

struct IENetProtoclCommand
{
	ENetProtocolCommandHeader header;
};

struct ENET_PACKED ENetProtocolSendFragment : public IENetProtoclCommand
{
	enet_uint16 startSequenceNumber; // Reliable/Unreliable용 sequenceNumber
	enet_uint16 dataLength;
	enet_uint32 fragmentCount;
	enet_uint32 fragmentNumber;
	enet_uint32 totalLength;
	enet_uint32 fragmentOffset;
};

struct ENET_PACKED ENetProtocolSendUnsequenced : public IENetProtoclCommand
{
	enet_uint16 unsequencedGroup;
	enet_uint16 dataLength;
};

struct ENET_PACKED ENetProtocolSendUnreliable : public IENetProtoclCommand
{
	enet_uint16 unreliableSequenceNumber;
	enet_uint16 dataLength;
};

struct ENET_PACKED ENetProtocolSendReliable : public IENetProtoclCommand
{
	enet_uint16 dataLength;
};

struct ENET_PACKED ENetProtocolPing : public IENetProtoclCommand
{
};

struct ENET_PACKED ENetProtocolDisconnect : public IENetProtoclCommand
{
	enet_uint32 data;
};

struct ENET_PACKED ENetProtocolVerifyConnect : public IENetProtoclCommand
{
	enet_uint16 outgoingPeerID;
	enet_uint8  incomingSessionID;
	enet_uint8  outgoingSessionID;
	enet_uint32 mtu;
	enet_uint32 windowSize;
	enet_uint32 channelCount;
	enet_uint32 incomingBandwidth;
	enet_uint32 outgoingBandwidth;
	enet_uint32 packetThrottleInterval;
	enet_uint32 packetThrottleAcceleration;
	enet_uint32 packetThrottleDeceleration;
	enet_uint32 connectID;
};

struct ENET_PACKED ENetProtocolConnect : public IENetProtoclCommand
{
	enet_uint16 outgoingPeerID;
	enet_uint8  incomingSessionID;
	enet_uint8  outgoingSessionID;
	enet_uint32 mtu;
	enet_uint32 windowSize;
	enet_uint32 channelCount;
	enet_uint32 incomingBandwidth;
	enet_uint32 outgoingBandwidth;
	enet_uint32 packetThrottleInterval;
	enet_uint32 packetThrottleAcceleration;
	enet_uint32 packetThrottleDeceleration;
	enet_uint32 connectID;
	enet_uint32 data;
};

struct ENET_PACKED ENetProtocolAcknowledge : public IENetProtoclCommand
{
	enet_uint16 receivedReliableSequenceNumber;
	enet_uint16 receivedSentTime;
};

struct ENET_PACKED ENetProtocolBandwidthLimit : public IENetProtoclCommand
{
	enet_uint32 incomingBandwidth;
	enet_uint32 outgoingBandwidth;
};

struct ENET_PACKED ENetProtocolThrottleConfigure : public IENetProtoclCommand
{
	enet_uint32 packetThrottleInterval;
	enet_uint32 packetThrottleAcceleration;
	enet_uint32 packetThrottleDeceleration;
};

union ENET_PACKED ENetProtocol
{
	ENetProtocolCommandHeader header;
	ENetProtocolAcknowledge acknowledge;
	ENetProtocolConnect connect;
	ENetProtocolVerifyConnect verifyConnect;
	ENetProtocolDisconnect disconnect;
	ENetProtocolPing ping;
	ENetProtocolSendReliable sendReliable;
	ENetProtocolSendUnreliable sendUnreliable;
	ENetProtocolSendUnsequenced sendUnsequenced;
	ENetProtocolSendFragment sendFragment;
	ENetProtocolBandwidthLimit bandwidthLimit;
	ENetProtocolThrottleConfigure throttleConfigure;
};

static const size_t commandSizes[ENET_PROTOCOL_COMMAND_COUNT] =
{
	0,
	sizeof(ENetProtocolAcknowledge),
	sizeof(ENetProtocolConnect),
	sizeof(ENetProtocolVerifyConnect),
	sizeof(ENetProtocolDisconnect),
	sizeof(ENetProtocolPing),
	sizeof(ENetProtocolSendReliable),
	sizeof(ENetProtocolSendUnreliable),
	sizeof(ENetProtocolSendFragment),
	sizeof(ENetProtocolSendUnsequenced),
	sizeof(ENetProtocolBandwidthLimit),
	sizeof(ENetProtocolThrottleConfigure),
	sizeof(ENetProtocolSendFragment)
};

static void enet_protocol_notify_connect(class ENetHost* host, class ENetPeer* peer, class ENetEvent* event);
static void enet_protocol_notify_disconnect(class ENetHost* host, class ENetPeer* peer, class ENetEvent* event);
static void enet_protocol_remove_sent_unreliable_commands(class ENetPeer* peer, class ENetList* sentUnreliableCommands);


#ifdef _MSC_VER
#pragma pack(pop)
#endif

#endif /* __ENET_PROTOCOL_H__ */