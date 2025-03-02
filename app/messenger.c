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
#ifdef ENABLE_MESSENGER

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/bk4819.h"
#include "external/printf/printf.h"
#include "misc.h"
#include "settings.h"
#include "radio.h"
#include "app.h"
#include "audio.h"
#include "functions.h"
#include "frequencies.h"
#include "driver/system.h"
#include "app/messenger.h"
#include "ui/ui.h"

#ifdef ENABLE_ENCRYPTION
  #include "helper/crypto.h"
#endif
#ifdef ENABLE_MESSENGER_UART
  #include "driver/uart.h"
#endif

// --------------------------------------------------------------------
// AX.25 APRS Messaging Definitions

// AX.25 constants
#define AX25_FLAG            0x7E
#define AX25_CONTROL_UI      0x03
#define AX25_PID_NO_LAYER3   0xF0
#define AX25_FCS_POLY        0x8408  // Reversed polynomial for CRC-16-CCITT

// We assume a fixed APRS payload length (adjust if needed)
enum {
    PAYLOAD_LENGTH = 30
};

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

MessengerConfig gMessengerConfig;

// --------------------------------------------------------------------
// Global Variables for Messaging

// Keypad T9 tables and related definitions
char T9TableLow[9][4] = {
    {',', '.', '?', '!'},
    {'a', 'b', 'c', '\0'},
    {'d', 'e', 'f', '\0'},
    {'g', 'h', 'i', '\0'},
    {'j', 'k', 'l', '\0'},
    {'m', 'n', 'o', '\0'},
    {'p', 'q', 'r', 's'},
    {'t', 'u', 'v', '\0'},
    {'w', 'x', 'y', 'z'}
};
char T9TableUp[9][4] = {
    {',', '.', '?', '!'},
    {'A', 'B', 'C', '\0'},
    {'D', 'E', 'F', '\0'},
    {'G', 'H', 'I', '\0'},
    {'J', 'K', 'L', '\0'},
    {'M', 'N', 'O', '\0'},
    {'P', 'Q', 'R', 'S'},
    {'T', 'U', 'V', '\0'},
    {'W', 'X', 'Y', 'Z'}
};
unsigned char numberOfLettersAssignedToKey[9] = { 4, 3, 3, 3, 3, 3, 4, 3, 4 };

char T9TableNum[9][4] = {
    {'1', '\0', '\0', '\0'},
    {'2', '\0', '\0', '\0'},
    {'3', '\0', '\0', '\0'},
    {'4', '\0', '\0', '\0'},
    {'5', '\0', '\0', '\0'},
    {'6', '\0', '\0', '\0'},
    {'7', '\0', '\0', '\0'},
    {'8', '\0', '\0', '\0'},
    {'9', '\0', '\0', '\0'}
};
unsigned char numberOfNumsAssignedToKey[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

#define MAX_MSG_LENGTH (PAYLOAD_LENGTH - 1)

const uint8_t MSG_BUTTON_STATE_HELD = 1 << 1;
const uint8_t MSG_BUTTON_EVENT_SHORT = 0;
const uint8_t MSG_BUTTON_EVENT_LONG  = MSG_BUTTON_STATE_HELD;

char cMessage[PAYLOAD_LENGTH];
char lastcMessage[PAYLOAD_LENGTH];
char rxMessage[4][PAYLOAD_LENGTH + 2];
uint8_t hasNewMessage = 0;
uint16_t gErrorsDuringMSG;
uint8_t keyTickCounter = 0;
unsigned char cIndex = 0;
unsigned char prevKey = 0, prevLetter = 0;
KeyboardType keyboardType = UPPERCASE;

typedef enum MsgStatus {
    READY,
    SENDING,
    RECEIVING,
} MsgStatus;

MsgStatus msgStatus = READY;

// Global data packet instance (AX.25 frame)
union DataPacket dataPacket;

// Tone2 frequency variable for FSK baudrate
uint16_t TONE2_FREQ;

// --------------------------------------------------------------------
// AX.25 Helper Functions

// Encodes a callsign and SSID into the 7-byte AX.25 address field.
// The first 6 bytes: callsign (left-justified, padded with spaces, then shifted left by one).
// The 7th byte: (SSID (4 bits) << 1) OR 0x60.
void ax25_encode_address(uint8_t *out, const char *callsign, uint8_t ssid) {
    memset(out, ' ', 6);
    size_t len = strlen(callsign);
    if (len > 6) len = 6;
    memcpy(out, callsign, len);
    for (int i = 0; i < 6; i++) {
        out[i] <<= 1;
    }
    out[6] = ((ssid & 0x0F) << 1) | 0x60;
}

// Updates the CRC for one byte.
static uint16_t ax25_crc_update(uint16_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ AX25_FCS_POLY;
        else
            crc >>= 1;
    }
    return crc;
}

// Computes the Frame Check Sequence (CRC-16) over the provided data.
uint16_t ax25_compute_fcs(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
        crc = ax25_crc_update(crc, data[i]);
    return ~crc;
}

// Serializes the AX.25 frame into the union's serializedArray.
// Format: [FLAG][dest][source][control][pid][payload][fcs][FLAG]
void ax25_serialize_frame(union DataPacket *packet) {
    size_t pos = 0;
    // Start flag
    packet->serializedArray[pos++] = AX25_FLAG;
    // Destination address (7 bytes)
    memcpy(&packet->serializedArray[pos], packet->ax25.dest, 7);
    pos += 7;
    // Source address (7 bytes)
    memcpy(&packet->serializedArray[pos], packet->ax25.source, 7);
    pos += 7;
    // Control field
    packet->serializedArray[pos++] = packet->ax25.control;
    // PID field
    packet->serializedArray[pos++] = packet->ax25.pid;
    // Payload (APRS message)
    memcpy(&packet->serializedArray[pos], packet->ax25.payload, PAYLOAD_LENGTH);
    pos += PAYLOAD_LENGTH;
    // Compute FCS over dest, source, control, pid, and payload (excluding start flag)
    uint16_t crc = ax25_compute_fcs(&packet->serializedArray[1], 7 + 7 + 1 + 1 + PAYLOAD_LENGTH);
    packet->ax25.fcs = crc;
    // Append FCS (little-endian)
    packet->serializedArray[pos++] = crc & 0xFF;
    packet->serializedArray[pos++] = (crc >> 8) & 0xFF;
    // End flag
    packet->serializedArray[pos++] = AX25_FLAG;
    // pos should equal AX25_FRAME_SIZE
}

// --------------------------------------------------------------------
// Messaging Functions (Adapted for AX.25/APRS)

// MSG_FSKSendData loads the serialized AX.25 frame into the modem's TX FIFO and starts transmission.
void MSG_FSKSendData() {
    // Turn off CTCSS/CDCSS during FFSK
    const uint16_t css_val = BK4819_ReadRegister(BK4819_REG_51);
    BK4819_WriteRegister(BK4819_REG_51, 0);
    
    // Set FM deviation level based on channel bandwidth
    const uint16_t dev_val = BK4819_ReadRegister(BK4819_REG_40);
    uint16_t deviation;
    switch (gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_BANDWIDTH) {
        case BK4819_FILTER_BW_WIDE:   deviation = 1300; break;
        case BK4819_FILTER_BW_NARROW: deviation = 1200; break;
        default:                      deviation = 850;  break;
    }
    BK4819_WriteRegister(BK4819_REG_40, (dev_val & 0xf000) | (deviation & 0xfff));
    
    // Disable certain filters
    const uint16_t filt_val = BK4819_ReadRegister(BK4819_REG_2B);
    BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | (1u << 0));
    
    MSG_ConfigureFSK(false);
    SYSTEM_DelayMs(100);
    
    // Load the complete serialized frame into TX FIFO (two bytes at a time)
    for (size_t i = 0; i < sizeof(dataPacket.serializedArray); i += 2) {
        uint16_t word = (dataPacket.serializedArray[i + 1] << 8) | dataPacket.serializedArray[i];
        BK4819_WriteRegister(BK4819_REG_5F, word);
    }
    
    // Enable FSK TX
    BK4819_FskEnableTx();
    
    // Wait for TX to finish (with timeout)
    unsigned int timeout = 1000 / 5;
    while (timeout-- > 0) {
        SYSTEM_DelayMs(5);
        if (BK4819_ReadRegister(BK4819_REG_0C) & (1u << 0)) {
            BK4819_WriteRegister(BK4819_REG_02, 0);
            if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
                break;
        }
    }
    SYSTEM_DelayMs(100);
    
    // Restore FSK settings
    MSG_ConfigureFSK(true);
    BK4819_WriteRegister(BK4819_REG_40, dev_val);
    BK4819_WriteRegister(BK4819_REG_2B, filt_val);
    BK4819_WriteRegister(BK4819_REG_51, css_val);
}

// Enables or disables RX.
void MSG_EnableRX(const bool enable) {
    if (enable) {
        MSG_ConfigureFSK(true);
        if(gEeprom.MESSENGER_CONFIG.data.receive)
            BK4819_FskEnableRx();
    } else {
        BK4819_WriteRegister(BK4819_REG_70, 0);
        BK4819_WriteRegister(BK4819_REG_58, 0);
    }
}

// Shifts received message lines upward.
void moveUP(char (*rxMessages)[PAYLOAD_LENGTH + 2]) {
    strcpy(rxMessages[0], rxMessages[1]);
    strcpy(rxMessages[1], rxMessages[2]);
    strcpy(rxMessages[2], rxMessages[3]);
    memset(rxMessages[3], 0, sizeof(rxMessages[3]));
}

// MSG_SendPacket sends the AX.25 packet via the modem.
// It assumes the AX.25 frame has been serialized into dataPacket.serializedArray.
void MSG_SendPacket() {
    if (msgStatus != READY) return;
    
    RADIO_PrepareTX();
    if(RADIO_GetVfoState() != VFO_STATE_NORMAL) {
        gRequestDisplayScreen = DISPLAY_MAIN;
        return;
    }
    
    if (strlen((char *)dataPacket.ax25.payload) > 0) {
        msgStatus = SENDING;
        RADIO_SetVfoState(VFO_STATE_NORMAL);
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
        
        // If not an ACK packet, update the display
        if (dataPacket.ax25.control != ACK_PACKET) {
            moveUP(rxMessage);
            sprintf(rxMessage[3], "> %s", dataPacket.ax25.payload);
            memset(lastcMessage, 0, sizeof(lastcMessage));
            memcpy(lastcMessage, dataPacket.ax25.payload, PAYLOAD_LENGTH);
            cIndex = 0;
            prevKey = 0;
            prevLetter = 0;
            memset(cMessage, 0, sizeof(cMessage));
        }
        
        #ifdef ENABLE_ENCRYPTION
        if(dataPacket.ax25.control == ENCRYPTED_MESSAGE_PACKET) {
            // Example: generate a nonce and encrypt payload.
            CRYPTO_Random(dataPacket.ax25.payload, PAYLOAD_LENGTH);
            CRYPTO_Crypt(
                dataPacket.ax25.payload,
                PAYLOAD_LENGTH,
                dataPacket.ax25.payload,
                dataPacket.ax25.payload, // nonce pointer placeholder
                gEncryptionKey,
                256
            );
        }
        #endif
        
        BK4819_DisableDTMF();
        gMuteMic = true;
        SYSTEM_DelayMs(50);
        MSG_FSKSendData();
        SYSTEM_DelayMs(50);
        APP_EndTransmission(false);
        FUNCTION_Select(FUNCTION_FOREGROUND);
        RADIO_SetVfoState(VFO_STATE_NORMAL);
        gMuteMic = false;
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        MSG_EnableRX(true);
        MSG_ClearPacketBuffer();
        msgStatus = READY;
    } else {
        AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
    }
}

// Validates a received character.
uint8_t validate_char(uint8_t rchar) {
    if ((rchar == 0x1b) || (rchar >= 32 && rchar <= 127))
        return rchar;
    return 32;
}

// MSG_StorePacket reads bytes from the modem FIFO and stores them into the dataPacket's serialized array.
void MSG_StorePacket(const uint16_t interrupt_bits) {
    const bool rx_sync = (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) ? true : false;
    const bool rx_fifo_almost_full = (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) ? true : false;
    const bool rx_finished = (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) ? true : false;
    
    if (rx_sync) {
        #ifdef ENABLE_MESSENGER_FSK_MUTE
        if(gCurrentCodeType == CODE_TYPE_OFF)
            AUDIO_AudioPathOff();
        #endif
        gFSKWriteIndex = 0;
        MSG_ClearPacketBuffer();
        msgStatus = RECEIVING;
    }
    
    if (rx_fifo_almost_full && msgStatus == RECEIVING) {
        const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & (7u << 0);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
            if (gFSKWriteIndex < sizeof(dataPacket.serializedArray))
                dataPacket.serializedArray[gFSKWriteIndex++] = (word >> 0) & 0xff;
            if (gFSKWriteIndex < sizeof(dataPacket.serializedArray))
                dataPacket.serializedArray[gFSKWriteIndex++] = (word >> 8) & 0xff;
        }
        SYSTEM_DelayMs(10);
    }
    
    if (rx_finished) {
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);
        BK4819_FskClearFifo();
        BK4819_FskEnableRx();
        msgStatus = READY;
        if (gFSKWriteIndex > 2) {
            MSG_HandleReceive();
        }
        gFSKWriteIndex = 0;
    }
}

// Initializes messaging by clearing buffers and resetting state.
void MSG_Init() {
    memset(rxMessage, 0, sizeof(rxMessage));
    memset(cMessage, 0, sizeof(cMessage));
    memset(lastcMessage, 0, sizeof(lastcMessage));
    hasNewMessage = 0;
    msgStatus = READY;
    prevKey = 0;
    prevLetter = 0;
    cIndex = 0;
    #ifdef ENABLE_ENCRYPTION
    gRecalculateEncKey = true;
    #endif
}

// Sends an ACK packet.
void MSG_SendAck() {
    MSG_ClearPacketBuffer();
    dataPacket.ax25.control = ACK_PACKET;
    // Set a few payload bytes for increased reliability.
    memset(dataPacket.ax25.payload, 255, 5);
    MSG_SendPacket();
}

// Handles a received message.
void MSG_HandleReceive() {
    if (dataPacket.ax25.control == ACK_PACKET) {
        #ifdef ENABLE_MESSENGER_DELIVERY_NOTIFICATION
        #ifdef ENABLE_MESSENGER_UART
        UART_printf("SVC<RCPT\r\n");
        #endif
        rxMessage[3][0] = '+';
        gUpdateStatus = true;
        gUpdateDisplay = true;
        #endif
    } else {
        moveUP(rxMessage);
        if (dataPacket.ax25.control >= INVALID_PACKET) {
            snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "ERROR: INVALID PACKET.");
        } else {
            #ifdef ENABLE_ENCRYPTION
            if(dataPacket.ax25.control == ENCRYPTED_MESSAGE_PACKET) {
                CRYPTO_Crypt(dataPacket.ax25.payload,
                             PAYLOAD_LENGTH,
                             dataPacket.ax25.payload,
                             dataPacket.ax25.payload,
                             gEncryptionKey,
                             256);
            }
            snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "< %s", dataPacket.ax25.payload);
            #else
            snprintf(rxMessage[3], PAYLOAD_LENGTH + 2, "< %s", dataPacket.ax25.payload);
            #endif
            #ifdef ENABLE_MESSENGER_UART
            UART_printf("SMS<%s\r\n", dataPacket.ax25.payload);
            #endif
        }
        if (gScreenToDisplay != DISPLAY_MSG) {
            hasNewMessage = 1;
            gUpdateStatus = true;
            gUpdateDisplay = true;
            #ifdef ENABLE_MESSENGER_NOTIFICATION
            gPlayMSGRing = true;
            #endif
        } else {
            gUpdateDisplay = true;
        }
    }
    if (dataPacket.ax25.control == MESSAGE_PACKET ||
        dataPacket.ax25.control == ENCRYPTED_MESSAGE_PACKET) {
        SYSTEM_DelayMs(700);
        if(gEeprom.MESSENGER_CONFIG.data.ack)
            MSG_SendAck();
    }
}

// Inserts a character into the outgoing message based on key input.
void insertCharInMessage(uint8_t key) {
    if (key == KEY_0) {
        if (keyboardType == NUMERIC)
            cMessage[cIndex] = '0';
        else
            cMessage[cIndex] = ' ';
        if (cIndex < MAX_MSG_LENGTH)
            cIndex++;
    } else if (prevKey == key) {
        cIndex = (cIndex > 0) ? cIndex - 1 : 0;
        if (keyboardType == NUMERIC)
            cMessage[cIndex] = T9TableNum[key - 1][(++prevLetter) % numberOfNumsAssignedToKey[key - 1]];
        else if (keyboardType == LOWERCASE)
            cMessage[cIndex] = T9TableLow[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
        else
            cMessage[cIndex] = T9TableUp[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
        if (cIndex < MAX_MSG_LENGTH)
            cIndex++;
    } else {
        prevLetter = 0;
        if (cIndex >= MAX_MSG_LENGTH)
            cIndex = (cIndex > 0) ? cIndex - 1 : 0;
        if (keyboardType == NUMERIC)
            cMessage[cIndex] = T9TableNum[key - 1][prevLetter];
        else if (keyboardType == LOWERCASE)
            cMessage[cIndex] = T9TableLow[key - 1][prevLetter];
        else
            cMessage[cIndex] = T9TableUp[key - 1][prevLetter];
        if (cIndex < MAX_MSG_LENGTH)
            cIndex++;
    }
    cMessage[cIndex] = '\\0';
    if (keyboardType == NUMERIC) {
        prevKey = 0;
        prevLetter = 0;
    } else {
        prevKey = key;
    }
}

// Processes a backspace input.
void processBackspace() {
    cIndex = (cIndex > 0) ? cIndex - 1 : 0;
    cMessage[cIndex] = '\\0';
    prevKey = 0;
    prevLetter = 0;
}

// Processes key events.
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
    uint8_t state = bKeyPressed + 2 * bKeyHeld;
    if (state == MSG_BUTTON_EVENT_SHORT) {
        switch (Key) {
            case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
            case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
                if (keyTickCounter > NEXT_CHAR_DELAY) {
                    prevKey = 0;
                    prevLetter = 0;
                }
                insertCharInMessage(Key);
                keyTickCounter = 0;
                break;
            case KEY_STAR:
                keyboardType = (KeyboardType)((keyboardType + 1) % END_TYPE_KBRD);
                break;
            case KEY_F:
                processBackspace();
                break;
            case KEY_UP:
                memset(cMessage, 0, sizeof(cMessage));
                memcpy(cMessage, lastcMessage, PAYLOAD_LENGTH);
                cIndex = strlen(cMessage);
                break;
            case KEY_MENU:
                MSG_Send(cMessage);
                break;
            case KEY_EXIT:
                gRequestDisplayScreen = DISPLAY_MAIN;
                break;
            default:
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                break;
        }
    } else if (state == MSG_BUTTON_EVENT_LONG) {
        switch (Key) {
            case KEY_F:
                MSG_Init();
                break;
            default:
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                break;
        }
    }
}

// Clears the data packet buffer.
void MSG_ClearPacketBuffer() {
    memset(dataPacket.serializedArray, 0, sizeof(dataPacket.serializedArray));
}

// Constructs and sends an AX.25 APRS message.
// This function sets the destination and source addresses, control, PID, copies the message text,
// serializes the frame, and calls MSG_SendPacket to transmit.
void MSG_Send(const char *cMessage) {
    MSG_ClearPacketBuffer();
    // Set addresses – adjust callsigns and SSID as needed.
    ax25_encode_address(dataPacket.ax25.dest, "APRS", 0);     // Destination (e.g. APRS)
    ax25_encode_address(dataPacket.ax25.source, "CALLSG", 1);   // Source (your callsign)
    
    // Set control and PID for a UI frame
    dataPacket.ax25.control = AX25_CONTROL_UI;
    dataPacket.ax25.pid = AX25_PID_NO_LAYER3;
    
    // Copy message text into payload (pad with spaces if shorter)
    memset(dataPacket.ax25.payload, ' ', PAYLOAD_LENGTH);
    strncpy((char *)dataPacket.ax25.payload, cMessage, PAYLOAD_LENGTH);
    
    #ifdef ENABLE_ENCRYPTION
      if(gEeprom.MESSENGER_CONFIG.data.encrypt)
          dataPacket.ax25.control = ENCRYPTED_MESSAGE_PACKET;
      else
          dataPacket.ax25.control = MESSAGE_PACKET;
    #else
      dataPacket.ax25.control = MESSAGE_PACKET;
    #endif
    
    // Serialize the AX.25 frame (computes FCS and adds flag bytes)
    ax25_serialize_frame(&dataPacket);
    
    // Transmit the packet
    MSG_SendPacket();
}

// Configures the FSK modem settings.
void MSG_ConfigureFSK(bool rx) {
    BK4819_WriteRegister(BK4819_REG_70,
        (0u << 15) |    // Disable TONE1
        (0u <<  8) |    // TONE1 tuning gain = 0
        (1u <<  7) |    // Enable TONE2
        (96u <<  0));   // TONE2/FSK tuning gain = 96

    switch(gEeprom.MESSENGER_CONFIG.data.modulation) {
        case MOD_AFSK_1200:
            TONE2_FREQ = 12389u;
            break;
        case MOD_FSK_700:
            TONE2_FREQ = 7227u;
            break;
        case MOD_FSK_450:
            TONE2_FREQ = 4646u;
            break;
    }
    BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
    
    switch(gEeprom.MESSENGER_CONFIG.data.modulation) {
        case MOD_FSK_700:
        case MOD_FSK_450:
            BK4819_WriteRegister(BK4819_REG_58,
                (0u << 13) | (0u << 10) | (3u << 8) | (0u << 6) | (0u << 4) | (0u << 1) | (1u << 0));
            break;
        case MOD_AFSK_1200:
            BK4819_WriteRegister(BK4819_REG_58,
                (1u << 13) | (7u << 10) | (3u << 8) | (0u << 6) | (0u << 4) | (1u << 1) | (1u << 0));
            break;
    }
    
    BK4819_WriteRegister(BK4819_REG_5A, 0x3072);
    BK4819_WriteRegister(BK4819_REG_5B, 0x576C);
    BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
    
    if(rx)
        BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | (1u << 0));
    
    uint16_t size = sizeof(dataPacket.serializedArray);
    if(rx)
        size = (((size + 1) / 2) * 2) + 2;
    BK4819_WriteRegister(BK4819_REG_5D, (size << 8));
    
    BK4819_FskClearFifo();
    
    BK4819_WriteRegister(BK4819_REG_59,
        (0u << 15) | (0u << 14) | (0u << 13) | (0u << 12) | (0u << 11) |
        (0u << 10) | (0u << 9)  | (0u << 8)  | ((rx ? 0u : 15u) << 4) |
        (1u << 3)  | (0u << 0));
    
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

#endif // ENABLE_MESSENGER
