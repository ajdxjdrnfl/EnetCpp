#include "enet.h"

ENetHost* ENetService::enet_host_create()
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

    host->Initialize();
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

}
