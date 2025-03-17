#ifndef FSK_H
#define FSK_H

#include <stdint.h>

// Modem Modulation                             // 2024 kamilsss655
typedef enum ModemModulation {
  MOD_FSK_450,   // for bad conditions
  MOD_FSK_700,   // for medium conditions
  MOD_AFSK_1200  // for good conditions
} ModemModulation;

typedef enum ModemStatus {
  READY,
  SENDING,
  RECEIVING,
} ModemStatus;

void FSK_send_data(char * data, uint16_t len);
void FSK_configure(uint8_t rx, uint16_t size);

extern uint16_t gFSKWriteIndex;
extern ModemStatus modem_status;

#endif