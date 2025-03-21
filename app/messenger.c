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

#include <string.h>
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
#ifdef ENABLE_APRS
	#include "app/aprs.h"
	#include "app/ax25.h"
#else
	#include "app/nunu.h"
#endif
#include "app/fsk.h"

const uint8_t MSG_BUTTON_STATE_HELD = 1 << 1;

const uint8_t MSG_BUTTON_EVENT_SHORT =  0;
const uint8_t MSG_BUTTON_EVENT_LONG =  MSG_BUTTON_STATE_HELD;

const uint8_t MAX_MSG_LENGTH = MESSAGE_LENGTH - 1;

#define NEXT_CHAR_DELAY 100 // 10ms tick

char T9TableLow[9][4] = { {',', '.', '?', '!'}, {'a', 'b', 'c', '\0'}, {'d', 'e', 'f', '\0'}, {'g', 'h', 'i', '\0'}, {'j', 'k', 'l', '\0'}, {'m', 'n', 'o', '\0'}, {'p', 'q', 'r', 's'}, {'t', 'u', 'v', '\0'}, {'w', 'x', 'y', 'z'} };
char T9TableUp[9][4] = { {',', '.', '-', ':'}, {'A', 'B', 'C', '\0'}, {'D', 'E', 'F', '\0'}, {'G', 'H', 'I', '\0'}, {'J', 'K', 'L', '\0'}, {'M', 'N', 'O', '\0'}, {'P', 'Q', 'R', 'S'}, {'T', 'U', 'V', '\0'}, {'W', 'X', 'Y', 'Z'} };
unsigned char numberOfLettersAssignedToKey[9] = { 4, 3, 3, 3, 3, 3, 4, 3, 4 };

char T9TableNum[9][4] = { {'1', '\0', '\0', '\0'}, {'2', '\0', '\0', '\0'}, {'3', '\0', '\0', '\0'}, {'4', '\0', '\0', '\0'}, {'5', '\0', '\0', '\0'}, {'6', '\0', '\0', '\0'}, {'7', '\0', '\0', '\0'}, {'8', '\0', '\0', '\0'}, {'9', '\0', '\0', '\0'} };
unsigned char numberOfNumsAssignedToKey[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };

char cMessage[MESSAGE_LENGTH];
char lastcMessage[MESSAGE_LENGTH];
char rxMessage[4][MESSAGE_LENGTH + 2];
unsigned char cIndex = 0;
unsigned char prevKey = 0, prevLetter = 0;
KeyboardType keyboardType = UPPERCASE;

#ifdef ENABLE_APRS
	AX25UIFrame ax25frame;
#else
	DataPacket dataPacket;
#endif

uint16_t gErrorsDuringMSG;

uint8_t hasNewMessage = 0;

uint8_t keyTickCounter = 0;


void moveUP(char (*rxMessages)[MESSAGE_LENGTH + 2]) {
    // Shift existing lines up
    strcpy(rxMessages[0], rxMessages[1]);
	strcpy(rxMessages[1], rxMessages[2]);
	strcpy(rxMessages[2], rxMessages[3]);

    // Insert the new line at the last position
	memset(rxMessages[3], 0, sizeof(rxMessages[3]));
}

void MSG_SendPacket(char * packet, uint16_t len) {

	if(len > 0) {
		FSK_send_data(packet, len);

	} else {
		AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
	}
}

uint8_t validate_char( uint8_t rchar ) {
	if ( (rchar == 0x1b) || (rchar >= 32 && rchar <= 127) ) {
		return rchar;
	}
	return 32;
}

void MSG_Init() {
	memset(rxMessage, 0, sizeof(rxMessage));
	memset(cMessage, 0, sizeof(cMessage));
	memset(lastcMessage, 0, sizeof(lastcMessage));
	hasNewMessage = 0;
	prevKey = 0;
    prevLetter = 0;
	cIndex = 0;

	FSK_init(&MSG_HandleReceive);

	#ifdef ENABLE_ENCRYPTION
		gRecalculateEncKey = true;
	#endif
}

#ifdef ENABLE_APRS
void MSG_SendAck(uint16_t ack_id) {
#else
void MSG_SendAck() {
#endif
	// in the future we might reply with received payload and then the sending radio
	// could compare it and determine if the messegage was read correctly (kamilsss655)
	#ifdef ENABLE_APRS
		char origin_callsign[CALLSIGN_SIZE + 1];
		origin_callsign[CALLSIGN_SIZE] = 0;
		strncpy(origin_callsign, ax25frame.raw_buffer + 1, CALLSIGN_SIZE);

		MSG_ClearPacketBuffer();
		APRS_prepare_ack(&ax25frame, ack_id, origin_callsign);
		MSG_SendPacket(ax25frame.raw_buffer, ax25frame.len);
	#else
		MSG_ClearPacketBuffer();
		NUNU_prepare_ack(&dataPacket);
		MSG_SendPacket(
			dataPacket.serializedArray,
			strlen((char *)dataPacket.data.payload)
		);
	#endif
}

void MSG_HandleReceive(uint8_t * receive_buffer) {
	

	#ifdef ENABLE_APRS
		uint8_t send_ack = 0;
		uint8_t valid = APRS_parse(&ax25frame, receive_buffer);
	#else
		uint8_t valid = NUNU_parse(&dataPacket, receive_buffer);
	#endif

	if(!valid) {
		snprintf(rxMessage[3], MESSAGE_LENGTH + 2, "ERROR: INVALID PACKET.");
	} else {
		moveUP(rxMessage);
		#ifdef ENABLE_APRS
			if (APRS_is_ack_for_message(&ax25frame, msg_id)) {
				send_ack = 1;
		#else
			if (dataPacket.data.header == ACK_PACKET) {
		#endif
			#ifdef ENABLE_MESSENGER_DELIVERY_NOTIFICATION
				#ifdef ENABLE_MESSENGER_UART
					UART_printf("SVC<RCPT\r\n");
				#endif
				rxMessage[3][0] = '+';
				gUpdateStatus = true;
				gUpdateDisplay = true;
			#endif
		}
		else
		{
			#ifdef ENABLE_APRS
				APRS_display_received(&ax25frame, rxMessage[3]);
			#else
				NUNU_display_received(&dataPacket, rxMessage[3]);
			#endif
			#ifdef ENABLE_MESSENGER_UART
				#ifdef ENABLE_APRS
					uint8_t buf[PAYLOAD_LENGTH + 8];
					snprintf(
						buf,
						ax25frame.fcs_offset - ax25frame.control_offset,
						"APRS<%s\r\n",
						ax25frame.buffer + ax25frame.control_offset + 1
					);
					UART_Send(buf, sizeof(buf));
				#else
					UART_printf("SMS<%s\r\n", dataPacket.data.payload);
				#endif
			#endif
		}

		if ( gScreenToDisplay != DISPLAY_MSG ) {
			hasNewMessage = 1;
			gUpdateStatus = true;
			gUpdateDisplay = true;
	#ifdef ENABLE_MESSENGER_NOTIFICATION
			gPlayMSGRing = true;
	#endif
		}
		else {
			gUpdateDisplay = true;
		}
	}

	// Transmit a message to the sender that we have received the message
	if(gEeprom.MESSENGER_CONFIG.data.ack) {
		#ifdef ENABLE_APRS
			uint16_t ack_id = APRS_get_msg_id(&ax25frame);
			if(send_ack && ack_id)
		#else
			if (dataPacket.data.header == MESSAGE_PACKET ||
				dataPacket.data.header == ENCRYPTED_MESSAGE_PACKET)
		#endif
		{
			// wait so the correspondent radio can properly receive it
			SYSTEM_DelayMs(700);
			
			#ifdef ENABLE_APRS
				MSG_SendAck(ack_id);
			#else
				MSG_SendAck();
			#endif
		}
	}
	
}

// ---------------------------------------------------------------------------------

void insertCharInMessage(uint8_t key) {
	if ( key == KEY_0 ) {
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = '0';
		} else {
			cMessage[cIndex] = ' ';
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}
	} else if (prevKey == key)
	{
		cIndex = (cIndex > 0) ? cIndex - 1 : 0;
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = T9TableNum[key - 1][(++prevLetter) % numberOfNumsAssignedToKey[key - 1]];
		} else if ( keyboardType == LOWERCASE ) {
			cMessage[cIndex] = T9TableLow[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
		} else {
			cMessage[cIndex] = T9TableUp[key - 1][(++prevLetter) % numberOfLettersAssignedToKey[key - 1]];
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}
	}
	else
	{
		prevLetter = 0;
		if ( cIndex >= MAX_MSG_LENGTH ) {
			cIndex = (cIndex > 0) ? cIndex - 1 : 0;
		}
		if ( keyboardType == NUMERIC ) {
			cMessage[cIndex] = T9TableNum[key - 1][prevLetter];
		} else if ( keyboardType == LOWERCASE ) {
			cMessage[cIndex] = T9TableLow[key - 1][prevLetter];
		} else {
			cMessage[cIndex] = T9TableUp[key - 1][prevLetter];
		}
		if ( cIndex < MAX_MSG_LENGTH ) {
			cIndex++;
		}

	}
	cMessage[cIndex] = '\0';
	if ( keyboardType == NUMERIC ) {
		prevKey = 0;
		prevLetter = 0;
	} else {
		prevKey = key;
	}
}

void processBackspace() {
	cIndex = (cIndex > 0) ? cIndex - 1 : 0;
	cMessage[cIndex] = '\0';
	prevKey = 0;
    prevLetter = 0;
}

void  MSG_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld) {
	uint8_t state = bKeyPressed + 2 * bKeyHeld;

	if (state == MSG_BUTTON_EVENT_SHORT) {

		switch (Key)
		{
			case KEY_0:
			case KEY_1:
			case KEY_2:
			case KEY_3:
			case KEY_4:
			case KEY_5:
			case KEY_6:
			case KEY_7:
			case KEY_8:
			case KEY_9:
				if ( keyTickCounter > NEXT_CHAR_DELAY) {
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
				memcpy(cMessage, lastcMessage, MESSAGE_LENGTH);
				cIndex = strlen(cMessage);
				break;
			/*case KEY_DOWN:
				break;*/
			case KEY_MENU:
				// Send message
				if(strlen(cMessage)){
					MSG_Send(cMessage);
				}
				break;
			case KEY_EXIT:
				gRequestDisplayScreen = DISPLAY_MAIN;
				break;

			default:
				AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
				break;
		}

	} else if (state == MSG_BUTTON_EVENT_LONG) {

		switch (Key)
		{
			case KEY_F:
				MSG_Init();
				break;
			default:
				AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
				break;
		}
	}

}

void MSG_ClearPacketBuffer()
{
	#ifdef ENABLE_APRS
		AX25_clear(&ax25frame);
	#else
		NUNU_clear(&dataPacket);
	#endif
}

void MSG_Send(char *cMessage){
	if(strlen(cMessage) < 1) {
		return;
	}

	MSG_ClearPacketBuffer();

	#ifdef ENABLE_APRS
		APRS_prepare_message(&ax25frame, cMessage, false);
		MSG_SendPacket(ax25frame.raw_buffer, ax25frame.len);
	#else
		NUNU_prepare_message(&dataPacket, cMessage);
		MSG_SendPacket(
			dataPacket.serializedArray,
			strlen((char *)dataPacket.data.payload)
		);
	#endif

	moveUP(rxMessage);

	sprintf(rxMessage[3], "> %s", cMessage);

	memset(lastcMessage, 0, sizeof(lastcMessage));

	sprintf(lastcMessage, "> %s", cMessage);

	cIndex = 0;
	prevKey = 0;
	prevLetter = 0;
	memset(cMessage, 0, strlen(cMessage));
}