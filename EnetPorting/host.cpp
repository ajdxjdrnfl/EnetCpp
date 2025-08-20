/**
 @file host.c
 @brief ENet host management functions
*/
#define ENET_BUILDING_LIB 1
#include <string.h>
#include "enet.h"
#include "protocol.h"
#include "callbacks.h"

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

    if (host->compressor.context != NULL && host->compressor.destroy)
        (*host->compressor.destroy) (host->compressor.context);

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
void enet_host_broadcast(ENetHost* host, enet_uint8 channelID, ENetPacket* packet)
{
    ENetPeer* currentPeer;
    
    for (int i = 0; i < host->peers.size(); i++)
    {
        currentPeer = host->peers[i];

        if (currentPeer == nullptr)
            continue;

        if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
            continue;

        enet_peer_send(currentPeer, channelID, packet);
    }

    if (packet->referenceCount == 0)
        enet_packet_destroy(packet);
}

/** Sets the packet compressor the host should use to compress and decompress packets.
    @param host host to enable or disable compression for
    @param compressor callbacks for for the packet compressor; if NULL, then compression is disabled
*/
void enet_host_compress(ENetHost* host, const ENetCompressor* compressor)
{
    if (host->compressor.context != NULL && host->compressor.destroy)
        (*host->compressor.destroy) (host->compressor.context);

    if (compressor)
        host->compressor = *compressor;
    else
        host->compressor.context = NULL;
}

/** Limits the maximum allowed channels of future incoming connections.
    @param host host to limit
    @param channelLimit the maximum number of channels allowed; if 0, then this is equivalent to ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT
*/
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