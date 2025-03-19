#ifndef FSK_H
#define FSK_H

#include <stdint.h>


// MessengerConfig                            // 2024 kamilsss655
typedef union {
  struct {
    uint8_t
      receive    :1, // determines whether fsk modem will listen for new messages
      modulation :2, // determines FSK modulation type
      unused     :5;
  } data;
  uint8_t __val;
} FSKConfig;

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

extern uint16_t gFSKWriteIndex;
extern ModemStatus modem_status;


void FSK_init(void (*receive_callback)(uint8_t*));
void FSK_enable_rx(const bool enable);
void FSK_send_data(char * data, uint16_t len);
void FSK_configure(uint8_t rx, uint16_t size);
void FSK_store_packet_interrupt(const uint16_t interrupt_bits);


#endif