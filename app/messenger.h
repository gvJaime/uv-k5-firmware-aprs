/* Original work Copyright 2023 joaquimorg
 * https://github.com/joaquimorg
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
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

#ifndef APP_MSG_H
#define APP_MSG_H

#ifdef ENABLE_MESSENGER

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/keyboard.h"

// For APRS we use a fixed payload length (adjust if needed)
enum {
    PAYLOAD_LENGTH = 30
};

typedef enum KeyboardType {
	UPPERCASE,
  	LOWERCASE,
  	NUMERIC,
  	END_TYPE_KBRD
} KeyboardType;

extern KeyboardType keyboardType;
extern uint16_t gErrorsDuringMSG;
extern char cMessage[PAYLOAD_LENGTH];
extern char rxMessage[4][PAYLOAD_LENGTH + 2];
extern uint8_t hasNewMessage;
extern uint8_t keyTickCounter;


// AX.25 frame constants
#define AX25_FLAG            0x7E
#define AX25_CONTROL_UI      0x03
#define AX25_PID_NO_LAYER3   0xF0
#define AX25_FCS_POLY        0x8408  // reversed polynomial (CRC-16-CCITT)


typedef enum MsgStatus {
    READY,
    SENDING,
    RECEIVING,
} MsgStatus;

typedef enum PacketType {
    MESSAGE_PACKET = 100u,
    ENCRYPTED_MESSAGE_PACKET,
    ACK_PACKET,
    INVALID_PACKET
} PacketType;

// Modem Modulation                             // 2024 kamilsss655
typedef enum ModemModulation {
  MOD_FSK_450,   // for bad conditions
  MOD_FSK_700,   // for medium conditions
  MOD_AFSK_1200  // for good conditions
} ModemModulation;

// Total AX.25 frame size calculation:
//   start flag (1) + dest (7) + source (7) + control (1) + pid (1) +
//   payload (PAYLOAD_LENGTH) + FCS (2) + end flag (1)
#define AX25_FRAME_SIZE (1 + 7 + 7 + 1 + 1 + PAYLOAD_LENGTH + 2 + 1)

// Union DataPacket now holds an AX.25 UI frame.
union DataPacket {
    struct {
        uint8_t dest[7];     // Destination callsign field (7 bytes)
        uint8_t source[7];   // Source callsign field (7 bytes)
        uint8_t control;     // Control field (UI frame)
        uint8_t pid;         // PID field (No layer 3)
        uint8_t payload[PAYLOAD_LENGTH]; // APRS text payload
        uint16_t fcs;        // Frame Check Sequence (CRC)
    } ax25;
    // Serialized array includes complete AX.25 frame with flag bytes.
    uint8_t serializedArray[AX25_FRAME_SIZE];
};

// For messaging configuration (unchanged)
typedef union {
    struct {
        uint8_t receive    :1, // FSK modem will listen for new messages
                ack        :1, // Automatically respond with ACK
                encrypt    :1, // Encrypt outgoing messages
                unused     :1,
                modulation :2, // FSK modulation type
                unused2    :2;
    } data;
    uint8_t __val;
} MessengerConfig;

extern MessengerConfig gMessengerConfig;

// Messaging functions (API remains the same)
void MSG_EnableRX(const bool enable);
void MSG_StorePacket(const uint16_t interrupt_bits);
void MSG_Init();
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void MSG_SendPacket();
void MSG_FSKSendData();
void MSG_ClearPacketBuffer();
void MSG_SendAck();
void MSG_HandleReceive();
void MSG_Send(const char *cMessage);
void MSG_ConfigureFSK(bool rx);

// AX.25 helper function prototypes
void ax25_encode_address(uint8_t *out, const char *callsign, uint8_t ssid);
uint16_t ax25_compute_fcs(const uint8_t *data, size_t len);
void ax25_serialize_frame(union DataPacket *packet);

#endif // ENABLE_MESSENGER

#endif // APP_MSG_H
