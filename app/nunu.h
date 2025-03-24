#ifndef NUNU_H
#define NUNU_H

#include <stdint.h>

#define NUNU_SYNC_WORD_01 0x3072
#define NUNU_SYNC_WORD_23 0x576C

enum {
	NONCE_LENGTH = 13,
    PAYLOAD_LENGTH = 30
};

typedef enum PacketType {
    MESSAGE_PACKET = 100u,
    ENCRYPTED_MESSAGE_PACKET,
    ACK_PACKET,
    INVALID_PACKET
} PacketType;

// Data Packet definition                            // 2024 kamilsss655
typedef union
{
  struct{
    uint8_t header;
    char payload[PAYLOAD_LENGTH];
    unsigned char nonce[NONCE_LENGTH];
    // uint8_t signature[SIGNATURE_LENGTH];
  } data;
  // header + payload + nonce = must be an even number
  char serializedArray[1+PAYLOAD_LENGTH+NONCE_LENGTH];
} DataPacket;


/**
 * Prepares the messages into a datapacket, leaving it's buffer ready
 * go be passed onto the modem.
 * 
 * @returns the length of the whole packet, in bytes
 */
uint16_t NUNU_prepare_message(DataPacket *dataPacket, const char * message);
void NUNU_prepare_ack(DataPacket *dataPacket);
void NUNU_clear(DataPacket *dataPacket);
void NUNU_display_received(DataPacket *dataPacket, char * field);
uint8_t NUNU_parse(DataPacket *dataPacket, char * origin, uint16_t len);

#endif