#ifdef APRS
#include "app/ax25.h"

uint16_t ax25_crc_update(uint16_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ AX25_FCS_POLY;
        else
            crc >>= 1;
    }
    return crc;
}

uint16_t ax25_compute_fcs(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
        crc = ax25_crc_update(crc, data[i]);
    return ~crc;
}

void ax25_encode_address(char *out, const char *callsign, uint8_t ssid) {
    memset(out, ' ', 6);
    strncpy(out, callsign, strlen(callsign) > 6 ? 6 : strlen(callsign));
    for (int i = 0; i < 6; i++)
        out[i] <<= 1;
    out[6] = ((ssid & 0x0F) << 1) | 0x60;
}

void ax25_serialize_frame(AX25Frame *frame, uint8_t *buffer, size_t *len) {
    size_t pos = 0;
    buffer[pos++] = AX25_FLAG;
    memcpy(&buffer[pos], frame->dest, 7); pos += 7;
    memcpy(&buffer[pos], frame->source, 7); pos += 7;
    buffer[pos++] = AX25_CONTROL_UI;
    buffer[pos++] = AX25_PID_NO_LAYER3;
    size_t payload_len = strlen(frame->payload);
    memcpy(&buffer[pos], frame->payload, payload_len);
    pos += payload_len;
    frame->fcs = ax25_compute_fcs(&buffer[1], pos - 1);
    buffer[pos++] = frame->fcs & 0xFF;
    buffer[pos++] = (frame->fcs >> 8) & 0xFF;
    buffer[pos++] = AX25_FLAG;
    *len = pos;
}

#endif