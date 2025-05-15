/*
 * Original work Copyright 2023 joaquimorg
 * https://github.com/joaquimorg
 *
 * Modified work Copyright 2025 gvJaime
 * https://github.com/gvJaime
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


#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/st7565.h"
#include "driver/bk4819.h"
#include "settings.h"
#include "driver/system.h"
#include "app.h"
#include "functions.h"
#include "app/fsk.h"
#include "ui/ui.h"
#include "audio.h"
#include "misc.h"

#define TX_FIFO_SEGMENT 64u
#define TX_FIFO_THRESHOLD 64u
#define RX_FIFO_THRESHOLD 4u

#define NRZI_PREAMBLE 15u

#define RX_DATA_LENGTH 32u

char transit_buffer[TRANSIT_BUFFER_SIZE];

uint16_t gFSKWriteIndex = 0;

void (*FSK_receive_callback)(char*, uint16_t);

ModemStatus modem_status = READY;

uint16_t _sync_01;
uint16_t _sync_23;

uint16_t processed_sync_01;
uint16_t processed_sync_23;
uint8_t nrzi_sync_state;

uint16_t FSK_set_data_length(uint16_t len);

/**
 * Decodes an NRZI (Non-Return-to-Zero Inverted) encoded buffer back to its original form
 * where '0' caused a state change and '1' maintained the state during encoding
 * 
 * This implementation processes the buffer in-place, one byte at a time
 * 
 * @param buffer The buffer to decode in-place
 * @param length Length of the buffer in bytes
 * @param initial_state The initial state of the NRZI encoding (typically 1)
 * @return 0 on success, -1 on failure
 */
int8_t FSK_decode_nrzi(char *buffer, size_t length, uint8_t initial_state) {
    if (!buffer || length == 0) {
        return -1;
    }

    uint8_t prev_bit = initial_state;
    
    char encoded_byte;

    // Process each byte in the buffer
    for (size_t i = 0; i < length; i++) {
        // Save the encoded byte value
        encoded_byte = buffer[i];
        // Clear the current byte for writing the decoded value
        buffer[i] = 0;
        
        // Process each bit in the byte (MSB first)
        for (int8_t bit_idx = 7; bit_idx >= 0; bit_idx--) {
            // Extract the current bit from the encoded byte
            uint8_t current_bit = (encoded_byte >> bit_idx) & 0x01;
            
            // If the state changed, it was a '0' bit, otherwise it was a '1' bit
            uint8_t decoded_bit = (current_bit == prev_bit) ? 1 : 0;
            
            // Set the decoded bit in the output
            if (decoded_bit) {
                buffer[i] |= (1 << bit_idx);
            }
            
            // Update the previous bit for the next iteration
            prev_bit = current_bit;
        }
    }
    
    return 0;
}

/**
 * Encodes a buffer of bytes into NRZI (Non-Return-to-Zero Inverted) format
 * where '0' causes a state change and '1' maintains the current state
 * 
 * This implementation processes the buffer in-place, one byte at a time
 * 
 * @param buffer The buffer to encode in-place
 * @param length Length of the buffer in bytes
 * @return 0 on success, -1 on failure
 */
int8_t FSK_encode_nrzi(char *buffer, size_t length, uint8_t initial_nrzi_state) {
    if (!buffer || length == 0) {
        return -1;
    }

    uint8_t nrzi_state = initial_nrzi_state;

    char original_byte;

    // Process each byte in the buffer
    for (size_t i = 0; i < length; i++) {
        // Save the original byte value
        original_byte = buffer[i];
        // Clear the current byte for writing the encoded value
        buffer[i] = 0;

        // Process each bit in the byte (MSB first)
        for (int8_t bit_idx = 7; bit_idx >= 0; bit_idx--) {
            // Extract the current bit from the original byte
            uint8_t bit = (original_byte >> bit_idx) & 0x01;

            // In this NRZI variant, a '0' bit causes a state change
            if (bit == 0) {
                nrzi_state = !nrzi_state;
            }
            // '1' bit maintains the current state (no change needed)

            // Set or clear the bit in the output at the same position
            if (nrzi_state) {
                buffer[i] |= (1 << bit_idx);
            }
            // If nrzi_state is 0, the bit remains 0 (already cleared)
        }
    }
    return 0;
}


void FSK_disable_tx() {
	const uint16_t fsk_reg59 = BK4819_ReadRegister(BK4819_REG_59);
	BK4819_WriteRegister(BK4819_REG_59, fsk_reg59 & ~(1u << 11) );
}

void FSK_disable_rx() {
	const uint16_t fsk_reg59 = BK4819_ReadRegister(BK4819_REG_59);
	BK4819_WriteRegister(BK4819_REG_59, fsk_reg59 & ~(1u << 12) );
}

char* FSK_find_end_of_sync_words(char* buffer, uint16_t buffer_length) {
    char sync_pattern[4];
    sync_pattern[0] = (char)(_sync_01 & 0xFF);
    sync_pattern[1] = (char)((_sync_01 >> 8) & 0xFF);
    sync_pattern[2] = (char)(_sync_23 & 0xFF);
    sync_pattern[3] = (char)((_sync_23 >> 8) & 0xFF);

    // First, find the start of a sync pattern
    uint16_t start_idx = 0;
    uint8_t found_start = 0;
    
    // Look for the first occurrence of a complete sync pattern
    for (start_idx = 0; start_idx <= buffer_length - 4; start_idx++) {
        if (buffer[start_idx] == sync_pattern[0] &&
            buffer[start_idx+1] == sync_pattern[1] &&
            buffer[start_idx+2] == sync_pattern[2] &&
            buffer[start_idx+3] == sync_pattern[3]) {
            found_start = 1;
            break;
        }
    }
    
    if (!found_start) {
        // No complete sync pattern found in the buffer
        return buffer; // Return the beginning of the buffer as fallback
    }
    
    // Now find where the repeated sync patterns end
    uint16_t i = start_idx;
    while (i <= buffer_length - 4) {
        if (buffer[i] == sync_pattern[0] &&
            buffer[i+1] == sync_pattern[1] &&
            buffer[i+2] == sync_pattern[2] &&
            buffer[i+3] == sync_pattern[3]) {
            // Found a sync pattern, skip it
            i += 4;
        } else {
            // Found non-sync pattern
            return &buffer[i];
        }
    }
    
    // If we've reached this point, the entire buffer after start_idx is sync words
    // or there's not enough data left to form a complete non-sync pattern
    if (i < buffer_length) {
        return &buffer[i]; // Return pointer to remaining partial data
    }
    
    return NULL; // No non-sync data found
}


void FSK_init(
    uint16_t sync_01,
    uint16_t sync_23,
    void (*receive_callback)(char*, uint16_t)
) {
    modem_status = READY;
    FSK_receive_callback = receive_callback;
    _sync_23 = sync_23;
    _sync_01 = sync_01;
    FSK_configure();
    FSK_disable_tx();
    if(gEeprom.FSK_CONFIG.data.receive) {
        BK4819_FskEnableRx();
        FSK_set_data_length(RX_DATA_LENGTH);
    }
}

void FSK_store_packet_interrupt(const uint16_t interrupt_bits) {

	//const uint16_t rx_sync_flags   = BK4819_ReadRegister(BK4819_REG_0B);

	const bool rx_sync             = (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) ? true : false;
	const bool rx_fifo_almost_full = (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) ? true : false;
	const bool rx_finished         = (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) ? true : false;

	//UART_printf("\nMSG : S%i, F%i, E%i | %i", rx_sync, rx_fifo_almost_full, rx_finished, interrupt_bits);

	if (rx_sync) {
        switch(modem_status) {
            case READY:
                modem_status = SYNCING;
                #ifdef ENABLE_MESSENGER_FSK_MUTE
                    // prevent listening to fsk data and squelch (kamilsss655)
                    // CTCSS codes seem to false trigger the rx_sync
                    if(gCurrentCodeType == CODE_TYPE_OFF)
                        AUDIO_AudioPathOff();
                #endif
                gFSKWriteIndex = 0;
                break;
            case SYNCING:
                break;
            case RECEIVING:
                break;
            case SENDING:
                break;
        }
	
	}

	if (rx_fifo_almost_full) {
        switch(modem_status) {
            case READY:
                break;
            case SYNCING:
                BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
                [[fallthrough]];
            case RECEIVING:
                modem_status = RECEIVING;
                const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & (7u << 0);  // almost full threshold
                for (uint16_t i = 0; i < count; i++) {
                    const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
                    if (gFSKWriteIndex < sizeof(transit_buffer))
                        transit_buffer[gFSKWriteIndex++] = (word >> 0) & 0xff;
                    if (gFSKWriteIndex < sizeof(transit_buffer))
                        transit_buffer[gFSKWriteIndex++] = (word >> 8) & 0xff;
                }
                break;
            case SENDING:
                break;
        }

		SYSTEM_DelayMs(10);

	}

	if (rx_finished) {
        switch(modem_status) {
            case READY:
                break;
            case SYNCING:
                break;
            case RECEIVING:
                FSK_end_rx();
                break;
            case SENDING:
                break;
        }
	}
}

void FSK_end_rx() {
    // turn off the LEDs
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_FskClearFifo();
    if(gEeprom.FSK_CONFIG.data.receive)
        BK4819_FskEnableRx();
    modem_status = READY;

    if (gFSKWriteIndex > 2) {
        if(FSK_receive_callback){
            if(gEeprom.FSK_CONFIG.data.nrzi) {
                FSK_decode_nrzi(transit_buffer, gFSKWriteIndex, nrzi_sync_state);
                char * beginning = FSK_find_end_of_sync_words(transit_buffer, gFSKWriteIndex);
                if(beginning) { // please don't send a null pointer to the callback.
                    uint16_t new_len = gFSKWriteIndex - (beginning - transit_buffer);
                    FSK_receive_callback(beginning, new_len); // Potentially refiring an Ack.
                }
            } else {
                FSK_receive_callback(transit_buffer, gFSKWriteIndex); // Potentially refiring an Ack.
            }
        }
    }
    gFSKWriteIndex = 0;
    memset(transit_buffer, 0, TRANSIT_BUFFER_SIZE);
}

uint16_t FSK_set_data_length(uint16_t len) {
    uint8_t rounded_length = (((len + 1) / 2) * 2) + 2;
    
    // Read the current value of register 5D
    uint16_t current_value = BK4819_ReadRegister(BK4819_REG_5D);
    
    // Clear the bits we're going to modify
    current_value &= ~(0xFF80); // Clear bits 15:7
    
    // Extract the low 8 bits (bits 0-7 of rounded_length)
    uint8_t low_bits = rounded_length & 0xFF;
    
    // Extract the high 3 bits (bits 8-10 of rounded_length) and position them at bits 5-7
    uint8_t high_bits = (rounded_length >> 8) & 0x07;
    
    // Combine the new bits with the current value
    uint16_t new_value = current_value | (low_bits << 8) | (high_bits << 5);
    
    // Write the modified value back to the register
    BK4819_WriteRegister(BK4819_REG_5D, new_value);
    
    return rounded_length;
}



uint16_t FSK_get_data_length() {
    // Read the current value of register 5D
    uint16_t reg_value = BK4819_ReadRegister(BK4819_REG_5D);
    
    // Extract the low 8 bits from bits 15:8
    uint8_t low_bits = (reg_value >> 8) & 0xFF;
    
    // Extract the high 3 bits from bits 7:5
    uint8_t high_bits = (reg_value >> 5) & 0x07;
    
    // Combine them to get the full 11-bit length value
    uint16_t length = (high_bits << 8) | low_bits;
    
    return length;
}


void FSK_configure() {
    uint16_t TONE1_FREQ;
    uint16_t TONE2_FREQ;
    // REG_70
    //
    // <15>   0 Enable TONE1
    //        1 = Enable
    //        0 = Disable
    //
    // <14:8> 0 TONE1 tuning gain
    //        0 ~ 127
    //
    // <7>    0 Enable TONE2
    //        1 = Enable
    //        0 = Disable
    //
    // <6:0>  0 TONE2/FSK tuning gain
    //        0 ~ 127
    //
    switch(gEeprom.FSK_CONFIG.data.modulation)
    {
        case MOD_AFSK_1200:
        case MOD_BELL_202:
            TONE1_FREQ = 22714u;
            TONE2_FREQ = 12389u;
            break;
        case MOD_FSK_700:
            TONE2_FREQ = 7227u;
            break;
        case MOD_FSK_450:
            TONE2_FREQ = 4646u;
            break;
    }

    switch(gEeprom.FSK_CONFIG.data.modulation)
    {
        case MOD_BELL_202:
            BK4819_WriteRegister(BK4819_REG_70,
                ( 1u << 15) |    // 1
                ( 0u <<  8) |    // 0
                ( 1u <<  7) |    // 1
                (96u <<  0));    // 96

            // TONE 1
            BK4819_WriteRegister(BK4819_REG_71, TONE1_FREQ);
            // TONE 2
            BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
            break;
        default:
            BK4819_WriteRegister(BK4819_REG_70,
                ( 0u << 15) |    // 0
                ( 0u <<  8) |    // 0
                ( 1u <<  7) |    // 1
                (96u <<  0));    // 96

            // TONE 2 only
            BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
            break;
    }

    
    switch(gEeprom.FSK_CONFIG.data.modulation)
    {
        case MOD_FSK_700:
        case MOD_FSK_450:
            BK4819_WriteRegister(BK4819_REG_58,
                (0u << 13) |		// 1 FSK TX mode selection
                                    //   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800 TX
                                    //   2 = ???
                                    //   3 = FFSK 1200 / 2400 TX
                                    //   4 = ???
                                    //   5 = NOAA SAME TX
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (0u << 10) |		// 0 FSK RX mode selection
                                    //   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
                                    //   1 = ???
                                    //   2 = ???
                                    //   3 = ???
                                    //   4 = FFSK 1200 / 2400 RX
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = FFSK 1200 / 1800 RX
                                    //
                (3u << 8) |			// 0 FSK RX gain
                                    //   0 ~ 3
                                    //
                (0u << 6) |			// 0 ???
                                    //   0 ~ 3
                                    //
                (0u << 4) |			// 0 FSK preamble type selection
                                    //   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
                                    //   1 = ???
                                    //   2 = 0x55
                                    //   3 = 0xAA
                                    //
                (0u << 1) |			// 1 FSK RX bandwidth setting
                                    //   0 = FSK 1.2K .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800
                                    //   2 = NOAA SAME RX
                                    //   3 = ???
                                    //   4 = FSK 2.4K and FFSK 1200 / 2400
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (1u << 0));			// 1 FSK enable
                                    //   0 = disable
                                    //   1 = enable
        break;
        case MOD_BELL_202:
            BK4819_WriteRegister(BK4819_REG_58,
                (1u << 13) |		// 1 FSK TX mode selection
                                    //   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800 TX
                                    //   2 = ???
                                    //   3 = FFSK 1200 / 2400 TX
                                    //   4 = ???
                                    //   5 = NOAA SAME TX
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (7u << 10) |		// 0 FSK RX mode selection
                                    //   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
                                    //   1 = ???
                                    //   2 = ???
                                    //   3 = ???
                                    //   4 = FFSK 1200 / 2400 RX
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = FFSK 1200 / 1800 RX
                                    //
                (3u << 8) |			// 0 FSK RX gain
                                    //   0 ~ 3
                                    //
                (0u << 6) |			// 0 ???
                                    //   0 ~ 3
                                    //
                (0u << 4) |			// 0 FSK preamble type selection
                                    //   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
                                    //   1 = ???
                                    //   2 = 0x55
                                    //   3 = 0xAA
                                    //
                (1u << 1) |			// 1 FSK RX bandwidth setting
                                    //   0 = FSK 1.2K .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800
                                    //   2 = NOAA SAME RX
                                    //   3 = ???
                                    //   4 = FSK 2.4K and FFSK 1200 / 2400
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (1u << 0));			// 1 FSK enable
                                    //   0 = disable
                                    //   1 = enable
            break;
        case MOD_AFSK_1200:
            BK4819_WriteRegister(BK4819_REG_58,
                (1u << 13) |		// 1 FSK TX mode selection
                                    //   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800 TX
                                    //   2 = ???
                                    //   3 = FFSK 1200 / 2400 TX
                                    //   4 = ???
                                    //   5 = NOAA SAME TX
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (7u << 10) |		// 0 FSK RX mode selection
                                    //   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
                                    //   1 = ???
                                    //   2 = ???
                                    //   3 = ???
                                    //   4 = FFSK 1200 / 2400 RX
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = FFSK 1200 / 1800 RX
                                    //
                (3u << 8) |			// 0 FSK RX gain
                                    //   0 ~ 3
                                    //
                (0u << 6) |			// 0 ???
                                    //   0 ~ 3
                                    //
                (0u << 4) |			// 0 FSK preamble type selection
                                    //   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
                                    //   1 = ???
                                    //   2 = 0x55
                                    //   3 = 0xAA
                                    //
                (1u << 1) |			// 1 FSK RX bandwidth setting
                                    //   0 = FSK 1.2K .. no tones, direct FM
                                    //   1 = FFSK 1200 / 1800
                                    //   2 = NOAA SAME RX
                                    //   3 = ???
                                    //   4 = FSK 2.4K and FFSK 1200 / 2400
                                    //   5 = ???
                                    //   6 = ???
                                    //   7 = ???
                                    //
                (1u << 0));			// 1 FSK enable
                                    //   0 = disable
                                    //   1 = enable
            break;
    }

    if(gEeprom.FSK_CONFIG.data.nrzi) {
        processed_sync_01 = _sync_01;
        processed_sync_23 = _sync_23;
        FSK_encode_nrzi((char*) &processed_sync_01, 2, 1);

        // Calculate final state after first encoding
        // Get the last byte of processed_sync_01 (assuming little-endian)
        char last_byte = ((char*)&processed_sync_01)[1];
        // The last bit processed is the LSB (bit 0)
        nrzi_sync_state = (last_byte & 0x01) ? 1 : 0;

        // Encode second sync word with the final state from first encoding
        FSK_encode_nrzi((char*) &processed_sync_23, 2, nrzi_sync_state);


        // Get the last byte of processed_sync_23 (assuming little-endian)
        last_byte = ((char*)&processed_sync_23)[1];
        // and store it so buffers can be adapted accordingly
        nrzi_sync_state = (last_byte & 0x01) ? 1 : 0;

    } else {
        processed_sync_01 = _sync_01;
        processed_sync_23 = _sync_23;
    }

    // REG_5A .. bytes 0 & 1 sync pattern
    //
    // <15:8> sync byte 0
    // < 7:0> sync byte 1
    BK4819_WriteRegister(BK4819_REG_5A, processed_sync_01);

    // REG_5B .. bytes 2 & 3 sync pattern
    //
    // <15:8> sync byte 2
    // < 7:0> sync byte 3
    BK4819_WriteRegister(BK4819_REG_5B, processed_sync_23);

    // disable CRC
    BK4819_WriteRegister(BK4819_REG_5C, 0x5625);

    // set the almost empty tx and almost full rx threshold
    BK4819_WriteRegister(BK4819_REG_5E, (TX_FIFO_THRESHOLD << 3) | (RX_FIFO_THRESHOLD << 0));  // 0 ~ 127, 0 ~ 7

    // packet size .. sync + packet - size of a single packet

    // size -= (fsk_reg59 & (1u << 3)) ? 4 : 2;
    // BK4819_WriteRegister(BK4819_REG_5D, ((sizeof(dataPacket.serializedArray)) << 8));

    // clear FIFO's
    BK4819_FskClearFifo();


    // configure main FSK params
    uint8_t preamble = 15u;
    if(gEeprom.FSK_CONFIG.data.nrzi) {
        preamble = 0u; // don't use or expect any preamble for NRZI as none of the preambles are valid
    }

    BK4819_WriteRegister(BK4819_REG_59,
        (0u        <<       15) |   // 0/1     1 = clear TX FIFO
        (0u        <<       14) |   // 0/1     1 = clear RX FIFO
        (0u        <<       13) |   // 0/1     1 = scramble
        (0u        <<       12) |   // 0/1     1 = enable RX
        (0u        <<       11) |   // 0/1     1 = enable TX
        (0u        <<       10) |   // 0/1     1 = invert data when RX
        (0u        <<        9) |   // 0/1     1 = invert data when TX
        (0u        <<        8) |   // 0/1     ???
        (preamble  <<        4) |   // 0 ~ 15  preamble length .. bit toggling
        (1u        <<        3) |   // 0/1     sync length
        (0u        <<        0)     // 0 ~ 7   ???
    );


    // clear interupts
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

void FSK_send_data(char * data, uint16_t len) {
    memset(transit_buffer, 0, TRANSIT_BUFFER_SIZE);

    if( modem_status != READY) return;

    if(len == 0) return;

    if(gEeprom.FSK_CONFIG.data.nrzi) {
        memcpy(transit_buffer + 4 * NRZI_PREAMBLE, data, len);
        // duplicate the sync bytes to increase chances of digital read lock
        for(uint8_t i = 0; i < NRZI_PREAMBLE; i++) {
            transit_buffer[i*4]     = (uint8_t)(_sync_01 & 0xFF);        // Low byte of first sync word
            transit_buffer[i*4 + 1] = (uint8_t)((_sync_01 >> 8) & 0xFF); // High byte of first sync word
            transit_buffer[i*4 + 2] = (uint8_t)(_sync_23 & 0xFF);        // Low byte of second sync word
            transit_buffer[i*4 + 3] = (uint8_t)((_sync_23 >> 8) & 0xFF); // High byte of second sync word
        }
        FSK_encode_nrzi(transit_buffer, (4 * NRZI_PREAMBLE) + len, nrzi_sync_state);
    } else {
        memcpy(transit_buffer, data, len);
    }

    if(RADIO_GetVfoState() != VFO_STATE_NORMAL){
        gRequestDisplayScreen = DISPLAY_MAIN;
        return;
    }

    FSK_disable_rx();

    modem_status = SENDING;

    RADIO_PrepareTX();

    RADIO_SetVfoState(VFO_STATE_NORMAL);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);

    BK4819_DisableDTMF();

    // mute the mic during TX
    gMuteMic = true;

    SYSTEM_DelayMs(50);


	// turn off CTCSS/CDCSS during FFSK
	const uint16_t css_val = BK4819_ReadRegister(BK4819_REG_51);
	BK4819_WriteRegister(BK4819_REG_51, 0);

	// set the FM deviation level
	const uint16_t dev_val = BK4819_ReadRegister(BK4819_REG_40);

	{
		uint16_t deviation;
		switch (gEeprom.VfoInfo[gEeprom.TX_VFO].CHANNEL_BANDWIDTH)
		{
			case BK4819_FILTER_BW_WIDE:            deviation =  1300; break; // 20k // measurements by kamilsss655
			case BK4819_FILTER_BW_NARROW:          deviation =  1200; break; // 10k
			// case BK4819_FILTER_BW_NARROWAVIATION:  deviation =  850; break;  // 5k
			// case BK4819_FILTER_BW_NARROWER:        deviation =  850; break;  // 5k
			// case BK4819_FILTER_BW_NARROWEST:	      deviation =  850; break;  // 5k
			default:                               deviation =  850;  break;  // 5k
		}

		//BK4819_WriteRegister(0x40, (3u << 12) | (deviation & 0xfff));
		BK4819_WriteRegister(BK4819_REG_40, (dev_val & 0xf000) | (deviation & 0xfff));
	}

	// REG_2B   0
	//
	// <15> 1 Enable CTCSS/CDCSS DC cancellation after FM Demodulation   1 = enable 0 = disable
	// <14> 1 Enable AF DC cancellation after FM Demodulation            1 = enable 0 = disable
	// <10> 0 AF RX HPF 300Hz filter     0 = enable 1 = disable
	// <9>  0 AF RX LPF 3kHz filter      0 = enable 1 = disable
	// <8>  0 AF RX de-emphasis filter   0 = enable 1 = disable
	// <2>  0 AF TX HPF 300Hz filter     0 = enable 1 = disable
	// <1>  0 AF TX LPF filter           0 = enable 1 = disable
	// <0>  0 AF TX pre-emphasis filter  0 = enable 1 = disable
	//
	// disable the 300Hz HPF and FM pre-emphasis filter
	//
	const uint16_t filt_val = BK4819_ReadRegister(BK4819_REG_2B);
	BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | (1u << 0));
	
    uint16_t transmit_len;
    if(gEeprom.FSK_CONFIG.data.nrzi)
        transmit_len = FSK_set_data_length((4 * NRZI_PREAMBLE) + len);
    else
        transmit_len = FSK_set_data_length(len);


    // Enable FSK TX
    BK4819_FskEnableTx();

    // this delay REALLY has to be here. Potentially replaceable with an interrupt wait but I don't know.
    // if you don't wait, bytes in the FIFO will just not be properly understood by the modem
	SYSTEM_DelayMs(100);


    uint16_t tx_index = 0;
    for (uint16_t j = 0; tx_index < transmit_len && j < TX_FIFO_THRESHOLD; tx_index += 2, j++) {
        if (tx_index + 1 < transmit_len) {
            BK4819_WriteRegister(BK4819_REG_5F, (transit_buffer[tx_index + 1] << 8) | transit_buffer[tx_index]);
        } else {
            // Handle odd length by padding with zero
            BK4819_WriteRegister(BK4819_REG_5F, 0x00 | transit_buffer[tx_index]);
        }
    }
    do {
        for (uint16_t j = 0; tx_index < transmit_len && j < TX_FIFO_SEGMENT; tx_index += 2, j++) {
            if (tx_index + 1 < transmit_len) {
                BK4819_WriteRegister(BK4819_REG_5F, (transit_buffer[tx_index + 1] << 8) | transit_buffer[tx_index]);
            } else {
                // Handle odd length by padding with zero
                BK4819_WriteRegister(BK4819_REG_5F, 0x00 | transit_buffer[tx_index]);
            }
        }

        // Allow up to 1s
        uint16_t timeout = 1000 / 5;

        while (timeout-- > 0)
        {
            SYSTEM_DelayMs(5);
            uint16_t reg_0c = BK4819_ReadRegister(BK4819_REG_0C);
            
            if (reg_0c & (1u << 0))
            {   // We have interrupt flags
                uint16_t reg_02 = BK4819_ReadRegister(BK4819_REG_02);
                
                if (reg_02 & BK4819_REG_02_FSK_TX_FINISHED)
                {
                    reg_02 &= ~BK4819_REG_02_FSK_TX_FINISHED;
                    break;
                }
                else if (reg_02 & (BK4819_REG_02_FSK_FIFO_ALMOST_EMPTY))
                {
                    reg_02 &= ~BK4819_REG_02_FSK_FIFO_ALMOST_EMPTY;
                    break;
                }
                
                // Clear only the flags we've handled
                BK4819_WriteRegister(BK4819_REG_02, reg_02);
            }
        }

        if(timeout == 0) // also exit if any segment timed out
            break;

    } while(tx_index < transmit_len);



    SYSTEM_DelayMs(100);

	// disable TX
    FSK_disable_tx();

	// restore FM deviation level
	BK4819_WriteRegister(BK4819_REG_40, dev_val);

	// restore TX/RX filtering
	BK4819_WriteRegister(BK4819_REG_2B, filt_val);

	// restore the CTCSS/CDCSS setting
	BK4819_WriteRegister(BK4819_REG_51, css_val);

    SYSTEM_DelayMs(50);

    APP_EndTransmission(false);

    // this must be run after end of TX, otherwise radio will still TX transmit without even RED LED on
    FUNCTION_Select(FUNCTION_FOREGROUND);

    RADIO_SetVfoState(VFO_STATE_NORMAL);

    // disable mic mute after TX
    gMuteMic = false;

    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);

    // clear packet buffer
    memset(transit_buffer, 0, TRANSIT_BUFFER_SIZE);
    BK4819_FskClearFifo();
    FSK_configure();
    FSK_disable_tx();
    if(gEeprom.FSK_CONFIG.data.receive){
        BK4819_FskEnableRx();
        FSK_set_data_length(RX_DATA_LENGTH);
    }
    modem_status = READY;
}

