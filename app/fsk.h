/*
 * Original work Copyright 2025 gvJaime
 * https://github.com/gvJaime / https://github.com/elgambitero 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FSK_H
#define FSK_H

#include <stdint.h>

#define TRANSIT_BUFFER_SIZE 512

// MessengerConfig                            // 2024 kamilsss655
typedef union {
  struct {
    uint8_t
      receive    :1, // determines whether fsk modem will listen for new messages
      modulation :2, // determines FSK modulation type
      nrzi       :1, // uses NRZI encoding for bits
      unused     :5;
  } data;
  uint8_t __val;
} FSKConfig;

// Modem Modulation                             // 2024 kamilsss655
typedef enum ModemModulation {
  MOD_FSK_450,   // for bad conditions
  MOD_FSK_700,   // for medium conditions
  MOD_AFSK_1200,  // for good conditions
  MOD_BELL_202
} ModemModulation;

typedef enum ModemStatus {
  READY,
  SYNCING,
  RECEIVING,
  SENDING,
} ModemStatus;

extern ModemStatus modem_status;


void FSK_init(
  uint16_t sync_01,
  uint16_t sync_23,
  void (*receive_callback)(char*, uint16_t)
);
void FSK_configure();
void FSK_disable_rx();
void FSK_send_data(char * data, uint16_t len);
void FSK_store_packet_interrupt(const uint16_t interrupt_bits);
void FSK_end_rx();


#endif