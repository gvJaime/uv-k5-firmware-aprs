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

#include "nunu.h"

#include <string.h>
#include "settings.h"
#include "external/printf/printf.h"

#ifdef ENABLE_ENCRYPTION
    #include "helper/crypto.h"
#endif

uint16_t NUNU_prepare_message(DataPacket *dataPacket, const char * message) {
    NUNU_clear(dataPacket);
    dataPacket->data.header=MESSAGE_PACKET;
    memcpy(dataPacket->data.payload, message, strlen(message));

	#ifdef ENABLE_ENCRYPTION
        if(gEeprom.MESSENGER_CONFIG.data.encrypt)
        {
            dataPacket->data.header=ENCRYPTED_MESSAGE_PACKET;

            CRYPTO_Random(dataPacket->data.nonce, NONCE_LENGTH);

            CRYPTO_Crypt(
                dataPacket->data.payload,
                PAYLOAD_LENGTH,
                dataPacket->data.payload,
                &(dataPacket->data.nonce),
                gEncryptionKey,
                256
            );
            return 1 + PAYLOAD_LENGTH + NONCE_LENGTH;
        } else {
            return strlen(dataPacket->serializedArray) + 1; // the ending 0 must be transmitted.
        }
    #else
        return strlen(dataPacket->serializedArray) + 1; // the ending 0 must be transmitted.
    #endif
}

void NUNU_prepare_ack(DataPacket *dataPacket) {
    NUNU_clear(dataPacket);
    // in the future we might reply with received payload and then the sending radio
    // could compare it and determine if the messegage was read correctly (kamilsss655)
    dataPacket->data.header = ACK_PACKET;
    // sending only empty header seems to not work, so set few bytes of payload to increase reliability (kamilsss655)
    memset(dataPacket->data.payload, 255, 5);
}

void NUNU_clear(DataPacket *dataPacket) {
    memset(dataPacket->serializedArray, 0, sizeof(dataPacket->serializedArray));
}

uint8_t NUNU_parse(DataPacket *dataPacket, char * origin, uint16_t len) {
    NUNU_clear(dataPacket);


    #ifdef ENABLE_ENCRYPTION
        if(dataPacket->data.header == ENCRYPTED_MESSAGE_PACKET)
        {
            CRYPTO_Crypt(dataPacket->data.payload,
                PAYLOAD_LENGTH,
                dataPacket->data.payload,
                &dataPacket->data.nonce,
                gEncryptionKey,
                256);

            memcpy(dataPacket->serializedArray, origin, 1 + PAYLOAD_LENGTH + NONCE_LENGTH);
        } else {
            memcpy(dataPacket->serializedArray, origin, len);
        }
    #else
        memcpy(dataPacket->serializedArray, origin, len);
    #endif


    return dataPacket->data.header < INVALID_PACKET && dataPacket->data.header >= MESSAGE_PACKET;
}

void NUNU_display_received(DataPacket *dataPacket, char * field) {
    snprintf(field, PAYLOAD_LENGTH + 2, "< %s", dataPacket->data.payload);
}