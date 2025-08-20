#pragma once
/**
 * Packet flag bit constants.
 *
 * The host must be specified in network byte-order, and the port must be in
 * host byte-order. The constant ENET_HOST_ANY may be used to specify the
 * default server host.

   @sa ENetPacket
*/
enum ENetPacketFlag
{
    /** packet must be received by the target peer and resend attempts should be
      * made until the packet is delivered */
    ENET_PACKET_FLAG_RELIABLE = (1 << 0), 
    /** packet will not be sequenced with other packets
      */
    ENET_PACKET_FLAG_UNSEQUENCED = (1 << 1), 
    /** packet will not allocate data, and user must supply it instead */
    ENET_PACKET_FLAG_NO_ALLOCATE = (1 << 2),
    /** packet will be fragmented using unreliable (instead of reliable) sends
      * if it exceeds the MTU */
    ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT = (1 << 3), // 데이터가 너무 클 경우 조각내서 보냄

    /** whether the packet has been sent from all queues it has been entered into */
    ENET_PACKET_FLAG_SENT = (1 << 8)
};

/**
 * ENet packet structure.
 *
 * An ENet data packet that may be sent to or received from a peer. The shown
 * fields should only be read and never modified. The data field contains the
 * allocated data for the packet. The dataLength fields specifies the length
 * of the allocated data.  The flags field is either 0 (specifying no flags),
 * or a bitwise-or of any combination of the following flags:
 *
 *    ENET_PACKET_FLAG_RELIABLE - packet must be received by the target peer
 *    and resend attempts should be made until the packet is delivered
 *
 *    ENET_PACKET_FLAG_UNSEQUENCED - packet will not be sequenced with other packets
 *    (not supported for reliable packets)
 *
 *    ENET_PACKET_FLAG_NO_ALLOCATE - packet will not allocate data, and user must supply it instead
 *
 *    ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT - packet will be fragmented using unreliable
 *    (instead of reliable) sends if it exceeds the MTU
 *
 *    ENET_PACKET_FLAG_SENT - whether the packet has been sent from all queues it has been entered into
   @sa ENetPacketFlag
 */
class ENetPacket
{
public:
    size_t                   referenceCount;  /**< internal use only */
    enet_uint32              flags;           /**< bitwise-or of ENetPacketFlag constants */
    enet_uint8*              data;            /**< allocated data for packet */
    size_t                   dataLength;      /**< length of data */
    ENetPacketFreeCallback   freeCallback;
    void*                    userData;        /**< application private data, may be freely modified */

public:
    virtual void Callback() = 0;    /**< function to be called when the packet is no longer in use */

};
