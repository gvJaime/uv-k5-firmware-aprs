#include <stdint.h>

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
#define RX_FIFO_THRESHOLD 1u

char transit_buffer[TRANSIT_BUFFER_SIZE];

uint16_t gFSKWriteIndex = 0;

void (*FSK_receive_callback)(char*);

ModemStatus modem_status = READY;

void FSK_disable_tx() {
	const uint16_t fsk_reg59 = BK4819_ReadRegister(BK4819_REG_59);
	BK4819_WriteRegister(BK4819_REG_59, fsk_reg59 & ~(1u << 11) );
}

void FSK_disable_rx() {
	const uint16_t fsk_reg59 = BK4819_ReadRegister(BK4819_REG_59);
	BK4819_WriteRegister(BK4819_REG_59, fsk_reg59 & ~(1u << 12) );
}

void FSK_init(void (*receive_callback)(char*) ) {
    modem_status = READY;
    FSK_receive_callback = receive_callback; 
    FSK_configure();
    FSK_disable_tx();
    if(gEeprom.FSK_CONFIG.data.receive)
        BK4819_FskEnableRx();
}

void FSK_store_packet_interrupt(const uint16_t interrupt_bits) {

	//const uint16_t rx_sync_flags   = BK4819_ReadRegister(BK4819_REG_0B);

	const bool rx_sync             = (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) ? true : false;
	const bool rx_fifo_almost_full = (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) ? true : false;
	const bool rx_finished         = (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) ? true : false;

	//UART_printf("\nMSG : S%i, F%i, E%i | %i", rx_sync, rx_fifo_almost_full, rx_finished, interrupt_bits);

	if (rx_sync) {
		#ifdef ENABLE_MESSENGER_FSK_MUTE
			// prevent listening to fsk data and squelch (kamilsss655)
			// CTCSS codes seem to false trigger the rx_sync
			if(gCurrentCodeType == CODE_TYPE_OFF)
				AUDIO_AudioPathOff();
		#endif
		gFSKWriteIndex = 0;
		modem_status = RECEIVING;
	}

	if (rx_fifo_almost_full && modem_status == RECEIVING) {

		const uint16_t count = BK4819_ReadRegister(BK4819_REG_5E) & (7u << 0);  // almost full threshold
		for (uint16_t i = 0; i < count; i++) {
			const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
            if (gFSKWriteIndex < sizeof(transit_buffer))
                transit_buffer[gFSKWriteIndex++] = (word >> 0) & 0xff;
            if (gFSKWriteIndex < sizeof(transit_buffer))
                transit_buffer[gFSKWriteIndex++] = (word >> 8) & 0xff;
		}

		SYSTEM_DelayMs(10);

	}

	if (rx_finished) {
		// turn off green LED
		BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);


		if (gFSKWriteIndex > 2) {
            if(FSK_receive_callback)
                // TODO: un NRZI maybe?
                FSK_receive_callback(transit_buffer);
		}
        memset(transit_buffer, 0, TRANSIT_BUFFER_SIZE);
		BK4819_FskClearFifo();
        if(gEeprom.FSK_CONFIG.data.receive)
            BK4819_FskEnableRx();
		modem_status = READY;
		gFSKWriteIndex = 0;
	}
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
    
    return len;
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
    #ifdef ENABLE_APRS
        uint16_t TONE1_FREQ;
    #endif
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
            #ifdef ENABLE_APRS
                TONE1_FREQ = 22714u;
            #endif
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
        case MOD_AFSK_1200:
            #ifdef ENABLE_APRS
                BK4819_WriteRegister(BK4819_REG_70,
                    ( 1u << 15) |    // 1 // APRS uses both tones
                    ( 0u <<  8) |    // 0
                    ( 1u <<  7) |    // 1
                    (96u <<  0));    // 96

                // TONE 1
                BK4819_WriteRegister(BK4819_REG_71, TONE1_FREQ);
                // TONE 2
                BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
            #else
                BK4819_WriteRegister(BK4819_REG_70,
                    ( 0u << 15) |    // 0
                    ( 0u <<  8) |    // 0
                    ( 1u <<  7) |    // 1
                    (96u <<  0));    // 96

                // TONE 2
                BK4819_WriteRegister(BK4819_REG_72, TONE2_FREQ);
            #endif
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
        case MOD_AFSK_1200:
            #ifdef ENABLE_APRS
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
                    (4u << 1) |			// 1 FSK RX bandwidth setting
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
            #else
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
            #endif
        break;
    }

    #ifdef ENABLE_APRS
        // REG_5A .. bytes 0 & 1 sync pattern
        //
        // <15:8> sync byte 0
        // < 7:0> sync byte 1
        BK4819_WriteRegister(BK4819_REG_5A, 0x7e7e);

        // REG_5B .. bytes 2 & 3 sync pattern
        //
        // <15:8> sync byte 2
        // < 7:0> sync byte 3
        BK4819_WriteRegister(BK4819_REG_5B, 0x7e7e);

        // disable CRC
        BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
    #else

        // REG_5A .. bytes 0 & 1 sync pattern
        //
        // <15:8> sync byte 0
        // < 7:0> sync byte 1
        BK4819_WriteRegister(BK4819_REG_5A, 0x3072);

        // REG_5B .. bytes 2 & 3 sync pattern
        //
        // <15:8> sync byte 2
        // < 7:0> sync byte 3
        BK4819_WriteRegister(BK4819_REG_5B, 0x576C);

        // disable CRC
        BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
    #endif

    // set the almost empty tx and almost full rx threshold
    BK4819_WriteRegister(BK4819_REG_5E, (TX_FIFO_THRESHOLD << 3) | (RX_FIFO_THRESHOLD << 0));  // 0 ~ 127, 0 ~ 7

    // packet size .. sync + packet - size of a single packet

    // size -= (fsk_reg59 & (1u << 3)) ? 4 : 2;
    // BK4819_WriteRegister(BK4819_REG_5D, ((sizeof(dataPacket.serializedArray)) << 8));

    // clear FIFO's
    BK4819_FskClearFifo();


    // configure main FSK params

    switch(gEeprom.FSK_CONFIG.data.modulation)
    {
        case MOD_AFSK_1200:
        #ifdef ENABLE_APRS
            BK4819_WriteRegister(BK4819_REG_59,
                (0u        <<       15) |   // 0/1     1 = clear TX FIFO
                (0u        <<       14) |   // 0/1     1 = clear RX FIFO
                (0u        <<       13) |   // 0/1     1 = scramble
                (0u        <<       12) |   // 0/1     1 = enable RX
                (0u        <<       11) |   // 0/1     1 = enable TX
                (0u        <<       10) |   // 0/1     1 = invert data when RX
                (0u        <<        9) |   // 0/1     1 = invert data when TX
                (0u        <<        8) |   // 0/1     ???
                (0u        <<        4) |   // 0 ~ 15  preamble length .. bit toggling
                (0u        <<        3) |   // 0/1     sync length
                (0u        <<        0)     // 0 ~ 7   ???
            );
        #else
            BK4819_WriteRegister(BK4819_REG_59,
                (0u        <<       15) |   // 0/1     1 = clear TX FIFO
                (0u        <<       14) |   // 0/1     1 = clear RX FIFO
                (0u        <<       13) |   // 0/1     1 = scramble
                (0u        <<       12) |   // 0/1     1 = enable RX
                (0u        <<       11) |   // 0/1     1 = enable TX
                (0u        <<       10) |   // 0/1     1 = invert data when RX
                (0u        <<        9) |   // 0/1     1 = invert data when TX
                (0u        <<        8) |   // 0/1     ???
                (15u       <<        4) |   // 0 ~ 15  preamble length .. bit toggling
                (1u        <<        3) |   // 0/1     sync length
                (0u        <<        0)     // 0 ~ 7   ???
            );
        #endif
            break;
        case MOD_FSK_700:
        case MOD_FSK_450:
            BK4819_WriteRegister(BK4819_REG_59,
                (0u        <<       15) |   // 0/1     1 = clear TX FIFO
                (0u        <<       14) |   // 0/1     1 = clear RX FIFO
                (0u        <<       13) |   // 0/1     1 = scramble
                (0u        <<       12) |   // 0/1     1 = enable RX
                (0u        <<       11) |   // 0/1     1 = enable TX
                (0u        <<       10) |   // 0/1     1 = invert data when RX
                (0u        <<        9) |   // 0/1     1 = invert data when TX
                (0u        <<        8) |   // 0/1     ???
                (15u       <<        4) |   // 0 ~ 15  preamble length .. bit toggling
                (1u        <<        3) |   // 0/1     sync length
                (0u        <<        0)     // 0 ~ 7   ???
            );
            break;
    }


    // clear interupts
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

void FSK_send_data(char * data, uint16_t len) {
    memset(transit_buffer, 0, TRANSIT_BUFFER_SIZE);

    if( modem_status != READY) return;

    if(len == 0) return;

    // TODO: NRZI and or any other weirdness.
    memcpy(transit_buffer, data, len);

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
	

	SYSTEM_DelayMs(100);

    uint16_t old_length = FSK_get_data_length();
    FSK_set_data_length(len);

    // Load the entire packet data into the TX FIFO buffer

    uint16_t tx_index = 0;

    // use full FIFO on first pass
    for (uint16_t j = 0; tx_index < len && j < TX_FIFO_SEGMENT + TX_FIFO_THRESHOLD; tx_index += 2, j++) {
        if (tx_index + 1 < len) {
            BK4819_WriteRegister(BK4819_REG_5F, (transit_buffer[tx_index + 1] << 8) | transit_buffer[tx_index]);
        } else {
            // Handle odd length by padding with zero
            BK4819_WriteRegister(BK4819_REG_5F, 0x00 | transit_buffer[tx_index]);
        }
    }
    
    // Enable FSK TX
    BK4819_FskEnableTx();
    while(tx_index < len) {
        // Allow up to 310ms for the TX to complete
        uint16_t timeout = 1290;
        uint8_t tx_finished = 0;

        while (timeout-- > 0 && !tx_finished)
        {
            SYSTEM_DelayMs(5);
            uint16_t reg_0c = BK4819_ReadRegister(BK4819_REG_0C);
            
            if (reg_0c & (1u << 0))
            {   // We have interrupt flags
                uint16_t reg_02 = BK4819_ReadRegister(BK4819_REG_02);
                
                if (reg_02 & BK4819_REG_02_FSK_TX_FINISHED)
                {
                    reg_02 &= ~BK4819_REG_02_FSK_TX_FINISHED;
                    tx_finished = 1;
                }
                else if (reg_02 & (BK4819_REG_02_FSK_FIFO_ALMOST_EMPTY))
                {
                    reg_02 &= ~BK4819_REG_02_FSK_FIFO_ALMOST_EMPTY;
                    // get out, and load more bytes
                    break;
                }
                
                // Clear only the flags we've handled
                BK4819_WriteRegister(BK4819_REG_02, reg_02);
            }
        }

        if(tx_finished || tx_index >= len || timeout == 0) // also exit if any segment timed out
            break;

        // if tx is not finished, load segment.
        for (uint16_t j = 0; tx_index < len && j < TX_FIFO_SEGMENT; tx_index += 2, j++) {
            if (tx_index + 1 < len) {
                BK4819_WriteRegister(BK4819_REG_5F, (transit_buffer[tx_index + 1] << 8) | transit_buffer[tx_index]);
            } else {
                // Handle odd length by padding with zero
                BK4819_WriteRegister(BK4819_REG_5F, 0x00 | transit_buffer[tx_index]);
            }
        }
    }



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
        FSK_set_data_length(old_length);
    }
    modem_status = READY;
}

