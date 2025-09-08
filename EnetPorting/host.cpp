/**
 @file host.c
 @brief ENet host management functions
*/
#define ENET_BUILDING_LIB 1
#include <string.h>
#include "enet.h"
#include "protocol.h"
#include "callbacks.h"
#include "utils.h"

/** @defgroup host ENet host functions
    @{
*/

/** Creates a host for communicating to peers.

    @param address   the address at which other peers may connect to this host.  If NULL, then no peers may connect to the host.
    @param peerCount the maximum number of peers that should be allocated for the host.
    @param channelLimit the maximum number of channels allowed; if 0, then this is equivalent to ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT
    @param incomingBandwidth downstream bandwidth of the host in bytes/second; if 0, ENet will assume unlimited bandwidth.
    @param outgoingBandwidth upstream bandwidth of the host in bytes/second; if 0, ENet will assume unlimited bandwidth.

    @returns the host on success and NULL on failure

    @remarks ENet will strategically drop packets on specific sides of a connection between hosts
    to ensure the host's bandwidth is not overwhelmed.  The bandwidth parameters also determine
    the window size of a connection which limits the amount of reliable packets that may be in transit
    at any given time.
*/

ENetHost* ENetService::enet_host_create(const ENetAddress* address, size_t peerCount, size_t channelLimit, enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
{
    ENetHost* host;

    if (host->peerCount > ENET_PROTOCOL_MAXIMUM_PEER_ID)
        return nullptr;

    host = (ENetHost*)enet_malloc(sizeof(ENetHost));
    if (host == nullptr)
        return nullptr;

    memset(host, 0, sizeof(ENetHost));

    for (int i = 0; i < host->peers.size(); i++)
    {
        host->peers[i] = static_cast<ENetPeer*>(enet_malloc(sizeof(ENetPeer)));
        if (host->peers[i] == nullptr)
            return nullptr;
    }

    host->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (host->socket.get_socket() == ENET_SOCKET_NULL || (address != nullptr && enet_socket_bind(host->socket, address) < 0))
    {
        if (host->socket.get_socket() != ENET_SOCKET_NULL)
            enet_socket_destroy(host->socket);

        for (int i = 0; i < host->peers.size(); i++)
            enet_free(host->peers[i]);

        enet_free(host);

        return nullptr;
    }

    enet_socket_set_option(host->socket, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_BROADCAST, 1);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_RCVBUF, ENET_HOST_RECEIVE_BUFFER_SIZE);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_SNDBUF, ENET_HOST_SEND_BUFFER_SIZE);

    if (address != NULL && enet_socket_get_address(host->socket, &host->address) < 0)
        host->address = *address;

    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    else
        if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
            channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

    host->randomSeed = (enet_uint32)(size_t)host;
    host->randomSeed += enet_host_random_seed();
    host->randomSeed = (host->randomSeed << 16) | (host->randomSeed >> 16);
    host->channelLimit = channelLimit;
    host->incomingBandwidth = incomingBandwidth;
    host->outgoingBandwidth = outgoingBandwidth;
    host->bandwidthThrottleEpoch = 0;
    host->recalculateBandwidthLimits = 0;
    host->mtu = ENET_HOST_DEFAULT_MTU;
    host->peerCount = peerCount;
    host->commandCount = 0;
    host->bufferCount = 0;
    host->checksum = nullptr;
    host->receivedAddress.host = ENET_HOST_ANY;
    host->receivedAddress.port = 0;
    host->receivedData = nullptr;
    host->receivedDataLength = 0;

    host->totalSentData = 0;
    host->totalSentPackets = 0;
    host->totalReceivedData = 0;
    host->totalReceivedPackets = 0;
    host->totalQueued = 0;

    host->connectedPeers = 0;
    host->bandwidthLimitedPeers = 0;
    host->duplicatePeers = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    host->maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
    host->maximumWaitingData = ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA;

    host->compressor.context = nullptr;
    host->compressor.compress = nullptr;
    host->compressor.decompress = nullptr;
    host->compressor.destroy = nullptr;

    host->intercept = nullptr;

    host->dispatchQueue.clear();

    for (int i = 0; i < host->peers.size(); i++)
    {
        auto* currentPeer = host->peers[i];

        currentPeer->host = host;
        currentPeer->incomingPeerID = i;
        currentPeer->outgoingSessionID = currentPeer->incomingSessionID = 0xFF;
        currentPeer->data = nullptr;

        currentPeer->acknowledgements.clear();
        currentPeer->sentReliableCommands.clear();
        currentPeer->outgoingCommands.clear();
        currentPeer->outgoingSendReliableCommands.clear();
        currentPeer->dispatchedCommands.clear();

        enet_peer_reset(currentPeer);
    }

    return host;
}

ENetHost* enet_host_create(const ENetAddress* address, size_t peerCount, size_t channelLimit, enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
{
    ENetHost* host;

    if (peerCount > ENET_PROTOCOL_MAXIMUM_PEER_ID)
        return nullptr;

    host = (ENetHost*)enet_malloc(sizeof(ENetHost));
    if (host == nullptr)
        return nullptr;

    memset(host, 0, sizeof(ENetHost));

    for (int i = 0; i < host->peers.size(); i++)
    {
        host->peers[i] = static_cast<ENetPeer*>(enet_malloc(sizeof(ENetPeer)));
        if (host->peers[i] == nullptr)
            return nullptr;
    }

    host->socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (host->socket.get_socket() == ENET_SOCKET_NULL || (address != nullptr && enet_socket_bind(host->socket, address) < 0))
    {
        if (host->socket.get_socket() != ENET_SOCKET_NULL)
            enet_socket_destroy(host->socket);
        
        for (int i = 0; i < host->peers.size(); i++)
            enet_free(host->peers[i]);

        enet_free(host);

        return nullptr;
    }

    enet_socket_set_option(host->socket, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_BROADCAST, 1);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_RCVBUF, ENET_HOST_RECEIVE_BUFFER_SIZE);
    enet_socket_set_option(host->socket, ENET_SOCKOPT_SNDBUF, ENET_HOST_SEND_BUFFER_SIZE);

    if (address != NULL && enet_socket_get_address(host->socket, &host->address) < 0)
        host->address = *address;

    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    else
        if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
            channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

    host->randomSeed = (enet_uint32)(size_t)host;
    host->randomSeed += enet_host_random_seed();
    host->randomSeed = (host->randomSeed << 16) | (host->randomSeed >> 16);
    host->channelLimit = channelLimit;
    host->incomingBandwidth = incomingBandwidth;
    host->outgoingBandwidth = outgoingBandwidth;
    host->bandwidthThrottleEpoch = 0;
    host->recalculateBandwidthLimits = 0;
    host->mtu = ENET_HOST_DEFAULT_MTU;
    host->peerCount = peerCount;
    host->commandCount = 0;
    host->bufferCount = 0;
    host->checksum = nullptr;
    host->receivedAddress.host = ENET_HOST_ANY;
    host->receivedAddress.port = 0;
    host->receivedData = nullptr;
    host->receivedDataLength = 0;

    host->totalSentData = 0;
    host->totalSentPackets = 0;
    host->totalReceivedData = 0;
    host->totalReceivedPackets = 0;
    host->totalQueued = 0;

    host->connectedPeers = 0;
    host->bandwidthLimitedPeers = 0;
    host->duplicatePeers = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    host->maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
    host->maximumWaitingData = ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA;

    host->compressor.context = nullptr;
    host->compressor.compress = nullptr;
    host->compressor.decompress = nullptr;
    host->compressor.destroy = nullptr;

    host->intercept = nullptr;

    host->dispatchQueue.clear();

    for (int i = 0; i < host->peers.size(); i++)
    {
        auto* currentPeer = host->peers[i];

        currentPeer->host = host;
        currentPeer->incomingPeerID = i;
        currentPeer->outgoingSessionID = currentPeer->incomingSessionID = 0xFF;
        currentPeer->data = nullptr;

        currentPeer->acknowledgements.clear();
        currentPeer->sentReliableCommands.clear();
        currentPeer->outgoingCommands.clear();
        currentPeer->outgoingSendReliableCommands.clear();
        currentPeer->dispatchedCommands.clear();

        enet_peer_reset(currentPeer);
    }

    return host;
}

/** Destroys the host and all resources associated with it.
    @param host pointer to the host to destroy
*/
void enet_host_destroy(ENetHost* host)
{
    if (host == NULL)
        return;

    enet_socket_destroy(host->socket);

    for (int i = 0; i < host->peers.size(); i++)
    {
        ENetPeer* currentPeer = host->peers[i];

        if(currentPeer!=nullptr)
            enet_peer_reset(currentPeer);
    }

    if (host->compressor.context != nullptr && host->compressor.destroy != nullptr)
        host->compressor.destroy(host->compressor.context);

    for (int i = 0; i < host->peers.size(); i++)
        enet_free(host->peers[i]);

    enet_free(host);
}

enet_uint32 enet_host_random(ENetHost* host)
{
    /* Mulberry32 by Tommy Ettinger */
    enet_uint32 n = (host->randomSeed += 0x6D2B79F5U);
    n = (n ^ (n >> 15)) * (n | 1U);
    n ^= n + (n ^ (n >> 7)) * (n | 61U);
    return n ^ (n >> 14);
}

/** Initiates a connection to a foreign host.
    @param host host seeking the connection
    @param address destination for the connection
    @param channelCount number of channels to allocate
    @param data user data supplied to the receiving host
    @returns a peer representing the foreign host on success, NULL on failure
    @remarks The peer returned will have not completed the connection until enet_host_service()
    notifies of an ENET_EVENT_TYPE_CONNECT event for the peer.
*/
ENetPeer* enet_host_connect(ENetHost* host, const ENetAddress* address, size_t channelCount, enet_uint32 data)
{
    ENetPeer* currentPeer = nullptr;
    ENetChannel* channel;
    ENetProtocol command;

    if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
        channelCount = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;
    else
        if (channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
            channelCount = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;

    for (int i = 0; i < host->peers.size(); i++)
    {
        currentPeer = host->peers[i];

        if (currentPeer == nullptr)
            continue;

        if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED)
            break;
    }

    currentPeer->channels = (ENetChannel*)enet_malloc(channelCount * sizeof(ENetChannel));
    if (currentPeer->channels == nullptr)
        return nullptr;

    currentPeer->channelCount = channelCount;
    currentPeer->state = ENET_PEER_STATE_CONNECTING;
    currentPeer->address = *address;
    currentPeer->connectID = enet_host_random(host);
    currentPeer->mtu = host->mtu;

    /* windowSize?? */
    if (host->outgoingBandwidth == 0)
        currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    else
        currentPeer->windowSize = (host->outgoingBandwidth /
            ENET_PEER_WINDOW_SIZE_SCALE) *
        ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

    if (currentPeer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
        currentPeer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    else
        if (currentPeer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
            currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

    for (channel = currentPeer->channels;
        channel < &currentPeer->channels[channelCount];
        ++channel)
    {
        channel->outgoingReliableSequenceNumber = 0;
        channel->outgoingUnreliableSequenceNumber = 0;
        channel->incomingReliableSequenceNumber = 0;
        channel->incomingUnreliableSequenceNumber = 0;

        channel->incomingReliableCommands.clear();
        channel->incomingUnreliableCommands.clear();

        channel->usedReliableWindows = 0;
        memset(channel->reliableWindows, 0, sizeof(channel->reliableWindows));
    }

    command.header.command = ENET_PROTOCOL_COMMAND_CONNECT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;
    command.connect.outgoingPeerID = ENET_HOST_TO_NET_16(currentPeer->incomingPeerID);
    command.connect.incomingSessionID = currentPeer->incomingSessionID;
    command.connect.outgoingSessionID = currentPeer->outgoingSessionID;
    command.connect.mtu = ENET_HOST_TO_NET_32(currentPeer->mtu);
    command.connect.windowSize = ENET_HOST_TO_NET_32(currentPeer->windowSize);
    command.connect.channelCount = ENET_HOST_TO_NET_32(channelCount);
    command.connect.incomingBandwidth = ENET_HOST_TO_NET_32(host->incomingBandwidth);
    command.connect.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);
    command.connect.packetThrottleInterval = ENET_HOST_TO_NET_32(currentPeer->packetThrottleInterval);
    command.connect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleAcceleration);
    command.connect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleDeceleration);
    command.connect.connectID = currentPeer->connectID;
    command.connect.data = ENET_HOST_TO_NET_32(data);

    enet_peer_queue_outgoing_command(currentPeer, &command, NULL, 0, 0);

    return currentPeer;
}

/** Queues a packet to be sent to all peers associated with the host.
    @param host host on which to broadcast the packet
    @param channelID channel on which to broadcast
    @param packet packet to broadcast
*/
void ENetHost::broadcast(enet_uint8 channelID, shared_ptr<ENetPacket> packet)
{
    ENetPeer* currentPeer;

    for (int i = 0; i < peers.size(); i++)
    {
        currentPeer = peers[i];

        if (currentPeer == nullptr)
            continue;

        if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
            continue;

        currentPeer->SendPacket(channelID, packet);
    }

}

/** Sets the packet compressor the host should use to compress and decompress packets.
    @param host host to enable or disable compression for
    @param compressor callbacks for for the packet compressor; if NULL, then compression is disabled
*/
void ENetHost::compress(const ENetCompressor* compressor)
{
    if (this->compressor.context != nullptr && this->compressor.destroy != nullptr)
        this->compressor.destroy(this->compressor.context);

    if (compressor)
        this->compressor = *compressor;
    else
        this->compressor.context = nullptr;
}

void enet_host_compress(ENetHost* host, const ENetCompressor* compressor)
{
    if (host->compressor.context != nullptr && host->compressor.destroy != nullptr)
        host->compressor.destroy(host->compressor.context);

    if (compressor)
        host->compressor = *compressor;
    else
        host->compressor.context = nullptr;
}

/** Limits the maximum allowed channels of future incoming connections.
    @param host host to limit
    @param channelLimit the maximum number of channels allowed; if 0, then this is equivalent to ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT
*/
void ENetHost::bandwidth_limit(enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
{
    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    else
        if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
            channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

    this->channelLimit = channelLimit;
}

void enet_host_channel_limit(ENetHost* host, size_t channelLimit)
{
    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    else
        if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
            channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;

    host->channelLimit = channelLimit;
}


/** Adjusts the bandwidth limits of a host.
    @param host host to adjust
    @param incomingBandwidth new incoming bandwidth
    @param outgoingBandwidth new outgoing bandwidth
    @remarks the incoming and outgoing bandwidth parameters are identical in function to those
    specified in enet_host_create().
*/
void ENetHost::bandwidth_limit(enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
{
    this->incomingBandwidth = incomingBandwidth;
    this->outgoingBandwidth = outgoingBandwidth;
    this->recalculateBandwidthLimits = 1;
}

void enet_host_bandwidth_limit(ENetHost* host, enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
{
    host->incomingBandwidth = incomingBandwidth;
    host->outgoingBandwidth = outgoingBandwidth;
    host->recalculateBandwidthLimits = 1;
}

/* 
    throttle은 말그대로 throttling이 일어날때를 위해 보낼 데이터의 양을 조절
    throttle은 bandwidth * (ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal로 계산한다
    peer의 bandwidth를 계속해서 조절
*/
void ENetHost::bandwidth_throttle()
{
    enet_uint32 timeCurrent = enet_time_get(),
        elapsedTime = timeCurrent - bandwidthThrottleEpoch,
        peersRemaining = (enet_uint32)connectedPeers,
        dataTotal = ~0,
        bandwidth = ~0,
        throttle = 0,
        bandwidthLimit = 0;
    int needsAdjustment = bandwidthLimitedPeers > 0 ? 1 : 0;
    ENetPeer* peer;
    ENetProtocol command;

    if (elapsedTime < ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
        return;

    bandwidthThrottleEpoch = timeCurrent;

    if (peersRemaining == 0)
        return;

    // outgoingDataTotal - peer에게 가는 DataTotal

    if (outgoingBandwidth != 0)
    {
        dataTotal = 0;
        bandwidth = (outgoingBandwidth * elapsedTime) / 1000;

        for (int i = 0; i < peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(peers[i], peer)

                if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
                    continue;

            dataTotal += peer->outgoingDataTotal;
        }
    }
    // packetThrottleLimit을 계산해준다. packetThrottle은 데이터르 보내는 양을 결정하는 windowSize에서 사용한다
    while (peersRemaining > 0 && needsAdjustment != 0)
    {
        needsAdjustment = 0;
        // throttle은 throttling이 일어날때, 패킷의 양을 조절하기 위한 변수
        // bandwith보다 dataTotal이 작다면
        // 아직은 데이터를 더 보낼 수 있음
        // 따라서 최대값을 넣어준다

        if (dataTotal <= bandwidth)
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        /* 반대로 bandwidth보다 보낼 데이터가 많으면
           보내는 데이터가 너무 많으므로
           throttle에는 비율을 곱해주어, 패킷의 양을 줄여준다.
        */
        else
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

        for (int i = 0; i < peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(peers[i], peer)

                enet_uint32 peerBandwidth;

            if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                peer->incomingBandwidth == 0 ||
                peer->outgoingBandwidthThrottleEpoch == timeCurrent)
                continue;

            // peer가 받을 수 있는 Bandwidth - peerBandwidth
            // incoming - 피어로 부터 들어오는 데이터 관련
            // outcoming - 피어한테 보낼 데이터 관련
            peerBandwidth = (peer->incomingBandwidth * elapsedTime) / 1000;
            // 피어가 받을 수 있는 양이, 보낼 데이터보다 많은가?
            if ((throttle * peer->outgoingDataTotal) <= peerBandwidth * ENET_PEER_PACKET_THROTTLE_SCALE)
                continue;

            // 여기서부터는 받을 수 있는 양보다 보내는 데이터가 큰 경우 - 너무 튀는 경우는 제외해준다
            // packetThrottleLimit에는 보내고자 한 총 데이터 양을 Bandwith로 나눈 비율을 넣어줌 - 정확히는 bandwidth를 조절해줌
            peer->packetThrottleLimit = (peerBandwidth *
                ENET_PEER_PACKET_THROTTLE_SCALE) / peer->outgoingDataTotal;

            if (peer->packetThrottleLimit == 0)
                peer->packetThrottleLimit = 1;

            if (peer->packetThrottle > peer->packetThrottleLimit)
                peer->packetThrottle = peer->packetThrottleLimit;

            peer->outgoingBandwidthThrottleEpoch = timeCurrent;

            // incomingDataTotal, outgoingDataTotal = 0
            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;

            // 데이터의 양을 조정해야겠다
            // 해당 peer들에게는 데이터를 보내지 않을 계획?이므로 빼줌 - 추후 확인
            needsAdjustment = 1;
            --peersRemaining;
            bandwidth -= peerBandwidth; // 해당 peer의 대역폭만큼 감소
            dataTotal -= peerBandwidth; // 

        }

    }
    // 적정 incomingBandwidth인 애들에 대해서
    if (peersRemaining > 0)
    {
        if (dataTotal <= bandwidth)
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        else
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

        for (int i = 0; i < peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(peers[i], peer)

                if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                    peer->outgoingBandwidthThrottleEpoch == timeCurrent)
                    continue;

            peer->packetThrottleLimit = throttle;

            // 실제 사용되는 throttle은 packetThrottle
            if (peer->packetThrottle > peer->packetThrottleLimit)
                peer->packetThrottle = peer->packetThrottleLimit;

            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;
        }
    }

    // host의 bandwidth를 재계산
    if (recalculateBandwidthLimits)
    {
        recalculateBandwidthLimits = 0;

        peersRemaining = (enet_uint32)connectedPeers;
        // host가 받을 수 있는 bandwidth
        bandwidth = incomingBandwidth;
        needsAdjustment = 1;

        if (bandwidth == 0)
            bandwidthLimit = 0;
        else
            while (peersRemaining > 0 && needsAdjustment != 0)
            {
                needsAdjustment = 0;
                // 각 peer마다, host가 받을 수 있는 bandwidth
                bandwidthLimit = bandwidth / peersRemaining;

                for (int i = 0; i < peers.size(); i++)
                {
                    IF_NULLPTR_THEN_CONTINUE(peers[i], peer)

                        if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                            peer->incomingBandwidthThrottleEpoch == timeCurrent)
                            continue;

                    // host의 수신 병목을 기준으로 peer의 outgoingBandwidth를 판단해주는듯?
                    // bandwidthLimit보다 크면, otugoingBandwidth는 bandwidthLimit와 같다 - 아래 참고
                    if (peer->outgoingBandwidth > 0 &&
                        peer->outgoingBandwidth >= bandwidthLimit)
                        continue;

                    peer->incomingBandwidthThrottleEpoch = timeCurrent;

                    needsAdjustment = 1;
                    --peersRemaining;
                    bandwidth -= peer->outgoingBandwidth;
                }
            }

        for (int i = 0; i < peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(peers[i], peer)

                if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
                    continue;

            command.header.command = ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            command.header.channelID = 0xFF;
            command.bandwidthLimit.outgoingBandwidth = ENET_HOST_TO_NET_32(outgoingBandwidth);

            if (peer->incomingBandwidthThrottleEpoch == timeCurrent)
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(peer->outgoingBandwidth);
            else
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(bandwidthLimit);

            enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
        }
    }
}

void enet_host_bandwidth_throttle(ENetHost* host)
{
    enet_uint32 timeCurrent = enet_time_get(),
        elapsedTime = timeCurrent - host->bandwidthThrottleEpoch,
        peersRemaining = (enet_uint32)host->connectedPeers,
        dataTotal = ~0,
        bandwidth = ~0,
        throttle = 0,
        bandwidthLimit = 0;
    int needsAdjustment = host->bandwidthLimitedPeers > 0 ? 1 : 0;
    ENetPeer* peer;
    ENetProtocol command;

    if (elapsedTime < ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
        return;

    host->bandwidthThrottleEpoch = timeCurrent;

    if (peersRemaining == 0)
        return;

    // outgoingDataTotal - peer에게 가는 DataTotal
    
    if (host->outgoingBandwidth != 0)
    {
        dataTotal = 0;
        bandwidth = (host->outgoingBandwidth * elapsedTime) / 1000;

        for (int i = 0; i < host->peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(host->peers[i], peer)

            if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
                continue;

            dataTotal += peer->outgoingDataTotal;
        }
    }
    // packetThrottleLimit을 계산해준다. packetThrottle은 데이터르 보내는 양을 결정하는 windowSize에서 사용한다
    while (peersRemaining > 0 && needsAdjustment != 0)
    {
        needsAdjustment = 0;
        // throttle은 throttling이 일어날때, 패킷의 양을 조절하기 위한 변수
        // bandwith보다 dataTotal이 작다면
        // 아직은 데이터를 더 보낼 수 있음
        // 따라서 최대값을 넣어준다

        if (dataTotal <= bandwidth)
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        /* 반대로 bandwidth보다 보낼 데이터가 많으면
           보내는 데이터가 너무 많으므로
           throttle에는 비율을 곱해주어, 패킷의 양을 줄여준다.
        */
        else
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

        for (int i = 0; i < host->peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(host->peers[i], peer)

            enet_uint32 peerBandwidth;

            if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                peer->incomingBandwidth == 0 ||
                peer->outgoingBandwidthThrottleEpoch == timeCurrent)
                continue;

            // peer가 받을 수 있는 Bandwidth - peerBandwidth
            // incoming - 피어로 부터 들어오는 데이터 관련
            // outcoming - 피어한테 보낼 데이터 관련
            peerBandwidth = (peer->incomingBandwidth * elapsedTime) / 1000;
            // 피어가 받을 수 있는 양이, 보낼 데이터보다 많은가?
            if ((throttle * peer->outgoingDataTotal) <= peerBandwidth * ENET_PEER_PACKET_THROTTLE_SCALE)
                continue;

            // 여기서부터는 받을 수 있는 양보다 보내는 데이터가 큰 경우 - 너무 튀는 경우는 제외해준다
            // packetThrottleLimit에는 보내고자 한 총 데이터 양을 Bandwith로 나눈 비율을 넣어줌 - 정확히는 bandwidth를 조절해줌
            peer->packetThrottleLimit = (peerBandwidth *
                ENET_PEER_PACKET_THROTTLE_SCALE) / peer->outgoingDataTotal;

            if (peer->packetThrottleLimit == 0)
                peer->packetThrottleLimit = 1;

            if (peer->packetThrottle > peer->packetThrottleLimit)
                peer->packetThrottle = peer->packetThrottleLimit;

            peer->outgoingBandwidthThrottleEpoch = timeCurrent;

            // incomingDataTotal, outgoingDataTotal = 0
            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;

            // 데이터의 양을 조정해야겠다
            // 해당 peer들에게는 데이터를 보내지 않을 계획?이므로 빼줌 - 추후 확인
            needsAdjustment = 1;
            --peersRemaining;
            bandwidth -= peerBandwidth; // 해당 peer의 대역폭만큼 감소
            dataTotal -= peerBandwidth; // 

        }
        
    }
    // 적정 incomingBandwidth인 애들에 대해서
    if (peersRemaining > 0)
    {
        if (dataTotal <= bandwidth)
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        else
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;

        for (int i = 0; i < host->peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(host->peers[i], peer)

            if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                peer->outgoingBandwidthThrottleEpoch == timeCurrent)
                continue;

            peer->packetThrottleLimit = throttle;

            // 실제 사용되는 throttle은 packetThrottle
            if (peer->packetThrottle > peer->packetThrottleLimit)
                peer->packetThrottle = peer->packetThrottleLimit;

            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;
        }
    }

    // host의 bandwidth를 재계산
    if (host->recalculateBandwidthLimits)
    {
        host->recalculateBandwidthLimits = 0;

        peersRemaining = (enet_uint32)host->connectedPeers;
        // host가 받을 수 있는 bandwidth
        bandwidth = host->incomingBandwidth;
        needsAdjustment = 1;

        if (bandwidth == 0)
            bandwidthLimit = 0;
        else
            while (peersRemaining > 0 && needsAdjustment != 0)
            {
                needsAdjustment = 0;
                // 각 peer마다, host가 받을 수 있는 bandwidth
                bandwidthLimit = bandwidth / peersRemaining;

                for (int i = 0; i < host->peers.size(); i++)
                {
                    IF_NULLPTR_THEN_CONTINUE(host->peers[i], peer)

                    if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                        peer->incomingBandwidthThrottleEpoch == timeCurrent)
                        continue;

                    // host의 수신 병목을 기준으로 peer의 outgoingBandwidth를 판단해주는듯?
                    // bandwidthLimit보다 크면, otugoingBandwidth는 bandwidthLimit와 같다 - 아래 참고
                    if (peer->outgoingBandwidth > 0 &&
                        peer->outgoingBandwidth >= bandwidthLimit)
                        continue;

                    peer->incomingBandwidthThrottleEpoch = timeCurrent;
                    
                    needsAdjustment = 1;
                    --peersRemaining;
                    bandwidth -= peer->outgoingBandwidth;
                }
            }

        for (int i = 0; i < host->peers.size(); i++)
        {
            IF_NULLPTR_THEN_CONTINUE(host->peers[i], peer)

            if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
                continue;

            command.header.command = ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            command.header.channelID = 0xFF;
            command.bandwidthLimit.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);

            if (peer->incomingBandwidthThrottleEpoch == timeCurrent)
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(peer->outgoingBandwidth);
            else
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(bandwidthLimit);

            enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
        }
    }
}

/** @} */

int ENetHost::receive_incoming_commands(ENetEvent* event)
{
    int packets;

    for (packets = 0; packets < 256; ++packets)
    {
        int receivedLength;
        ENetBuffer buffer;

        buffer.data = packetData[0];
        buffer.dataLength = sizeof(packetData[0]);

        receivedLength = enet_socket_receive(socket,
            &receivedAddress,
            &buffer,
            1);

        if (receivedLength == -2)
            continue;

        if (receivedLength < 0)
            return -1;

        if (receivedLength == 0)
            return 0;

        receivedData = packetData[0];
        receivedDataLength = receivedLength;

        totalReceivedData += receivedLength;
        totalReceivedPackets++;

        if (intercept != nullptr)
        {
            switch (intercept(this, event))
            {
            case 1:
                if (event != nullptr && event->type != ENET_EVENT_TYPE_NONE)
                    return 1;

                continue;

            case -1:
                return -1;

            default:
                break;
            }
        }

        switch (handle_incoming_commands(event))
        {
        case 1:
            return 1;

        case -1:
            return -1;

        default:
            break;
        }
    }

    return 0;
}

int ENetHost::handle_incoming_commands(ENetEvent* event)
{
    ENetProtocolHeader* header;
    ENetProtocol* command;
    ENetPeer* peer;
    enet_uint8* currentData;
    size_t headerSize;
    enet_uint16 peerID, flags;
    enet_uint8 sessionID;

    if (receivedDataLength < ENET_OFFSETOF(ENetProtocolHeader, sentTime))
        return 0;

    header = (ENetProtocolHeader*)receivedData;

    peerID = ENET_NET_TO_HOST_16(header->peerID);
    sessionID = (peerID & ENET_PROTOCOL_HEADER_SESSION_MASK) >> ENET_PROTOCOL_HEADER_SESSION_SHIFT;
    flags = peerID & ENET_PROTOCOL_HEADER_FLAG_MASK;
    peerID &= ~(ENET_PROTOCOL_HEADER_FLAG_MASK | ENET_PROTOCOL_HEADER_SESSION_MASK);

    headerSize = (flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME ? sizeof(ENetProtocolHeader) : ENET_OFFSETOF(ENetProtocolHeader, sentTime));
    if (checksum != nullptr)
        headerSize += sizeof(enet_uint32);

    if (peerID == ENET_PROTOCOL_MAXIMUM_PEER_ID)
        peer = nullptr;
    else
        if (peerID >= peerCount)
            return 0;
        else
        {
            peer = peers[peerID];

            if (peer->state == ENET_PEER_STATE_DISCONNECTED ||
                peer->state == ENET_PEER_STATE_ZOMBIE ||
                ((receivedAddress.host != peer->address.host ||
                    receivedAddress.port != peer->address.port) &&
                    peer->address.host != ENET_HOST_BROADCAST) ||
                (peer->outgoingPeerID < ENET_PROTOCOL_MAXIMUM_PEER_ID &&
                    sessionID != peer->incomingSessionID))
                return 0;
        }

    if (flags & ENET_PROTOCOL_HEADER_FLAG_COMPRESSED)
    {
        size_t originalSize;
        if (compressor.context == nullptr || compressor.decompress == nullptr)
            return 0;

        originalSize = compressor.decompress(compressor.context,
            receivedData + headerSize,
            receivedDataLength - headerSize,
            packetData[1] + headerSize,
            sizeof(packetData[1]) - headerSize);
        if (originalSize <= 0 || originalSize > sizeof(packetData[1]) - headerSize)
            return 0;

        memcpy(packetData[1], header, headerSize);
        receivedData = packetData[1];
        receivedDataLength = headerSize + originalSize;
    }

    if (checksum != nullptr)
    {
        enet_uint32* dataChecksum = (enet_uint32*)&receivedData[headerSize - sizeof(enet_uint32)];
        enet_uint32 desiredChecksum, newChecksum;
        ENetBuffer buffer;
        /* Checksum may be an unaligned pointer, use memcpy to avoid undefined behaviour. */
        memcpy(&desiredChecksum, dataChecksum, sizeof(enet_uint32));

        newChecksum = peer != nullptr ? peer->connectID : 0;
        memcpy(dataChecksum, &newChecksum, sizeof(enet_uint32));

        buffer.data = receivedData;
        buffer.dataLength = receivedDataLength;

        if (checksum(&buffer, 1) != desiredChecksum)
            return 0;
    }

    if (peer != nullptr)
    {
        peer->address.host = receivedAddress.host;
        peer->address.port = receivedAddress.port;
        peer->incomingDataTotal += receivedDataLength;
    }

    currentData = receivedData + headerSize;

    auto commandError = [&]()
    {
        if (event != nullptr && event->type != ENET_EVENT_TYPE_NONE)
            return 1;

        return 0;
    };

    while (currentData < &receivedData[receivedDataLength])
    {
        enet_uint8 commandNumber;
        size_t commandSize;

        command = (ENetProtocol*)currentData;

        if (currentData + sizeof(ENetProtocolCommandHeader) > &receivedData[receivedDataLength])
            break;

        commandNumber = command->header.command & ENET_PROTOCOL_COMMAND_MASK;
        if (commandNumber >= ENET_PROTOCOL_COMMAND_COUNT)
            break;

        commandSize = commandSizes[commandNumber];
        if (commandSize == 0 || currentData + commandSize > &receivedData[receivedDataLength])
            break;

        currentData += commandSize;

        if (peer == nullptr && commandNumber != ENET_PROTOCOL_COMMAND_CONNECT)
            break;

        command->header.reliableSequenceNumber = ENET_NET_TO_HOST_16(command->header.reliableSequenceNumber);

        switch (commandNumber)
        {
        case ENET_PROTOCOL_COMMAND_ACKNOWLEDGE:
            if (handle_acknowledge(event, peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_CONNECT:
            if (peer != nullptr)
                return commandError();
            peer = handle_connect(header, command);
            if (peer == nullptr)
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_VERIFY_CONNECT:
            if (handle_verify_connect(event, peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_DISCONNECT:
            if (handle_disconnect(peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_PING:
            if (handle_ping(peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
            if (handle_send_reliable(peer, command, &currentData))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
            if (handle_send_unreliable(peer, command, &currentData))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
            if (handle_send_unsequenced(peer, command, &currentData))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
            if (handle_send_fragment(peer, command, &currentData))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT:
            if (handle_bandwidth_limit(peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE:
            if (handle_throttle_configure(peer, command))
                return commandError();
            break;

        case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
            if (handle_send_unreliable_fragment(peer, command, &currentData))
                return commandError();
            break;

        default:
            return commandError();
        }

        if (peer != nullptr &&
            (command->header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0)
        {
            enet_uint16 sentTime;

            if (!(flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME))
                break;

            sentTime = ENET_NET_TO_HOST_16(header->sentTime);

            switch (peer->state)
            {
            case ENET_PEER_STATE_DISCONNECTING:
            case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
            case ENET_PEER_STATE_DISCONNECTED:
            case ENET_PEER_STATE_ZOMBIE:
                break;

            case ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT:
                if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_DISCONNECT)
                    enet_peer_queue_acknowledgement(peer, command, sentTime);
                break;

            default:
                enet_peer_queue_acknowledgement(peer, command, sentTime);
                break;
            }
        }
    }
}

bool ENetHost::handle_acknowledge(ENetEvent* event, ENetPeer* peer, const ENetProtocol* command)
{
    enet_uint32 roundTripTime,
        receivedSentTime,
        receivedReliableSequenceNumber;
    ENetProtocolCommand commandNumber;

    if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE)
        return 0;

    receivedSentTime = ENET_NET_TO_HOST_16(command->acknowledge.receivedSentTime);
    receivedSentTime |= serviceTime & 0xFFFF0000;
    if ((receivedSentTime & 0x8000) > (serviceTime & 0x8000))
        receivedSentTime -= 0x10000;

    if (ENET_TIME_LESS(serviceTime, receivedSentTime))
        return 0;

    roundTripTime = ENET_TIME_DIFFERENCE(serviceTime, receivedSentTime);
    roundTripTime = ENET_MAX(roundTripTime, 1);

    if (peer->lastReceiveTime > 0)
    {
        enet_peer_throttle(peer, roundTripTime);

        peer->roundTripTimeVariance -= peer->roundTripTimeVariance / 4;

        if (roundTripTime >= peer->roundTripTime)
        {
            enet_uint32 diff = roundTripTime - peer->roundTripTime;
            peer->roundTripTimeVariance += diff / 4;
            peer->roundTripTime += diff / 8;
        }
        else
        {
            enet_uint32 diff = peer->roundTripTime - roundTripTime;
            peer->roundTripTimeVariance += diff / 4;
            peer->roundTripTime -= diff / 8;
        }
    }
    else
    {
        peer->roundTripTime = roundTripTime;
        peer->roundTripTimeVariance = (roundTripTime + 1) / 2;
    }

    if (peer->roundTripTime < peer->lowestRoundTripTime)
        peer->lowestRoundTripTime = peer->roundTripTime;

    if (peer->roundTripTimeVariance > peer->highestRoundTripTimeVariance)
        peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;

    if (peer->packetThrottleEpoch == 0 ||
        ENET_TIME_DIFFERENCE(serviceTime, peer->packetThrottleEpoch) >= peer->packetThrottleInterval)
    {
        peer->lastRoundTripTime = peer->lowestRoundTripTime;
        peer->lastRoundTripTimeVariance = ENET_MAX(peer->highestRoundTripTimeVariance, 1);
        peer->lowestRoundTripTime = peer->roundTripTime;
        peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;
        peer->packetThrottleEpoch = serviceTime;
    }

    peer->lastReceiveTime = ENET_MAX(serviceTime, 1);
    peer->earliestTimeout = 0;

    receivedReliableSequenceNumber = ENET_NET_TO_HOST_16(command->acknowledge.receivedReliableSequenceNumber);

    commandNumber = peer->RemoveSentReliableCommand(receivedReliableSequenceNumber, command->header.channelID);

    switch (peer->state)
    {
    case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
        if (commandNumber != ENET_PROTOCOL_COMMAND_VERIFY_CONNECT)
            return -1;

        enet_protocol_notify_connect(this, peer, event);
        break;

    case ENET_PEER_STATE_DISCONNECTING:
        if (commandNumber != ENET_PROTOCOL_COMMAND_DISCONNECT)
            return -1;

        enet_protocol_notify_disconnect(this, peer, event);
        break;

    case ENET_PEER_STATE_DISCONNECT_LATER:
        if (!enet_peer_has_outgoing_commands(peer))
            enet_peer_disconnect(peer, peer->eventData);
        break;

    default:
        break;
    }

    return 0;
}

ENetPeer* ENetHost::handle_connect(ENetProtocolHeader* header, ENetProtocol* command)
{
    enet_uint8 incomingSessionID, outgoingSessionID;
    enet_uint32 mtu, windowSize;
    ENetChannel* channel;
    size_t channelCount, duplicatePeers = 0;
    ENetPeer* currentPeer, * peer = nullptr;
    ENetProtocol verifyCommand;

    channelCount = ENET_NET_TO_HOST_32(command->connect.channelCount);

    if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT ||
        channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
        return nullptr;

    for (int i = 0; i < peers.size(); i++)
    {
        currentPeer = peers[i];

        if (currentPeer == nullptr)
            continue;

        if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED)
        {
            if (peer == nullptr)
                peer = currentPeer;
        }
        else
            if (currentPeer->state != ENET_PEER_STATE_CONNECTING &&
                currentPeer->address.host == receivedAddress.host)
            {
                if (currentPeer->address.port == receivedAddress.port &&
                    currentPeer->connectID == command->connect.connectID)
                    return nullptr;

                ++duplicatePeers;
            }
    }

    // duplicatePeer를 허용할것인가?
    if (peer == nullptr || duplicatePeers >= duplicatePeers)
        return nullptr;

    if (channelCount > channelLimit)
        channelCount = channelLimit;
    peer->channels = (ENetChannel*)enet_malloc(channelCount * sizeof(ENetChannel));
    if (peer->channels == nullptr)
        return nullptr;
    peer->channelCount = channelCount;
    peer->state = ENET_PEER_STATE_ACKNOWLEDGING_CONNECT;
    peer->connectID = command->connect.connectID;
    peer->address = receivedAddress;
    peer->mtu = mtu;
    peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->connect.outgoingPeerID);
    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->connect.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->connect.outgoingBandwidth);
    peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->connect.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleDeceleration);
    peer->eventData = ENET_NET_TO_HOST_32(command->connect.data);

    incomingSessionID = command->connect.incomingSessionID == 0xFF ? peer->outgoingSessionID : command->connect.incomingSessionID;
    incomingSessionID = (incomingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);
    if (incomingSessionID == peer->outgoingSessionID)
        incomingSessionID = (incomingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);
    peer->outgoingSessionID = incomingSessionID;

    outgoingSessionID = command->connect.outgoingSessionID == 0xFF ? peer->incomingSessionID : command->connect.outgoingSessionID;
    outgoingSessionID = (outgoingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);
    if (outgoingSessionID == peer->incomingSessionID)
        outgoingSessionID = (outgoingSessionID + 1) & (ENET_PROTOCOL_HEADER_SESSION_MASK >> ENET_PROTOCOL_HEADER_SESSION_SHIFT);
    peer->incomingSessionID = outgoingSessionID;

    for (channel = peer->channels;
        channel < &peer->channels[channelCount];
        ++channel)
    {
        channel->outgoingReliableSequenceNumber = 0;
        channel->outgoingUnreliableSequenceNumber = 0;
        channel->incomingReliableSequenceNumber = 0;
        channel->incomingUnreliableSequenceNumber = 0;

        channel->incomingReliableCommands.clear();
        channel->incomingUnreliableCommands.clear();

        channel->usedReliableWindows = 0;
        memset(channel->reliableWindows, 0, sizeof(channel->reliableWindows));
    }

    mtu = ENET_NET_TO_HOST_32(command->connect.mtu);

    if (mtu < ENET_PROTOCOL_MINIMUM_MTU)
        mtu = ENET_PROTOCOL_MINIMUM_MTU;
    else
        if (mtu > ENET_PROTOCOL_MAXIMUM_MTU)
            mtu = ENET_PROTOCOL_MAXIMUM_MTU;

    if (mtu < peer->mtu)
        peer->mtu = mtu;

    if (outgoingBandwidth == 0 &&
        peer->incomingBandwidth == 0)
        peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    else
        if (outgoingBandwidth == 0 ||
            peer->incomingBandwidth == 0)
            peer->windowSize = (ENET_MAX(outgoingBandwidth, peer->incomingBandwidth) /
                ENET_PEER_WINDOW_SIZE_SCALE) *
            ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
        else
            peer->windowSize = (ENET_MIN(outgoingBandwidth, peer->incomingBandwidth) /
                ENET_PEER_WINDOW_SIZE_SCALE) *
            ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

    if (peer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
        peer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    else
        if (peer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
            peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

    if (incomingBandwidth == 0)
        windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    else
        windowSize = (incomingBandwidth / ENET_PEER_WINDOW_SIZE_SCALE) *
        ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

    if (windowSize > ENET_NET_TO_HOST_32(command->connect.windowSize))
        windowSize = ENET_NET_TO_HOST_32(command->connect.windowSize);

    if (windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
        windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    else
        if (windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
            windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

    verifyCommand.header.command = ENET_PROTOCOL_COMMAND_VERIFY_CONNECT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    verifyCommand.header.channelID = 0xFF;
    verifyCommand.verifyConnect.outgoingPeerID = ENET_HOST_TO_NET_16(peer->incomingPeerID);
    verifyCommand.verifyConnect.incomingSessionID = incomingSessionID;
    verifyCommand.verifyConnect.outgoingSessionID = outgoingSessionID;
    verifyCommand.verifyConnect.mtu = ENET_HOST_TO_NET_32(peer->mtu);
    verifyCommand.verifyConnect.windowSize = ENET_HOST_TO_NET_32(windowSize);
    verifyCommand.verifyConnect.channelCount = ENET_HOST_TO_NET_32(channelCount);
    verifyCommand.verifyConnect.incomingBandwidth = ENET_HOST_TO_NET_32(incomingBandwidth);
    verifyCommand.verifyConnect.outgoingBandwidth = ENET_HOST_TO_NET_32(outgoingBandwidth);
    verifyCommand.verifyConnect.packetThrottleInterval = ENET_HOST_TO_NET_32(peer->packetThrottleInterval);
    verifyCommand.verifyConnect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(peer->packetThrottleAcceleration);
    verifyCommand.verifyConnect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(peer->packetThrottleDeceleration);
    verifyCommand.verifyConnect.connectID = peer->connectID;

    enet_peer_queue_outgoing_command(peer, &verifyCommand, nullptr, 0, 0);

    return peer;
}

bool ENetHost::handle_verify_connect(ENetEvent* event, ENetPeer* peer, const ENetProtocol* command)
{
    enet_uint32 mtu, windowSize;
    size_t channelCount;

    if (peer->state != ENET_PEER_STATE_CONNECTING)
        return 0;

    channelCount = ENET_NET_TO_HOST_32(command->verifyConnect.channelCount);

    if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleInterval) != peer->packetThrottleInterval ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleAcceleration) != peer->packetThrottleAcceleration ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleDeceleration) != peer->packetThrottleDeceleration ||
        command->verifyConnect.connectID != peer->connectID)
    {
        peer->eventData = 0;

        dispatch_state(peer, ENET_PEER_STATE_ZOMBIE);

        return -1;
    }

    peer->RemoveSentReliableCommand(1, 0xFF);

    if (channelCount < peer->channelCount)
        peer->channelCount = channelCount;

    peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->verifyConnect.outgoingPeerID);
    peer->incomingSessionID = command->verifyConnect.incomingSessionID;
    peer->outgoingSessionID = command->verifyConnect.outgoingSessionID;

    mtu = ENET_NET_TO_HOST_32(command->verifyConnect.mtu);

    if (mtu < ENET_PROTOCOL_MINIMUM_MTU)
        mtu = ENET_PROTOCOL_MINIMUM_MTU;
    else
        if (mtu > ENET_PROTOCOL_MAXIMUM_MTU)
            mtu = ENET_PROTOCOL_MAXIMUM_MTU;

    if (mtu < peer->mtu)
        peer->mtu = mtu;

    windowSize = ENET_NET_TO_HOST_32(command->verifyConnect.windowSize);

    if (windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
        windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

    if (windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
        windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

    if (windowSize < peer->windowSize)
        peer->windowSize = windowSize;

    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.outgoingBandwidth);

    enet_protocol_notify_connect(this, peer, event);
    return 0;
}

bool ENetHost::handle_disconnect(ENetPeer* peer, const ENetProtocol* command)
{
    if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE || peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT)
        return 0;

    enet_peer_reset_queues(peer);

    if (peer->state == ENET_PEER_STATE_CONNECTION_SUCCEEDED || peer->state == ENET_PEER_STATE_DISCONNECTING || peer->state == ENET_PEER_STATE_CONNECTING)
        dispatch_state(peer, ENET_PEER_STATE_ZOMBIE);
    else
        if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
        {
            if (peer->state == ENET_PEER_STATE_CONNECTION_PENDING) recalculateBandwidthLimits = 1;

            enet_peer_reset(peer);
        }
        else
            if (command->header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
                change_state(peer, ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT);
            else
                dispatch_state(peer, ENET_PEER_STATE_ZOMBIE);

    if (peer->state != ENET_PEER_STATE_DISCONNECTED)
        peer->eventData = ENET_NET_TO_HOST_32(command->disconnect.data);

    return 0;
}

bool ENetHost::handle_ping(ENetPeer* peer, const ENetProtocol* command)
{
    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
        return -1;

    return 0;
}

bool ENetHost::handle_send_reliable(ENetPeer* peer, const ENetProtocol* command, enet_uint8** currentData)
{
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
        return -1;

    dataLength = ENET_NET_TO_HOST_16(command->sendReliable.dataLength);
    *currentData += dataLength;
    if (dataLength > maximumPacketSize ||
        *currentData < receivedData ||
        *currentData > &receivedData[receivedDataLength])
        return -1;

    if (enet_peer_queue_incoming_command(peer, command, (const enet_uint8*)command + sizeof(ENetProtocolSendReliable), dataLength, ENET_PACKET_FLAG_RELIABLE, 0) == nullptr)
        return -1;

    return 0;
}

bool ENetHost::handle_send_unreliable(ENetPeer* peer, const ENetProtocol* command, enet_uint8** currentData)
{
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
        return -1;

    dataLength = ENET_NET_TO_HOST_16(command->sendUnreliable.dataLength);
    *currentData += dataLength;
    if (dataLength > maximumPacketSize ||
        *currentData < receivedData ||
        *currentData > &receivedData[receivedDataLength])
        return -1;

    if (enet_peer_queue_incoming_command(peer, command, (const enet_uint8*)command + sizeof(ENetProtocolSendUnreliable), dataLength, 0, 0) == NULL)
        return -1;

    return 0;
}

bool ENetHost::handle_send_unsequenced(ENetPeer* peer, const ENetProtocol* command, enet_uint8** currentData)
{
    enet_uint32 unsequencedGroup, index;
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
        return -1;

    dataLength = ENET_NET_TO_HOST_16(command->sendUnsequenced.dataLength);
    *currentData += dataLength;
    if (dataLength > maximumPacketSize ||
        *currentData < receivedData ||
        *currentData > &receivedData[receivedDataLength])
        return -1;

    // unsequenced용 번호같음
    // 단 단순 번호가 아니라, window 방식(비트마스크)를 사용하는듯
    unsequencedGroup = ENET_NET_TO_HOST_16(command->sendUnsequenced.unsequencedGroup);
    index = unsequencedGroup % ENET_PEER_UNSEQUENCED_WINDOW_SIZE;

    if (unsequencedGroup < peer->incomingUnsequencedGroup)
        unsequencedGroup += 0x10000;

    if (unsequencedGroup >= (enet_uint32)peer->incomingUnsequencedGroup + ENET_PEER_FREE_UNSEQUENCED_WINDOWS * ENET_PEER_UNSEQUENCED_WINDOW_SIZE)
        return 0;

    unsequencedGroup &= 0xFFFF;

    if (unsequencedGroup - index != peer->incomingUnsequencedGroup)
    {
        peer->incomingUnsequencedGroup = unsequencedGroup - index;

        memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));
    }
    // 이미 있는 패킷이면 버린다
    else
        if (peer->unsequencedWindow[index / 32] & (1u << (index % 32)))
            return 0;

    if (enet_peer_queue_incoming_command(peer, command, (const enet_uint8*)command + sizeof(ENetProtocolSendUnsequenced), dataLength, ENET_PACKET_FLAG_UNSEQUENCED, 0) == NULL)
        return -1;

    peer->unsequencedWindow[index / 32] |= 1u << (index % 32);

    return 0;
}

bool ENetHost::handle_send_fragment(ENetPeer* peer, const ENetProtocol* command, enet_uint8** currentData)
{
    enet_uint32 fragmentNumber,
        fragmentCount,
        fragmentOffset,
        fragmentLength,
        startSequenceNumber,
        totalLength;
    ENetChannel* channel;
    enet_uint16 startWindow, currentWindow;
    ENetListIterator currentCommand;
    ENetIncomingCommand* startCommand = NULL;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
        return -1;

    fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength <= 0 ||
        fragmentLength > maximumPacketSize ||
        *currentData < receivedData ||
        *currentData > &receivedData[receivedDataLength])
        return -1;

    channel = &peer->channels[command->header.channelID];
    startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);
    startWindow = startSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

    if (startSequenceNumber < channel->incomingReliableSequenceNumber)
        startWindow += ENET_PEER_RELIABLE_WINDOWS;

    if (startWindow < currentWindow || startWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
        return 0;

    fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

    if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT ||
        fragmentNumber >= fragmentCount ||
        totalLength > maximumPacketSize ||
        totalLength < fragmentCount ||
        fragmentOffset >= totalLength ||
        fragmentLength > totalLength - fragmentOffset)
        return -1;

    for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingReliableCommands));
        currentCommand != enet_list_end(&channel->incomingReliableCommands);
        currentCommand = enet_list_previous(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&*currentCommand;

        if (startSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                continue;
        }
        else
            if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
                break;

        if (incomingCommand->reliableSequenceNumber <= startSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < startSequenceNumber)
                break;

            // 이 commnand가 fragment 패킷과 같은 쌍인지 체크 - incomingCommand에서 fragmentCount를 통해 찾음
            if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_FRAGMENT ||
                totalLength != incomingCommand->packet->dataLength ||
                fragmentCount != incomingCommand->fragmentCount)
                return -1;

            startCommand = incomingCommand;
            break;
        }
    }

    if (startCommand == nullptr)
    {
        ENetProtocol hostCommand = *command;

        hostCommand.header.reliableSequenceNumber = startSequenceNumber;

        startCommand = enet_peer_queue_incoming_command(peer, &hostCommand, nullptr, totalLength, ENET_PACKET_FLAG_RELIABLE, fragmentCount);
        if (startCommand == nullptr)
            return -1;
    }

    if ((startCommand->fragments[fragmentNumber / 32] & (1u << (fragmentNumber % 32))) == 0)
    {
        --startCommand->fragmentsRemaining;

        startCommand->fragments[fragmentNumber / 32] |= (1u << (fragmentNumber % 32));

        if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
            fragmentLength = startCommand->packet->dataLength - fragmentOffset;

        memcpy(startCommand->packet->data + fragmentOffset,
            (enet_uint8*)command + sizeof(ENetProtocolSendFragment),
            fragmentLength);

        if (startCommand->fragmentsRemaining <= 0)
            enet_peer_dispatch_incoming_reliable_commands(peer, channel, nullptr);
    }

    return 0;
}

bool ENetHost::handle_bandwidth_limit(ENetPeer* peer, const ENetProtocol* command)
{
    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
        return -1;

    if (peer->incomingBandwidth != 0)
        --bandwidthLimitedPeers;

    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.outgoingBandwidth);

    if (peer->incomingBandwidth != 0)
        ++bandwidthLimitedPeers;

    if (peer->incomingBandwidth == 0 && outgoingBandwidth == 0)
        peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    else
        if (peer->incomingBandwidth == 0 || outgoingBandwidth == 0)
            peer->windowSize = (ENET_MAX(peer->incomingBandwidth, outgoingBandwidth) /
                ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
        else
            peer->windowSize = (ENET_MIN(peer->incomingBandwidth, outgoingBandwidth) /
                ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;

    if (peer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
        peer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    else
        if (peer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
            peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;

    return 0;

}

bool ENetHost::handle_throttle_configure(ENetPeer* peer, const ENetProtocol* command)
{
    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
        return -1;

    peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleDeceleration);

    return 0;
}

bool ENetHost::handle_send_unreliable_fragment(ENetPeer* peer, const ENetProtocol* command, enet_uint8** currentData)
{
    enet_uint32 fragmentNumber,
        fragmentCount,
        fragmentOffset,
        fragmentLength,
        reliableSequenceNumber,
        startSequenceNumber,
        totalLength;
    enet_uint16 reliableWindow, currentWindow;
    ENetChannel* channel;
    ENetListIterator currentCommand;
    ENetIncomingCommand* startCommand = nullptr;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
        return -1;

    fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength <= 0 ||
        fragmentLength > maximumPacketSize ||
        *currentData < receivedData ||
        *currentData > &receivedData[receivedDataLength])
        return -1;

    channel = &peer->channels[command->header.channelID];
    reliableSequenceNumber = command->header.reliableSequenceNumber;
    startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);

    reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

    if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
        reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

    if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
        return 0;

    if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
        startSequenceNumber <= channel->incomingUnreliableSequenceNumber)
        return 0;

    fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

    // 유효성만 체크
    if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT ||
        fragmentNumber >= fragmentCount ||
        totalLength > maximumPacketSize ||
        totalLength < fragmentCount ||
        fragmentOffset >= totalLength ||
        fragmentLength > totalLength - fragmentOffset)
        return -1;

    for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingUnreliableCommands));
        currentCommand != enet_list_end(&channel->incomingUnreliableCommands);
        currentCommand = enet_list_previous(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&*currentCommand;

        if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                continue;
        }
        else
            if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
                break;

        if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
            break;

        if (incomingCommand->reliableSequenceNumber > reliableSequenceNumber)
            continue;

        if (incomingCommand->unreliableSequenceNumber <= startSequenceNumber)
        {
            if (incomingCommand->unreliableSequenceNumber < startSequenceNumber)
                break;

            if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT ||
                totalLength != incomingCommand->packet->dataLength ||
                fragmentCount != incomingCommand->fragmentCount)
                return -1;

            startCommand = incomingCommand;
            break;
        }
    }

    if (startCommand == nullptr)
    {
        startCommand = enet_peer_queue_incoming_command(peer, command, nullptr, totalLength, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT, fragmentCount);
        if (startCommand == nullptr)
            return -1;
    }

    if ((startCommand->fragments[fragmentNumber / 32] & (1u << (fragmentNumber % 32))) == 0)
    {
        --startCommand->fragmentsRemaining;

        startCommand->fragments[fragmentNumber / 32] |= (1u << (fragmentNumber % 32));

        if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
            fragmentLength = startCommand->packet->dataLength - fragmentOffset;

        memcpy(startCommand->packet->data + fragmentOffset,
            (enet_uint8*)command + sizeof(ENetProtocolSendFragment),
            fragmentLength);

        if (startCommand->fragmentsRemaining <= 0)
            enet_peer_dispatch_incoming_unreliable_commands(peer, channel, nullptr);
    }

    return 0;
}
void ENetHost::Initialize()
{
    randomSeed = (enet_uint32)(size_t)this;
    randomSeed += enet_host_random_seed();
    randomSeed = (randomSeed << 16) | (randomSeed >> 16);
    channelLimit = channelLimit;
    incomingBandwidth = incomingBandwidth;
    outgoingBandwidth = outgoingBandwidth;
    bandwidthThrottleEpoch = 0;
    recalculateBandwidthLimits = 0;
    mtu = ENET_HOST_DEFAULT_MTU;
    peerCount = peerCount;
    commandCount = 0;
    bufferCount = 0;
    checksum = nullptr;
    receivedAddress.host = ENET_HOST_ANY;
    receivedAddress.port = 0;
    receivedData = nullptr;
    receivedDataLength = 0;

    totalSentData = 0;
    totalSentPackets = 0;
    totalReceivedData = 0;
    totalReceivedPackets = 0;
    totalQueued = 0;

    connectedPeers = 0;
    bandwidthLimitedPeers = 0;
    duplicatePeers = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
    maximumWaitingData = ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA;

    compressor.context = nullptr;
    compressor.compress = nullptr;
    compressor.decompress = nullptr;
    compressor.destroy = nullptr;

    intercept = nullptr;

    dispatchQueue.clear();

    for (int i = 0; i < peers.size(); i++)
    {
        auto* currentPeer = peers[i];

        currentPeer->host = this;
        currentPeer->incomingPeerID = i;
        currentPeer->outgoingSessionID = currentPeer->incomingSessionID = 0xFF;
        currentPeer->data = nullptr;

        currentPeer->acknowledgements.clear();
        currentPeer->sentReliableCommands.clear();
        currentPeer->outgoingCommands.clear();
        currentPeer->outgoingSendReliableCommands.clear();
        currentPeer->dispatchedCommands.clear();

        enet_peer_reset(currentPeer);
    }
}