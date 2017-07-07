#ifndef RNDIS_STD_H
#define RNDIS_STD_H

#define RNDIS_SEND_ENCAPSULATED_COMMAND 0x00
#define RNDIS_GET_ENCAPSULATED_RESPONSE 0x01

#define RNDIS_NOTIFICATION_RESPONSE_AVAILABLE 0x01

#define RNDIS_PACKET_MSG        0x00000001
#define RNDIS_INITIALIZE_MSG    0x00000002
#define RNDIS_HALT_MSG          0x00000003
#define RNDIS_QUERY_MSG         0x00000004
#define RNDIS_SET_MSG           0x00000005

#define RNDIS_STATUS_SUCCESS            0x00000000
#define RNDIS_STATUS_FAILURE            0xC0000001
#define RNDIS_STATUS_NOT_SUPPORTED      0xC00000BB

#define RNDIS_OID_SUPPORTED_LIST        0x00010101
#define RNDIS_OID_PHYSICAL_MEDIUM       0x00010202
#define RNDIS_OID_PACKET_FILTER         0x0001010E
#define RNDIS_OID_PERMANENT_ADDRESS     0x01010101
#define RNDIS_OID_CURRENT_ADDRESS       0x01010102

struct rndis_notification {
  uint32_t Notification;
  uint32_t Reserved;
};

struct rndis_command_header {
  uint32_t MessageType;
  uint32_t MessageLength;
  uint32_t RequestID;
};

struct rndis_response_header {
  uint32_t MessageType;
  uint32_t MessageLength;
  uint32_t RequestID;
  uint32_t Status;
};

struct rndis_initialize_msg {
  struct rndis_command_header hdr;
  uint32_t MajorVersion;
  uint32_t MinorVersion;
  uint32_t MaxTransferSize;
};

struct rndis_initialize_cmplt {
  struct rndis_response_header hdr;
  uint32_t MajorVersion;
  uint32_t MinorVersion;
  uint32_t DeviceFlags;
  uint32_t Medium;
  uint32_t MaxPacketsPerTransfer;
  uint32_t MaxTransferSize;
  uint32_t PacketAlignmentFactor;
  uint32_t Reserved1;
  uint32_t Reserved2;
};

struct rndis_query_msg {
  struct rndis_command_header hdr;
  uint32_t ObjectID;
  uint32_t InformationBufferLength;
  uint32_t InformationBufferOffset;
  uint32_t Reserved;
  uint32_t Buffer[];
};

struct rndis_query_cmplt {
  struct rndis_response_header hdr;
  uint32_t InformationBufferLength;
  uint32_t InformationBufferOffset;
  uint32_t Buffer[];
};

struct rndis_set_msg {
  struct rndis_command_header hdr;
  uint32_t ObjectID;
  uint32_t InformationBufferLength;
  uint32_t InformationBufferOffset;
  uint32_t Reserved;
  uint32_t Buffer[];
};

struct rndis_packet_msg {
  uint32_t MessageType;
  uint32_t MessageLength;
  uint32_t DataOffset;
  uint32_t DataLength;
  uint32_t OutOfBandDataOffset;
  uint32_t OutOfBandDataLength;
  uint32_t NumOutOfBandDataElements;
  uint32_t PerPacketInfoOffset;
  uint32_t PerPacketInfoLength;
  uint32_t Padding[7];
};

#endif
