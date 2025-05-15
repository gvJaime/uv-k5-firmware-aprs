/* Original work Copyright 2023 joaquimorg
 * https://github.com/joaquimorg
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 * 
 * Adaption Copyright 2025 gvJaime
 * https://github.com/gvJaime / https://github.com/elgambitero 
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

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