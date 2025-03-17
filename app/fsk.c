#include <stdint.h>

#include "driver/st7565.h"
#include "driver/bk4819.h"
#include "settings.h"
#include "driver/system.h"
#include "app.h"
#include "functions.h"
#include "app/fsk.h"


uint16_t gFSKWriteIndex = 0;

ModemStatus modem_status = READY;

void FSK_configure(uint8_t rx, uint16_t size) {
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
    switch(gEeprom.MESSENGER_CONFIG.data.modulation)
    {
        case MOD_AFSK_1200:
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

    switch(gEeprom.MESSENGER_CONFIG.data.modulation)
    {
        case MOD_AFSK_1200:
            BK4819_WriteRegister(BK4819_REG_70,
                ( 1u << 15) |    // 1 // APRS uses both tones
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

    
    switch(gEeprom.MESSENGER_CONFIG.data.modulation)
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
        break;
    }

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

    // set the almost full threshold
    if(rx)
        BK4819_WriteRegister(BK4819_REG_5E, (64u << 3) | (1u << 0));  // 0 ~ 127, 0 ~ 7

    // packet size .. sync + packet - size of a single packet

    // size -= (fsk_reg59 & (1u << 3)) ? 4 : 2;
    if(rx)
        size = (((size + 1) / 2) * 2) + 2;             // round up to even, else FSK RX doesn't work

    BK4819_WriteRegister(BK4819_REG_5D, (size << 8));
    // BK4819_WriteRegister(BK4819_REG_5D, ((sizeof(dataPacket.serializedArray)) << 8));

    // clear FIFO's
    BK4819_FskClearFifo();


    // configure main FSK params

    switch(gEeprom.MESSENGER_CONFIG.data.modulation)
    {
        case MOD_AFSK_1200:
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
                ((rx ? 0u : 15u) <<  4) |   // 0 ~ 15  preamble length .. bit toggling
                (0u        <<        3) |   // 0/1     sync length
                (0u        <<        0)     // 0 ~ 7   ???
            );
            break;
    }


    // clear interupts
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

void FSK_send_data(char * data, uint16_t len) {

    modem_status = SENDING;

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
	
	FSK_configure(false, len);


	SYSTEM_DelayMs(100);

	{	// load the entire packet data into the TX FIFO buffer

		for (uint16_t i = 0, j = 0; i < len; i += 2, j++) {
        	BK4819_WriteRegister(BK4819_REG_5F, (data[i + 1] << 8) | data[i]);
    	}
	}

	// enable FSK TX
	BK4819_FskEnableTx();

	{
		// allow up to 310ms for the TX to complete
		// if it takes any longer then somethings gone wrong, we shut the TX down
		uint16_t timeout = 1290;

		while (timeout-- > 0)
		{
			SYSTEM_DelayMs(5);
			if (BK4819_ReadRegister(BK4819_REG_0C) & (1u << 0))
			{	// we have interrupt flags
				BK4819_WriteRegister(BK4819_REG_02, 0);
				if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
					timeout = 0;       // TX is complete
			}
		}
	}
	//BK4819_WriteRegister(BK4819_REG_02, 0);

	SYSTEM_DelayMs(100);

	// disable TX
	FSK_configure(true, len);

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

    MSG_EnableRX(true);

    // clear packet buffer
    MSG_ClearPacketBuffer();

    modem_status = READY;

}
