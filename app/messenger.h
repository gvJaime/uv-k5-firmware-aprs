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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/keyboard.h"


typedef enum KeyboardType {
	UPPERCASE,
  	LOWERCASE,
  	NUMERIC,
  	END_TYPE_KBRD
} KeyboardType;

#define MESSAGE_LENGTH 128

extern KeyboardType keyboardType;
extern uint16_t gErrorsDuringMSG;
extern char cMessage[MESSAGE_LENGTH];
extern char rxMessage[4][MESSAGE_LENGTH + 2];
extern uint8_t hasNewMessage;
extern uint8_t keyTickCounter;

// MessengerConfig                            // 2024 kamilsss655
typedef union {
  struct {
    uint8_t
      ack        :1, // determines whether the radio will automatically respond to messages with ACK
      encrypt    :1, // determines whether outgoing messages will be encrypted
      unused     :6;
  } data;
  uint8_t __val;
} MessengerConfig;

void MSG_Init();
void MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void MSG_SendPacket(char * packet, uint16_t len);
void MSG_ClearPacketBuffer();
#ifdef ENABLE_APRS
  void MSG_SendAck(uint16_t ack_id);
  void MSG_DisplaySent(char * field);
#else
  void MSG_SendAck();
#endif
void MSG_HandleReceive(uint8_t * receive_buffer);
void MSG_Send(char *cMessage);

#endif
