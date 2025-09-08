/**
 @file  peer.c
 @brief ENet peer management functions
*/
#include <string.h>
#define ENET_BUILDING_LIB 1
#include "utils.h"
#include "enet.h"
#include "protocol.h"
#include "Defines.h"
#include "list.h"

/** @defgroup peer ENet peer functions
    @{
*/

/** Configures throttle parameter for a peer.

    Unreliable packets are dropped by ENet in response to the varying conditions
    of the Internet connection to the peer.  The throttle represents a probability
    that an unreliable packet should not be dropped and thus sent by ENet to the peer.
    The lowest mean round trip time from the sending of a reliable packet to the
    receipt of its acknowledgement is measured over an amount of time specified by
    the interval parameter in milliseconds.  If a measured round trip time happens to
    be significantly less than the mean round trip time measured over the interval,
    then the throttle probability is increased to allow more traffic by an amount
    specified in the acceleration parameter, which is a ratio to the ENET_PEER_PACKET_THROTTLE_SCALE
    constant.  If a measured round trip time happens to be significantly greater than
    the mean round trip time measured over the interval, then the throttle probability
    is decreased to limit traffic by an amount specified in the deceleration parameter, which
    is a ratio to the ENET_PEER_PACKET_THROTTLE_SCALE constant.  When the throttle has
    a value of ENET_PEER_PACKET_THROTTLE_SCALE, no unreliable packets are dropped by
    ENet, and so 100% of all unreliable packets will be sent.  When the throttle has a
    value of 0, all unreliable packets are dropped by ENet, and so 0% of all unreliable
    packets will be sent.  Intermediate values for the throttle represent intermediate
    probabilities between 0% and 100% of unreliable packets being sent.  The bandwidth
    limits of the local and foreign hosts are taken into account to determine a
    sensible limit for the throttle probability above which it should not raise even in
    the best of conditions.

    ENet의 throttle은 **unreliable 패킷을 전송할 확률(0~100%)**을 나타내며,
    RTT(네트워크 지연)에 따라 가속/감속되면서 동적으로 조정되고,
    bandwidth 제한에 맞춰 상한이 걸린다.

    @param peer peer to configure
    @param interval interval, in milliseconds, over which to measure lowest mean RTT; the default value is ENET_PEER_PACKET_THROTTLE_INTERVAL.
    @param acceleration rate at which to increase the throttle probability as mean RTT declines
    @param deceleration rate at which to decrease the throttle probability as mean RTT increases
*/
void enet_peer_throttle_configure(ENetPeer* peer, enet_uint32 interval, enet_uint32 acceleration, enet_uint32 deceleration)
{
    ENetProtocol command;

    peer->packetThrottleInterval = interval;
    peer->packetThrottleAcceleration = acceleration;
    peer->packetThrottleDeceleration = deceleration;

    command.header.command = ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    command.throttleConfigure.packetThrottleInterval = ENET_HOST_TO_NET_32(interval);
    command.throttleConfigure.packetThrottleAcceleration = ENET_HOST_TO_NET_32(acceleration);
    command.throttleConfigure.packetThrottleDeceleration = ENET_HOST_TO_NET_32(deceleration);

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
}

int enet_peer_throttle(ENetPeer* peer, enet_uint32 rtt)
{
    if (peer->lastRoundTripTime <= peer->lastRoundTripTimeVariance)
    {
        peer->packetThrottle = peer->packetThrottleLimit;
    }
    else
        if (rtt <= peer->lastRoundTripTime)
        {
            peer->packetThrottle += peer->packetThrottleAcceleration;

            if (peer->packetThrottle > peer->packetThrottleLimit)
                peer->packetThrottle = peer->packetThrottleLimit;

            return 1;
        }
        else
            if (rtt > peer->lastRoundTripTime + 2 * peer->lastRoundTripTimeVariance)
            {
                if (peer->packetThrottle > peer->packetThrottleDeceleration)
                    peer->packetThrottle -= peer->packetThrottleDeceleration;
                else
                    peer->packetThrottle = 0;

                return -1;
            }

    return 0;
}

/** Queues a packet to be sent.

    On success, ENet will assume ownership of the packet, and so enet_packet_destroy
    should not be called on it thereafter. On failure, the caller still must destroy
    the packet on its own as ENet has not queued the packet. The caller can also
    check the packet's referenceCount field after sending to check if ENet queued
    the packet and thus incremented the referenceCount.

    @param peer destination for the packet
    @param channelID channel on which to send
    @param packet packet to send
    @retval 0 on success
    @retval < 0 on failure
*/
/*
    packet을 command로 만들어줌
*/
int enet_peer_send(ENetPeer* peer, enet_uint8 channelID, std::shared_ptr<ENetPacket> packet)
{
    ENetChannel* channel;
    ENetProtocol command;
    size_t fragmentLength;

    if (peer->state != ENET_PEER_STATE_CONNECTED ||
        channelID >= peer->channelCount ||
        packet->dataLength > peer->host->maximumPacketSize)
        return -1;

    channel = &peer->channels[channelID];
    fragmentLength = peer->mtu - sizeof(ENetProtocolSendFragment);
    if (peer->host->checksum != nullptr)
        fragmentLength -= sizeof(enet_uint32);

    /* 크면 잘라서 보냄 */
    if (packet->dataLength > fragmentLength)
    {
        enet_uint32 fragmentCount = (packet->dataLength + fragmentLength - 1) / fragmentLength,
            fragmentNumber,
            fragmentOffset;
        enet_uint8 commandNumber;
        enet_uint16 startSequenceNumber;
        ENetList fragments;
        ENetOutgoingCommand* fragment;
        
        if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
            return -1;

        if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT)) == ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT &&
            channel->outgoingUnreliableSequenceNumber < 0xFFFF)
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingUnreliableSequenceNumber + 1);
        }
        else
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_FRAGMENT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingReliableSequenceNumber + 1);
        }

        fragments.clear();

        for (fragmentNumber = 0,
            fragmentOffset = 0;
            fragmentOffset < packet->dataLength;
            ++fragmentNumber,
            fragmentOffset += fragmentLength)
        {
            if (packet->dataLength - fragmentOffset < fragmentLength)
                fragmentLength = packet->dataLength - fragmentOffset;

            fragment = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));
            if (fragment == nullptr)
            {
                while (!fragments.empty())
                {

                    fragment = static_cast<ENetOutgoingCommand*>(fragments.remove(fragments.begin()));
                    enet_free(fragment);
                }

                return -1;
            }

            fragment->fragmentOffset = fragmentOffset;
            fragment->fragmentLength = fragmentLength;
            fragment->packet = packet;
            fragment->command.header.command = commandNumber;
            fragment->command.header.channelID = channelID;
            fragment->command.sendFragment.startSequenceNumber = startSequenceNumber;
            fragment->command.sendFragment.dataLength = ENET_HOST_TO_NET_16(fragmentLength);
            fragment->command.sendFragment.fragmentCount = ENET_HOST_TO_NET_32(fragmentCount);
            fragment->command.sendFragment.fragmentNumber = ENET_HOST_TO_NET_32(fragmentNumber);
            fragment->command.sendFragment.totalLength = ENET_HOST_TO_NET_32(packet->dataLength);
            fragment->command.sendFragment.fragmentOffset = ENET_NET_TO_HOST_32(fragmentOffset);

            fragments.insert(fragments.begin(), fragment);
        }

        packet->referenceCount += fragmentNumber;

        while (!fragments.empty())
        {
            fragment = (ENetOutgoingCommand*)fragments.remove(fragments.begin());

            enet_peer_setup_outgoing_command(peer, fragment);
        }

        return 0;
    }

    command.header.channelID = channelID;

    if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED)) == ENET_PACKET_FLAG_UNSEQUENCED)
    {
        command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.sendUnsequenced.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
    }
    else
        if (packet->flags & ENET_PACKET_FLAG_RELIABLE || channel->outgoingUnreliableSequenceNumber >= 0xFFFF)
        {
            command.header.command = ENET_PROTOCOL_COMMAND_SEND_RELIABLE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            command.sendReliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
        }
        else
        {
            command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE;
            command.sendUnreliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
        }

    if (enet_peer_queue_outgoing_command(peer, &command, packet, 0, packet->dataLength) == NULL)
        return -1;

    return 0;
}

/** Attempts to dequeue any incoming queued packet.
    @param peer peer to dequeue packets from
    @param channelID holds the channel ID of the channel the packet was received on success
    @returns a pointer to the packet, or NULL if there are no available incoming queued packets
*/

std::shared_ptr<ENetPacket> ENetPeer::OnReceive(enet_uint8* channelID)
{
    ENetIncomingCommand* incomingCommand;

    if (dispatchedCommands.empty())
        return nullptr;

    incomingCommand = reinterpret_cast<ENetIncomingCommand*>(dispatchedCommands.remove(dispatchedCommands.begin()));

    if (channelID != nullptr)
        *channelID = incomingCommand->command.header.channelID;

    auto& packet = incomingCommand->packet;

    if (packet == nullptr)
    {
        ASSERT(false);
        return nullptr;
    }

    if (incomingCommand->fragments != nullptr)
        enet_free(incomingCommand->fragments);

    enet_free(incomingCommand);

    totalWaitingData -= ENET_MIN(totalWaitingData, packet->dataLength);

    return packet;
}

int ENetPeer::SendPacket(enet_uint8 channelID, std::shared_ptr<ENetPacket> packet)
{
    ENetChannel* channel;
    ENetProtocol command;
    size_t fragmentLength;

    if (state != ENET_PEER_STATE_CONNECTED ||
        channelID >= channelCount ||
        packet->dataLength > host->maximumPacketSize)
        return -1;

    channel = &channels[channelID];
    fragmentLength = mtu - sizeof(ENetProtocolSendFragment);
    if (host->checksum != nullptr)
        fragmentLength -= sizeof(enet_uint32);

    /* 크면 잘라서 보냄 */
    if (packet->dataLength > fragmentLength)
    {
        enet_uint32 fragmentCount = (packet->dataLength + fragmentLength - 1) / fragmentLength,
            fragmentNumber,
            fragmentOffset;
        enet_uint8 commandNumber;
        enet_uint16 startSequenceNumber;
        ENetList fragments;
        ENetOutgoingCommand* fragment;

        if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
            return -1;

        if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT)) == ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT &&
            channel->outgoingUnreliableSequenceNumber < 0xFFFF)
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingUnreliableSequenceNumber + 1);
        }
        else
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_FRAGMENT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingReliableSequenceNumber + 1);
        }

        fragments.clear();

        for (fragmentNumber = 0,
            fragmentOffset = 0;
            fragmentOffset < packet->dataLength;
            ++fragmentNumber,
            fragmentOffset += fragmentLength)
        {
            if (packet->dataLength - fragmentOffset < fragmentLength)
                fragmentLength = packet->dataLength - fragmentOffset;

            fragment = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));
            if (fragment == nullptr)
            {
                while (!fragments.empty())
                {

                    fragment = static_cast<ENetOutgoingCommand*>(fragments.remove(fragments.begin()));
                    enet_free(fragment);
                }

                return -1;
            }

            fragment->fragmentOffset = fragmentOffset;
            fragment->fragmentLength = fragmentLength;
            fragment->packet = packet;
            fragment->command.header.command = commandNumber;
            fragment->command.header.channelID = channelID;
            fragment->command.sendFragment.startSequenceNumber = startSequenceNumber;
            fragment->command.sendFragment.dataLength = ENET_HOST_TO_NET_16(fragmentLength);
            fragment->command.sendFragment.fragmentCount = ENET_HOST_TO_NET_32(fragmentCount);
            fragment->command.sendFragment.fragmentNumber = ENET_HOST_TO_NET_32(fragmentNumber);
            fragment->command.sendFragment.totalLength = ENET_HOST_TO_NET_32(packet->dataLength);
            fragment->command.sendFragment.fragmentOffset = ENET_NET_TO_HOST_32(fragmentOffset);

            fragments.insert(fragments.begin(), fragment);
        }

        packet->referenceCount += fragmentNumber;

        while (!fragments.empty())
        {
            fragment = (ENetOutgoingCommand*)fragments.remove(fragments.begin());

            SetupOutgoingCommand(fragment);
        }

        return 0;
    }

    command.header.channelID = channelID;

    if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED)) == ENET_PACKET_FLAG_UNSEQUENCED)
    {
        command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.sendUnsequenced.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
    }
    else
        if (packet->flags & ENET_PACKET_FLAG_RELIABLE || channel->outgoingUnreliableSequenceNumber >= 0xFFFF)
        {
            command.header.command = ENET_PROTOCOL_COMMAND_SEND_RELIABLE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            command.sendReliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
        }
        else
        {
            command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE;
            command.sendUnreliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
        }

    if (QueueOutgoingCommand(&command, packet, 0, packet->dataLength) == nullptr)
        return -1;

    return 0;
}

void ENetPeer::SendPing()
{
    ENetProtocol command;

    if (state != ENET_PEER_STATE_CONNECTED)
        return;

    command.header.command = ENET_PROTOCOL_COMMAND_PING | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    QueueOutgoingCommand(&command, nullptr, 0, 0);
}

void ENetPeer::SetPingInterval(enet_uint32 newPingInterval)
{
    pingInterval = newPingInterval ? newPingInterval : ENET_PEER_PING_INTERVAL;
}

void ENetPeer::SetupOutgoingCommand(ENetOutgoingCommand* outgoingCommand)
{
    /* 헤더 + 데이터그램 */
    outgoingDataTotal += enet_protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

    /* channelIID == 0xFF ?*/
    if (outgoingCommand->command.header.channelID == 0xFF)
    {
        ++outgoingReliableSequenceNumber;

        outgoingCommand->reliableSequenceNumber = outgoingReliableSequenceNumber;
        outgoingCommand->unreliableSequenceNumber = 0;
    }
    else
    {
        ENetChannel* channel = &channels[outgoingCommand->command.header.channelID];

        // Acknowldege 패킷이면 channel, command의 reliableSequenceNumber를 세팅
        if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            ++channel->outgoingReliableSequenceNumber;
            channel->outgoingUnreliableSequenceNumber = 0;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else
            if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED)
            {
                ++outgoingUnsequencedGroup;

                outgoingCommand->reliableSequenceNumber = 0;
                outgoingCommand->unreliableSequenceNumber = 0;
            }
            else
            {
                if (outgoingCommand->fragmentOffset == 0)
                    ++channel->outgoingUnreliableSequenceNumber;

                outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
                outgoingCommand->unreliableSequenceNumber = channel->outgoingUnreliableSequenceNumber;
            }
    }

    outgoingCommand->sendAttempts = 0;
    outgoingCommand->sentTime = 0;
    outgoingCommand->roundTripTimeout = 0;
    outgoingCommand->command.header.reliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);
    outgoingCommand->queueTime = ++host->totalQueued;

    switch (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        outgoingCommand->command.sendUnreliable.unreliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        outgoingCommand->command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(outgoingUnsequencedGroup);
        break;

    default:
        break;
    }

    if ((outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0 &&
        outgoingCommand->packet != nullptr)
        outgoingSendReliableCommands.insert(outgoingSendReliableCommands.end(), outgoingCommand);
    else
        outgoingCommands.insert(outgoingCommands.end(), outgoingCommand);
}

/** Forcefully disconnects a peer.
    @param peer peer to forcefully disconnect
    @remarks The foreign host represented by the peer is not notified of the disconnection and will timeout
    on its connection to the local host.

    peer를 강제종료?
*/
void ENetPeer::Reset()
{
    OnDisconnect();

    outgoingPeerID = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    connectID = 0;

    state = ENET_PEER_STATE_DISCONNECTED;

    incomingBandwidth = 0;
    outgoingBandwidth = 0;
    incomingBandwidthThrottleEpoch = 0;
    outgoingBandwidthThrottleEpoch = 0;
    incomingDataTotal = 0;
    outgoingDataTotal = 0;
    lastSendTime = 0;
    lastReceiveTime = 0;
    nextTimeout = 0;
    earliestTimeout = 0;
    packetLossEpoch = 0;
    packetsSent = 0;
    packetsLost = 0;
    packetLoss = 0;
    packetLossVariance = 0;
    packetThrottle = ENET_PEER_DEFAULT_PACKET_THROTTLE;
    packetThrottleLimit = ENET_PEER_PACKET_THROTTLE_SCALE;
    packetThrottleCounter = 0;
    packetThrottleEpoch = 0;
    packetThrottleAcceleration = ENET_PEER_PACKET_THROTTLE_ACCELERATION;
    packetThrottleDeceleration = ENET_PEER_PACKET_THROTTLE_DECELERATION;
    packetThrottleInterval = ENET_PEER_PACKET_THROTTLE_INTERVAL;
    pingInterval = ENET_PEER_PING_INTERVAL;
    timeoutLimit = ENET_PEER_TIMEOUT_LIMIT;
    timeoutMinimum = ENET_PEER_TIMEOUT_MINIMUM;
    timeoutMaximum = ENET_PEER_TIMEOUT_MAXIMUM;
    lastRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    lowestRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    lastRoundTripTimeVariance = 0;
    highestRoundTripTimeVariance = 0;
    roundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    roundTripTimeVariance = 0;
    mtu = host->mtu;
    reliableDataInTransit = 0;
    outgoingReliableSequenceNumber = 0;
    windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    incomingUnsequencedGroup = 0;
    outgoingUnsequencedGroup = 0;
    eventData = 0;
    totalWaitingData = 0;
    flags = 0;

    memset(unsequencedWindow, 0, sizeof(unsequencedWindow));

    ResetQueues();
}

void ENetPeer::ResetQueues()
{
    ENetChannel* channel;

    if (flags & ENET_PEER_FLAG_NEEDS_DISPATCH)
    {
        enet_list_remove(&dispatchList);

        flags &= ~ENET_PEER_FLAG_NEEDS_DISPATCH;
    }

    while (!acknowledgements.empty())
        enet_free(acknowledgements.remove(acknowledgements.begin()));

    ResetOutgoingCommands(&sentReliableCommands);
    ResetOutgoingCommands(&outgoingCommands);
    ResetOutgoingCommands(&outgoingSendReliableCommands);
    ResetOutgoingCommands(&dispatchedCommands);

    if (channels != nullptr && channelCount > 0)
    {
        for (channel = channels;
            channel < &channels[channelCount];
            ++channel)
        {
            ResetIncomingCommands(&channel->incomingReliableCommands);
            ResetIncomingCommands(&channel->incomingUnreliableCommands);
        }

        enet_free(channels);
    }

    channels = nullptr;
    channelCount = 0;
}

void ENetPeer::ResetOutgoingCommands(ENetList* queue)
{
    ENetOutgoingCommand* outgoingCommand;

    while (!queue->empty())
    {
        outgoingCommand = (ENetOutgoingCommand*)queue->remove(queue->begin());

        enet_free(outgoingCommand);
    }
}

ENetOutgoingCommand* ENetPeer::QueueOutgoingCommand(const ENetProtocol* command, std::shared_ptr<ENetPacket> packet, enet_uint32 offset, enet_uint16 length)
{
    ENetOutgoingCommand* outgoingCommand = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));
    if (outgoingCommand == nullptr)
        return nullptr;

    outgoingCommand->command = *command;
    outgoingCommand->fragmentOffset = offset;
    outgoingCommand->fragmentLength = length;
    outgoingCommand->packet = packet;

    SetupOutgoingCommand(outgoingCommand);

    return outgoingCommand;
}

ENetIncomingCommand* ENetPeer::QueueIncomingCommand(const ENetProtocol* command, const void* data, size_t dataLength, enet_uint32 flags, enet_uint32 fragmentCount)
{
    static ENetIncomingCommand dummyCommand;

    ENetChannel* channel = &channels[command->header.channelID];
    enet_uint32 unreliableSequenceNumber = 0, reliableSequenceNumber = 0;
    enet_uint16 reliableWindow, currentWindow;
    ENetIncomingCommand* incomingCommand;
    ENetListIterator currentCommand;
    std::shared_ptr<ENetPacket> packet = nullptr;

    auto notifyError = [&]()
    {
        if (packet != nullptr && packet->referenceCount == 0)
            enet_packet_destroy(packet);

        return (ENetIncomingCommand*)nullptr;
    };

    auto discardCommand = [&]()
    {
        if (fragmentCount > 0)
            return notifyError();

        if (packet != nullptr && packet->referenceCount == 0)
            enet_packet_destroy(packet);

        return &dummyCommand;
    };

    if (state == ENET_PEER_STATE_DISCONNECT_LATER)
        return discardCommand();

    // unsequenced가 아니라면 seqnumber를 체크한다
    if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
    {
        reliableSequenceNumber = command->header.reliableSequenceNumber;
        reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
        currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

        if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
            return discardCommand();
    }

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber)
            return discardCommand();

        for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingReliableCommands));
            currentCommand != enet_list_end(&channel->incomingReliableCommands);
            currentCommand = enet_list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

            if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
            {
                if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                    continue;
            }
            else
                if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
                    break;

            if (incomingCommand->reliableSequenceNumber <= reliableSequenceNumber)
            {
                if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
                    break;

                return discardCommand();
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        unreliableSequenceNumber = ENET_NET_TO_HOST_16(command->sendUnreliable.unreliableSequenceNumber);

        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
            unreliableSequenceNumber <= channel->incomingUnreliableSequenceNumber)
            return discardCommand();

        for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingUnreliableCommands));
            currentCommand != enet_list_end(&channel->incomingUnreliableCommands);
            currentCommand = enet_list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

            if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
                continue;

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

            if (incomingCommand->unreliableSequenceNumber <= unreliableSequenceNumber)
            {
                if (incomingCommand->unreliableSequenceNumber < unreliableSequenceNumber)
                    break;

                return discardCommand();
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        currentCommand = enet_list_end(&channel->incomingUnreliableCommands);
        break;

    default:
        return discardCommand();
    }

    if (totalWaitingData >= host->maximumWaitingData)
        return notifyError();

    packet = enet_packet_create(data, dataLength, flags);
    if (packet == nullptr)
        return notifyError();

    incomingCommand = (ENetIncomingCommand*)enet_malloc(sizeof(ENetIncomingCommand));
    if (incomingCommand == nullptr)
        return notifyError();

    incomingCommand->reliableSequenceNumber = command->header.reliableSequenceNumber;
    incomingCommand->unreliableSequenceNumber = unreliableSequenceNumber & 0xFFFF;
    incomingCommand->command = *command;
    incomingCommand->fragmentCount = fragmentCount;
    incomingCommand->fragmentsRemaining = fragmentCount;
    incomingCommand->packet = packet;
    incomingCommand->fragments = nullptr;

    if (fragmentCount > 0)
    {
        if (fragmentCount <= ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
            incomingCommand->fragments = (enet_uint32*)enet_malloc((fragmentCount + 31) / 32 * sizeof(enet_uint32));
        if (incomingCommand->fragments == nullptr)
        {
            enet_free(incomingCommand);

            return notifyError();
        }
        memset(incomingCommand->fragments, 0, (fragmentCount + 31) / 32 * sizeof(enet_uint32));
    }

    if (packet != nullptr)
    {
        ++packet->referenceCount;

        totalWaitingData += packet->dataLength;
    }

    enet_list_insert(enet_list_next(currentCommand), incomingCommand);

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        DispatchIncomingReliableCommands(channel, incomingCommand);
        break;

    default:
        DispatchIncomingUnReliableCommands(channel, incomingCommand);
        break;
    }

    return incomingCommand;
}

void ENetPeer::SetupOutgoingCommand(ENetOutgoingCommand* outgoingCommand)
{
    /* 헤더 + 데이터그램 */
    outgoingDataTotal += enet_protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

    /* channelIID == 0xFF ?*/
    if (outgoingCommand->command.header.channelID == 0xFF)
    {
        ++outgoingReliableSequenceNumber;

        outgoingCommand->reliableSequenceNumber = outgoingReliableSequenceNumber;
        outgoingCommand->unreliableSequenceNumber = 0;
    }
    else
    {
        ENetChannel* channel = &channels[outgoingCommand->command.header.channelID];

        // Acknowldege 패킷이면 channel, command의 reliableSequenceNumber를 세팅
        if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            ++channel->outgoingReliableSequenceNumber;
            channel->outgoingUnreliableSequenceNumber = 0;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED)
        {
                ++outgoingUnsequencedGroup;

                outgoingCommand->reliableSequenceNumber = 0;
                outgoingCommand->unreliableSequenceNumber = 0;
        }
        else
        {
            if (outgoingCommand->fragmentOffset == 0)
                ++channel->outgoingUnreliableSequenceNumber;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = channel->outgoingUnreliableSequenceNumber;
        }
    }

    outgoingCommand->sendAttempts = 0;
    outgoingCommand->sentTime = 0;
    outgoingCommand->roundTripTimeout = 0;
    outgoingCommand->command.header.reliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);
    outgoingCommand->queueTime = ++host->totalQueued;

    switch (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        outgoingCommand->command.sendUnreliable.unreliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        outgoingCommand->command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(outgoingUnsequencedGroup);
        break;

    default:
        break;
    }

    // Ack 패킷을 보낸다면, sendReliable에 넣어준다
    if ((outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0 &&
        outgoingCommand->packet != nullptr)
        outgoingSendReliableCommands.insert(outgoingSendReliableCommands.end(), outgoingCommand);
    else
        outgoingCommands.insert(outgoingCommands.end(), outgoingCommand);
}

void ENetPeer::ResetIncomingCommands(ENetList* queue)
{
    RemoveIncomingCommands(queue, queue->begin(), queue->end(), nullptr);
}

void ENetPeer::RemoveIncomingCommands(ENetList* queue, ENetList::iterator startCommand, ENetList::iterator endCommand, ENetIncomingCommand* excludeCommand)
{
    ENetList::iterator currentCommand;

    for (currentCommand = startCommand; currentCommand != endCommand; )
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

        currentCommand = enet_list_next(currentCommand);

        if (incomingCommand == excludeCommand)
            continue;

        enet_list_remove(&incomingCommand->incomingCommandList);

        if (incomingCommand->fragments != nullptr)
            enet_free(incomingCommand->fragments);

        enet_free(incomingCommand);
    }
}

ENetProtocolCommand ENetPeer::RemoveSentReliableCommand(enet_uint16 reliableSequenceNumber, enet_uint8 channelID)
{
    ENetOutgoingCommand* outgoingCommand = nullptr;
    ENetListIterator currentCommand;
    ENetProtocolCommand commandNumber;
    int wasSent = 1;

    for (currentCommand = enet_list_begin(&sentReliableCommands);
        currentCommand != enet_list_end(&sentReliableCommands);
        currentCommand = enet_list_next(currentCommand))
    {
        outgoingCommand = (ENetOutgoingCommand*)&*currentCommand;

        if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber &&
            outgoingCommand->command.header.channelID == channelID)
            break;
    }

    if (currentCommand == enet_list_end(&sentReliableCommands))
    {
        outgoingCommand = FindSentReliableCommand(&outgoingCommands, reliableSequenceNumber, channelID);
        if (outgoingCommand == nullptr)
            outgoingCommand = FindSentReliableCommand(&outgoingSendReliableCommands, reliableSequenceNumber, channelID);

        wasSent = 0;
    }

    if (outgoingCommand == nullptr)
        return ENET_PROTOCOL_COMMAND_NONE;

    if (channelID < channelCount)
    {
        ENetChannel* channel = &channels[channelID];
        enet_uint16 reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
        if (channel->reliableWindows[reliableWindow] > 0)
        {
            --channel->reliableWindows[reliableWindow];
            if (!channel->reliableWindows[reliableWindow])
                channel->usedReliableWindows &= ~(1u << reliableWindow);
        }
    }

    commandNumber = (ENetProtocolCommand)(outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK);

    enet_list_remove(&outgoingCommand->outgoingCommandList);

    if (outgoingCommand->packet != nullptr)
    {
        if (wasSent)
            reliableDataInTransit -= outgoingCommand->fragmentLength;

        --outgoingCommand->packet->referenceCount;

        if (outgoingCommand->packet->referenceCount == 0)
        {
            outgoingCommand->packet->flags |= ENET_PACKET_FLAG_SENT;

            enet_packet_destroy(outgoingCommand->packet);
        }
    }

    enet_free(outgoingCommand);

    if (enet_list_empty(&sentReliableCommands))
        return commandNumber;

    outgoingCommand = (ENetOutgoingCommand*)enet_list_front(&sentReliableCommands);

    nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;

    return commandNumber;
}

ENetOutgoingCommand* ENetPeer::FindSentReliableCommand(ENetList* list, enet_uint16 reliableSequenceNumber, enet_uint8 channelID)
{
    ENetListIterator currentCommand;

    for (currentCommand = enet_list_begin(list);
        currentCommand != enet_list_end(list);
        currentCommand = enet_list_next(currentCommand))
    {
        ENetOutgoingCommand* outgoingCommand = (ENetOutgoingCommand*)&*currentCommand;

        if (!(outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE))
            continue;

        if (outgoingCommand->sendAttempts < 1)
            break;

        if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber &&
            outgoingCommand->command.header.channelID == channelID)
            return outgoingCommand;
    }

    return nullptr;
}

void ENetPeer::OnConnect()
{
    if (state != ENET_PEER_STATE_CONNECTED && state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        if (incomingBandwidth != 0)
            ++host->bandwidthLimitedPeers;

        ++host->connectedPeers;
    }
}

void ENetPeer::OnDisconnect()
{
    if (state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        if (incomingBandwidth != 0)
            --host->bandwidthLimitedPeers;

        --host->connectedPeers;
    }
}

/** Sends a ping request to a peer.
    @param peer destination for the ping request
    @remarks ping requests factor into the mean round trip time as designated by the
    roundTripTime field in the ENetPeer structure.  ENet automatically pings all connected
    peers at regular intervals, however, this function may be called to ensure more
    frequent ping requests.

    udp에서 ping은 생존 확인용
*/
void enet_peer_ping(ENetPeer* peer)
{
    ENetProtocol command;

    if (peer->state != ENET_PEER_STATE_CONNECTED)
        return;

    command.header.command = ENET_PROTOCOL_COMMAND_PING | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
}

/** Sets the interval at which pings will be sent to a peer.

    Pings are used both to monitor the liveness of the connection and also to dynamically
    adjust the throttle during periods of low traffic so that the throttle has reasonable
    responsiveness during traffic spikes.

    @param peer the peer to adjust
    @param pingInterval the interval at which to send pings; defaults to ENET_PEER_PING_INTERVAL if 0
*/
void enet_peer_ping_interval(ENetPeer* peer, enet_uint32 pingInterval)
{
    peer->pingInterval = pingInterval ? pingInterval : ENET_PEER_PING_INTERVAL;
}

/** Sets the timeout parameters for a peer.

    The timeout parameter control how and when a peer will timeout from a failure to acknowledge
    reliable traffic. Timeout values use an exponential backoff mechanism, where if a reliable
    packet is not acknowledge within some multiple of the average RTT plus a variance tolerance,
    the timeout will be doubled until it reaches a set limit. If the timeout is thus at this
    limit and reliable packets have been sent but not acknowledged within a certain minimum time
    period, the peer will be disconnected. Alternatively, if reliable packets have been sent
    but not acknowledged for a certain maximum time period, the peer will be disconnected regardless
    of the current timeout limit value.

    @param peer the peer to adjust
    @param timeoutLimit the timeout limit; defaults to ENET_PEER_TIMEOUT_LIMIT if 0
    @param timeoutMinimum the timeout minimum; defaults to ENET_PEER_TIMEOUT_MINIMUM if 0
    @param timeoutMaximum the timeout maximum; defaults to ENET_PEER_TIMEOUT_MAXIMUM if 0
*/

void ENetPeer::SetTimeout(enet_uint32 timeoutLimit, enet_uint32 timeoutMinimum, enet_uint32 timeoutMaximum)
{
    this->timeoutLimit = timeoutLimit ? timeoutLimit : ENET_PEER_TIMEOUT_LIMIT;
    this->timeoutMinimum = timeoutMinimum ? timeoutMinimum : ENET_PEER_TIMEOUT_MINIMUM;
    this->timeoutMaximum = timeoutMaximum ? timeoutMaximum : ENET_PEER_TIMEOUT_MAXIMUM;
}

void enet_peer_timeout(ENetPeer* peer, enet_uint32 timeoutLimit, enet_uint32 timeoutMinimum, enet_uint32 timeoutMaximum)
{
    peer->timeoutLimit = timeoutLimit ? timeoutLimit : ENET_PEER_TIMEOUT_LIMIT;
    peer->timeoutMinimum = timeoutMinimum ? timeoutMinimum : ENET_PEER_TIMEOUT_MINIMUM;
    peer->timeoutMaximum = timeoutMaximum ? timeoutMaximum : ENET_PEER_TIMEOUT_MAXIMUM;
}

/** Force an immediate disconnection from a peer.
    @param peer peer to disconnect
    @param data data describing the disconnection
    @remarks No ENET_EVENT_DISCONNECT event will be generated. The foreign peer is not
    guaranteed to receive the disconnect notification, and is reset immediately upon
    return from this function.
*/

void ENetPeer::DisconnectNow(enet_uint32 data)
{
    ENetProtocol command;

    if (state == ENET_PEER_STATE_DISCONNECTED)
        return;

    if (state != ENET_PEER_STATE_ZOMBIE &&
        state != ENET_PEER_STATE_DISCONNECTING)
    {
        ResetQueues();

        command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.header.channelID = 0xFF;
        command.disconnect.data = ENET_HOST_TO_NET_32(data);

        QueueOutgoingCommand(&command, nullptr, 0, 0);

        host->flush();
    }

    Reset();
}

void ENetPeer::Disconnect(enet_uint32 data)
{
    ENetProtocol command;

    if (state == ENET_PEER_STATE_DISCONNECTING ||
        state == ENET_PEER_STATE_DISCONNECTED ||
        state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT ||
        state == ENET_PEER_STATE_ZOMBIE)
        return;

    ResetQueues();

    command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT;
    command.header.channelID = 0xFF;
    command.disconnect.data = ENET_HOST_TO_NET_32(data);

    if (state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER)
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    else
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;

    QueueOutgoingCommand(&command, nullptr, 0, 0);

    if (state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        OnDisconnect();

        state = ENET_PEER_STATE_DISCONNECTING;
    }
    else
    {
        host->flush();
        Reset();
    }
}

bool ENetPeer::HasOutgoingCommands()
{
    if (outgoingCommands.empty() &&
        outgoingSendReliableCommands.empty() &&
        sentReliableCommands.empty())
        return false;

    return true;
}

void enet_peer_disconnect_now(ENetPeer* peer, enet_uint32 data)
{
    ENetProtocol command;

    if (peer->state == ENET_PEER_STATE_DISCONNECTED)
        return;

    if (peer->state != ENET_PEER_STATE_ZOMBIE &&
        peer->state != ENET_PEER_STATE_DISCONNECTING)
    {
        enet_peer_reset_queues(peer);

        command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.header.channelID = 0xFF;
        command.disconnect.data = ENET_HOST_TO_NET_32(data);

        enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);

        enet_host_flush(peer->host);
    }

    enet_peer_reset(peer);
}

/** Request a disconnection from a peer.
    @param peer peer to request a disconnection
    @param data data describing the disconnection
    @remarks An ENET_EVENT_DISCONNECT event will be generated by enet_host_service()
    once the disconnection is complete.
*/
void enet_peer_disconnect(ENetPeer* peer, enet_uint32 data)
{
    ENetProtocol command;

    if (peer->state == ENET_PEER_STATE_DISCONNECTING ||
        peer->state == ENET_PEER_STATE_DISCONNECTED ||
        peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT ||
        peer->state == ENET_PEER_STATE_ZOMBIE)
        return;

    enet_peer_reset_queues(peer);

    command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT;
    command.header.channelID = 0xFF;
    command.disconnect.data = ENET_HOST_TO_NET_32(data);

    if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    else
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);

    if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        enet_peer_on_disconnect(peer);

        peer->state = ENET_PEER_STATE_DISCONNECTING;
    }
    else
    {
        enet_host_flush(peer->host);
        enet_peer_reset(peer);
    }
}

int enet_peer_has_outgoing_commands(ENetPeer* peer)
{
    if (peer->outgoingCommands.empty() &&
        peer->outgoingSendReliableCommands.empty() &&
        peer->sentReliableCommands.empty())
        return 0;

    return 1;
}

/** Request a disconnection from a peer, but only after all queued outgoing packets are sent.
    @param peer peer to request a disconnection
    @param data data describing the disconnection
    @remarks An ENET_EVENT_DISCONNECT event will be generated by enet_host_service()
    once the disconnection is complete.

    쌓인 큐는 전부 보내고 disconnet
*/

void ENetPeer::DisconnectLater(enet_uint32 data)
{
    if ((state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER) &&
        HasOutgoingCommands())
    {
        state = ENET_PEER_STATE_DISCONNECT_LATER;
        eventData = data;
    }
    else
        Disconnect(data);
}

void enet_peer_disconnect_later(ENetPeer* peer, enet_uint32 data)
{
    if ((peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER) &&
        enet_peer_has_outgoing_commands(peer))
    {
        peer->state = ENET_PEER_STATE_DISCONNECT_LATER;
        peer->eventData = data;
    }
    else
        enet_peer_disconnect(peer, data);
}

// 보낼 ack 패킷을 만들어서 넣어준다
//
ENetAcknowledgement* ENetPeer::QueueAcknowledgement(const ENetProtocol* command, enet_uint16 sentTime)
{
    ENetAcknowledgement* acknowledgement;

    if (command->header.channelID < channelCount)
    {
        ENetChannel* channel = &channels[command->header.channelID];
        // 현재 들어온 command의 sequenceNumber
        enet_uint16 reliableWindow = command->header.reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
            // 현재 channel이 기대하는 sequenceNumber
            currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        // wrap-around?
        if (command->header.reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

        // 허용된 윈도우의 범위내의 command인가?
        // 너무 멀다면 이상한 패킷이다
        if (reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1 && reliableWindow <= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS)
            return nullptr;
    }

    acknowledgement = (ENetAcknowledgement*)enet_malloc(sizeof(ENetAcknowledgement));
    if (acknowledgement == nullptr)
        return nullptr;

    outgoingDataTotal += sizeof(ENetProtocolAcknowledge);

    acknowledgement->sentTime = sentTime;
    acknowledgement->command = *command;

    acknowledgements.insert(acknowledgements.end(), acknowledgement);

    return acknowledgement;
}

ENetAcknowledgement* enet_peer_queue_acknowledgement(ENetPeer* peer, const ENetProtocol* command, enet_uint16 sentTime)
{
    ENetAcknowledgement* acknowledgement;

    if (command->header.channelID < peer->channelCount)
    {
        ENetChannel* channel = &peer->channels[command->header.channelID];
        // 현재 들어온 command의 sequenceNumber
        enet_uint16 reliableWindow = command->header.reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
            // 현재 channel이 기대하는 sequenceNumber
            currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        // wrap-around?
        if (command->header.reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

        // 허용된 윈도우의 범위내의 command인가?
        // 너무 멀다면 이상한 패킷이다
        if (reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1 && reliableWindow <= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS)
            return nullptr;
    }

    acknowledgement = (ENetAcknowledgement*)enet_malloc(sizeof(ENetAcknowledgement));
    if (acknowledgement == nullptr)
        return nullptr;

    peer->outgoingDataTotal += sizeof(ENetProtocolAcknowledge);

    acknowledgement->sentTime = sentTime;
    acknowledgement->command = *command;

    peer->acknowledgements.insert(peer->acknowledgements.end(), acknowledgement);

    return acknowledgement;
}

// 송신용 command 세팅
void enet_peer_setup_outgoing_command(ENetPeer* peer, ENetOutgoingCommand* outgoingCommand)
{
    /* 헤더 + 데이터그램 */
    peer->outgoingDataTotal += enet_protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

    /* channelIID == 0xFF ?*/
    if (outgoingCommand->command.header.channelID == 0xFF)
    {
        ++peer->outgoingReliableSequenceNumber;

        outgoingCommand->reliableSequenceNumber = peer->outgoingReliableSequenceNumber;
        outgoingCommand->unreliableSequenceNumber = 0;
    }
    else
    {
        ENetChannel* channel = &peer->channels[outgoingCommand->command.header.channelID];

        // Acknowldege 패킷이면 channel, command의 reliableSequenceNumber를 세팅
        if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            ++channel->outgoingReliableSequenceNumber;
            channel->outgoingUnreliableSequenceNumber = 0;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else
            if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED)
            {
                ++peer->outgoingUnsequencedGroup;
 
                outgoingCommand->reliableSequenceNumber = 0;
                outgoingCommand->unreliableSequenceNumber = 0;
            }
            else
            {
                if (outgoingCommand->fragmentOffset == 0)
                    ++channel->outgoingUnreliableSequenceNumber;

                outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
                outgoingCommand->unreliableSequenceNumber = channel->outgoingUnreliableSequenceNumber;
            }
    }

    outgoingCommand->sendAttempts = 0;
    outgoingCommand->sentTime = 0;
    outgoingCommand->roundTripTimeout = 0;
    outgoingCommand->command.header.reliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);
    outgoingCommand->queueTime = ++peer->host->totalQueued;

    switch (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        outgoingCommand->command.sendUnreliable.unreliableSequenceNumber = ENET_HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        outgoingCommand->command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(peer->outgoingUnsequencedGroup);
        break;

    default:
        break;
    }

    if ((outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0 &&
        outgoingCommand->packet != nullptr)
        peer->outgoingSendReliableCommands.insert(peer->outgoingSendReliableCommands.end(), outgoingCommand);
    else
        peer->outgoingCommands.insert(peer->outgoingCommands.end(), outgoingCommand);
}

ENetOutgoingCommand* enet_peer_queue_outgoing_command(ENetPeer* peer, const ENetProtocol* command, std::shared_ptr<ENetPacket> packet, enet_uint32 offset, enet_uint16 length)
{
    ENetOutgoingCommand* outgoingCommand = (ENetOutgoingCommand*)enet_malloc(sizeof(ENetOutgoingCommand));
    if (outgoingCommand == nullptr)
        return nullptr;

    outgoingCommand->command = *command;
    outgoingCommand->fragmentOffset = offset;
    outgoingCommand->fragmentLength = length;
    outgoingCommand->packet = packet;
    if (packet != nullptr)
        ++packet->referenceCount;

    enet_peer_setup_outgoing_command(peer, outgoingCommand);

    return outgoingCommand;
}

/*
// 호스트로부터 받은 Unreliable 패킷들을 처리해보자
// Unreliable이기 때문에 재전송은 없다
// Reliable - Unreliable = 재전송
// Sequenced - Unsequenced = SequenceNumber

// 기본적으로 Unreliable이든, Reliable이든
// 모든 incoming, outcoming 큐에서는 ReliableSequenceNumber에 대해 정렬되어 있고,
// Unreliable 이라면 ReliableSequenceNumber - UnreliableSequenceNumber 순서대로 정렬되어 있다
*/
void ENetPeer::DispatchIncomingUnReliableCommands(ENetChannel* channel, ENetIncomingCommand* queuedCommand)
{
    ENetListIterator droppedCommand, startCommand, currentCommand;

    for (droppedCommand = startCommand = currentCommand = channel->incomingUnreliableCommands.begin();
        currentCommand != channel->incomingUnreliableCommands.end();
        currentCommand = enet_list_next(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

        if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
            continue;

        // command는 reliable-unreliable 상관없이 두종류의 seq number의 쌍으로 관리된다
        // 현재 command가 채널의 마지막 relSeq과 같다 => 받아들인다
        if (incomingCommand->reliableSequenceNumber == channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->fragmentsRemaining <= 0)
            {
                channel->incomingUnreliableSequenceNumber = incomingCommand->unreliableSequenceNumber;
                continue;
            }

            if (startCommand != currentCommand)
            {
                // 
                dispatchedCommands.move(dispatchedCommands.end(), &(*startCommand), enet_list_previous(currentCommand));

                if (!(flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
                {
                    host->dispatchQueue.insert(host->dispatchQueue.end(), &dispatchList);

                    flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }

                droppedCommand = currentCommand;
            }
            else
                if (droppedCommand != currentCommand)
                    droppedCommand = enet_list_previous(currentCommand);
        }
        else
        {
            // reliableSeq가 다르다? 과거 reliable에 대한, 또는 미래의 reliable에 대한
            enet_uint16 reliableWindow = incomingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
                currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
            // 미래 패킷이면 break
            if (reliableWindow >= currentWindow && reliableWindow < currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
                break;

            droppedCommand = enet_list_next(currentCommand);

            if (startCommand != currentCommand)
            {
                dispatchedCommands.move(dispatchedCommands.end(), &(*startCommand), enet_list_previous(currentCommand));

                if (!(flags & ENET_PEER_FLAG_NEEDS_DISPATCH)) {

                    host->dispatchQueue.insert(host->dispatchQueue.end(), &dispatchList);

                    flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }
            }
        }

        startCommand = enet_list_next(currentCommand);
    }

    if (startCommand != currentCommand)
    {
        dispatchedCommands.move(dispatchedCommands.end(), &*startCommand, enet_list_previous(currentCommand));

        if (!(flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
        {
            host->dispatchQueue.insert(host->dispatchQueue.end(), &dispatchList);

            flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
        }

        droppedCommand = currentCommand;
    }

    RemoveIncomingCommands(&channel->incomingUnreliableCommands, enet_list_begin(&channel->incomingUnreliableCommands), droppedCommand, queuedCommand);
}

void enet_peer_dispatch_incoming_unreliable_commands(ENetPeer* peer, ENetChannel* channel, ENetIncomingCommand* queuedCommand)
{
    ENetListIterator droppedCommand, startCommand, currentCommand;

    for (droppedCommand = startCommand = currentCommand = channel->incomingUnreliableCommands.begin();
        currentCommand != channel->incomingUnreliableCommands.end();
        currentCommand = enet_list_next(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

        if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
            continue;

        // command는 reliable-unreliable 상관없이 두종류의 seq number의 쌍으로 관리된다
        // 현재 command가 채널의 마지막 relSeq과 같다 => 받아들인다
        if (incomingCommand->reliableSequenceNumber == channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->fragmentsRemaining <= 0)
            {
                channel->incomingUnreliableSequenceNumber = incomingCommand->unreliableSequenceNumber;
                continue;
            }

            if (startCommand != currentCommand)
            {
                // 
                peer->dispatchedCommands.move(peer->dispatchedCommands.end(), &(*startCommand), enet_list_previous(currentCommand));

                if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
                {
                    peer->host->dispatchQueue.insert(peer->host->dispatchQueue.end(), &peer->dispatchList);

                    peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }

                droppedCommand = currentCommand;
            }
            else
                if (droppedCommand != currentCommand)
                    droppedCommand = enet_list_previous(currentCommand);
        }
        else
        {
            // reliableSeq가 다르다? 과거 reliable에 대한, 또는 미래의 reliable에 대한
            enet_uint16 reliableWindow = incomingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
                currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
            // 미래 패킷이면 break
            if (reliableWindow >= currentWindow && reliableWindow < currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
                break;

            droppedCommand = enet_list_next(currentCommand);

            if (startCommand != currentCommand)
            {
                peer->dispatchedCommands.move(peer->dispatchedCommands.end(), &(*startCommand), enet_list_previous(currentCommand));

                if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH)) {

                    peer->host->dispatchQueue.insert(peer->host->dispatchQueue.end(), &peer->dispatchList);

                    peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }
            }
        }

        startCommand = enet_list_next(currentCommand);
    }

    if (startCommand != currentCommand)
    {
        peer->dispatchedCommands.move(peer->dispatchedCommands.end(), &*startCommand, enet_list_previous(currentCommand));

        if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
        {
            peer->host->dispatchQueue.insert(peer->host->dispatchQueue.end(), &peer->dispatchList);

            peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
        }

        droppedCommand = currentCommand;
    }

    enet_peer_remove_incoming_commands(peer, &channel->incomingUnreliableCommands, enet_list_begin(&channel->incomingUnreliableCommands), droppedCommand, queuedCommand);
}

// 호스트로 부터 들어온 Reliable 패킷에 대한 처리
void ENetPeer::DispatchIncomingReliableCommands(ENetChannel* channel, ENetIncomingCommand* queuedCommand)
{
    ENetListIterator currentCommand;

    for (currentCommand = channel->incomingReliableCommands.begin();
        currentCommand != channel->incomingReliableCommands.end();
        currentCommand = enet_list_next(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

        if (incomingCommand->fragmentsRemaining > 0 ||
            incomingCommand->reliableSequenceNumber != (enet_uint16)(channel->incomingReliableSequenceNumber + 1))
            break;

        channel->incomingReliableSequenceNumber = incomingCommand->reliableSequenceNumber;

        if (incomingCommand->fragmentCount > 0)
            channel->incomingReliableSequenceNumber += incomingCommand->fragmentCount - 1;
    }

    if (currentCommand == enet_list_begin(&channel->incomingReliableCommands))
        return;

    channel->incomingUnreliableSequenceNumber = 0;

    dispatchedCommands.move(dispatchedCommands.end(), &(*dispatchedCommands.begin()), enet_list_previous(currentCommand));

    if (!(flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
    {
        host->dispatchQueue.insert(host->dispatchQueue.end(), &dispatchList);

        flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
    }

    if (!channel->incomingUnreliableCommands.empty())
        DispatchIncomingUnReliableCommands(channel, queuedCommand);
}
void enet_peer_dispatch_incoming_reliable_commands(ENetPeer* peer, ENetChannel* channel, ENetIncomingCommand* queuedCommand)
{
    ENetListIterator currentCommand;

    for (currentCommand = channel->incomingReliableCommands.begin();
        currentCommand != channel->incomingReliableCommands.end();
        currentCommand = enet_list_next(currentCommand))
    {
        ENetIncomingCommand* incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

        if (incomingCommand->fragmentsRemaining > 0 ||
            incomingCommand->reliableSequenceNumber != (enet_uint16)(channel->incomingReliableSequenceNumber + 1))
            break;

        channel->incomingReliableSequenceNumber = incomingCommand->reliableSequenceNumber;

        if (incomingCommand->fragmentCount > 0)
            channel->incomingReliableSequenceNumber += incomingCommand->fragmentCount - 1;
    }

    if (currentCommand == enet_list_begin(&channel->incomingReliableCommands))
        return;

    channel->incomingUnreliableSequenceNumber = 0;

    peer->dispatchedCommands.move(peer->dispatchedCommands.end(), &(*peer->dispatchedCommands.begin()), enet_list_previous(currentCommand));

    if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
    {
        peer->host->dispatchQueue.insert(peer->host->dispatchQueue.end(), &peer->dispatchList);

        peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
    }

    if (!channel->incomingUnreliableCommands.empty())
        enet_peer_dispatch_incoming_unreliable_commands(peer, channel, queuedCommand);
}

ENetIncomingCommand* enet_peer_queue_incoming_command(ENetPeer* peer, const ENetProtocol* command, const void* data, size_t dataLength, enet_uint32 flags, enet_uint32 fragmentCount)
{
    static ENetIncomingCommand dummyCommand;

    ENetChannel* channel = &peer->channels[command->header.channelID];
    enet_uint32 unreliableSequenceNumber = 0, reliableSequenceNumber = 0;
    enet_uint16 reliableWindow, currentWindow;
    ENetIncomingCommand* incomingCommand;
    ENetListIterator currentCommand;
    std::shared_ptr<ENetPacket> packet = nullptr;

    auto notifyError = [&]()
    {
        if (packet != nullptr && packet->referenceCount == 0)
            enet_packet_destroy(packet);

        return (ENetIncomingCommand*)nullptr;
    };

    auto discardCommand = [&]()
    {
        if (fragmentCount > 0)
            return notifyError();

        if (packet != nullptr && packet->referenceCount == 0)
            enet_packet_destroy(packet);

        return &dummyCommand;
    };

    if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
        return discardCommand();

    // unsequenced가 아니라면 seqnumber를 체크한다
    if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
    {
        reliableSequenceNumber = command->header.reliableSequenceNumber;
        reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
        currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;

        if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
            return discardCommand();
    }

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber)
            return discardCommand();

        for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingReliableCommands));
            currentCommand != enet_list_end(&channel->incomingReliableCommands);
            currentCommand = enet_list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

            if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
            {
                if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
                    continue;
            }
            else
                if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
                    break;

            if (incomingCommand->reliableSequenceNumber <= reliableSequenceNumber)
            {
                if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
                    break;

                return discardCommand();
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        unreliableSequenceNumber = ENET_NET_TO_HOST_16(command->sendUnreliable.unreliableSequenceNumber);

        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
            unreliableSequenceNumber <= channel->incomingUnreliableSequenceNumber)
            return discardCommand();

        for (currentCommand = enet_list_previous(enet_list_end(&channel->incomingUnreliableCommands));
            currentCommand != enet_list_end(&channel->incomingUnreliableCommands);
            currentCommand = enet_list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand*)&(*currentCommand);

            if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
                continue;

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

            if (incomingCommand->unreliableSequenceNumber <= unreliableSequenceNumber)
            {
                if (incomingCommand->unreliableSequenceNumber < unreliableSequenceNumber)
                    break;

                return discardCommand();
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        currentCommand = enet_list_end(&channel->incomingUnreliableCommands);
        break;

    default:
        return discardCommand();
    }

    if (peer->totalWaitingData >= peer->host->maximumWaitingData)
        return notifyError();

    packet = enet_packet_create(data, dataLength, flags);
    if (packet == nullptr)
        return notifyError();

    incomingCommand = (ENetIncomingCommand*)enet_malloc(sizeof(ENetIncomingCommand));
    if (incomingCommand == nullptr)
        return notifyError();

    incomingCommand->reliableSequenceNumber = command->header.reliableSequenceNumber;
    incomingCommand->unreliableSequenceNumber = unreliableSequenceNumber & 0xFFFF;
    incomingCommand->command = *command;
    incomingCommand->fragmentCount = fragmentCount;
    incomingCommand->fragmentsRemaining = fragmentCount;
    incomingCommand->packet = packet;
    incomingCommand->fragments = nullptr;

    if (fragmentCount > 0)
    {
        if (fragmentCount <= ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
            incomingCommand->fragments = (enet_uint32*)enet_malloc((fragmentCount + 31) / 32 * sizeof(enet_uint32));
        if (incomingCommand->fragments == nullptr)
        {
            enet_free(incomingCommand);

            return notifyError();
        }
        memset(incomingCommand->fragments, 0, (fragmentCount + 31) / 32 * sizeof(enet_uint32));
    }

    if (packet != nullptr)
    {
        ++packet->referenceCount;

        peer->totalWaitingData += packet->dataLength;
    }

    enet_list_insert(enet_list_next(currentCommand), incomingCommand);

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        enet_peer_dispatch_incoming_reliable_commands(peer, channel, incomingCommand);
        break;

    default:
        enet_peer_dispatch_incoming_unreliable_commands(peer, channel, incomingCommand);
        break;
    }

    return incomingCommand;
}

/** @} */