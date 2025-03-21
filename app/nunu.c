#include "nunu.h"

#include <string.h>
#include "settings.h"
#include "external/printf/printf.h"

#ifdef ENABLE_ENCRYPTION
    #include "helper/crypto.h"
#endif

void NUNU_prepare_message(DataPacket *dataPacket, const char * message) {
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
        }
        else
        {
            dataPacket->data.header=MESSAGE_PACKET;
        }
    #endif
}

void NUNU_prepare_ack(DataPacket *dataPacket) {
    // in the future we might reply with received payload and then the sending radio
    // could compare it and determine if the messegage was read correctly (kamilsss655)
    dataPacket->data.header = ACK_PACKET;
    // sending only empty header seems to not work, so set few bytes of payload to increase reliability (kamilsss655)
    memset(dataPacket->data.payload, 255, 5);
}

void NUNU_clear(DataPacket *dataPacket) {
    memset(dataPacket->serializedArray, 0, sizeof(dataPacket->serializedArray));
}

uint8_t NUNU_parse(DataPacket *dataPacket, uint8_t * origin) {
    #ifdef ENABLE_ENCRYPTION
        if(dataPacket->data.header == ENCRYPTED_MESSAGE_PACKET)
        {
            CRYPTO_Crypt(dataPacket->data.payload,
                PAYLOAD_LENGTH,
                dataPacket->data.payload,
                &dataPacket->data.nonce,
                gEncryptionKey,
                256);
        }
    #endif

    memcpy(dataPacket->serializedArray, origin, sizeof(dataPacket->serializedArray));

    return dataPacket->data.header >= INVALID_PACKET; 
}

void NUNU_display_received(DataPacket *dataPacket, char * field) {
    snprintf(field, PAYLOAD_LENGTH + 2, "< %s", dataPacket->data.payload);
}