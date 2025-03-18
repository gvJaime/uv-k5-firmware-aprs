
#include <string.h>
#include <stdint.h>

#include "app/ax25.h"
#include "external/printf/printf.h"

uint8_t AX25_insert_destination(AX25UIFrame * self, uint8_t * callsign, uint8_t ssid) {
    if (self == NULL || callsign == NULL) {
        return 0; // Error - invalid parameters
    }
    
    // Reset frame if it's the first address being added
    self->len = 0;
    self->readable = 0;
    
    // Encode callsign (left-shifted by 1 bit)
    uint8_t i;
    for (i = 0; i < 6; i++) {
        if (i < strlen((char*)callsign)) {
            self->raw_buffer[self->len++] = callsign[i] << 1;
        } else {
            self->raw_buffer[self->len++] = ' ' << 1; // Pad with spaces
        }
    }
    
    // Encode SSID byte: 111SSID0 for destination
    self->raw_buffer[self->len++] = 0xE0 | ((ssid & 0x0F) << 1);
    
    return 1; // Success
}

uint8_t AX25_insert_source(AX25UIFrame * self, uint8_t * callsign, uint8_t ssid) {
    if (self == NULL || callsign == NULL || self->len < 7) {
        return 0; // Error - invalid parameters or destination not set
    }
    
    // Encode callsign (left-shifted by 1 bit)
    uint8_t i;
    for (i = 0; i < CALLSIGN_SIZE - 1; i++) {
        if (i < strlen((char*)callsign)) {
            self->raw_buffer[self->len++] = callsign[i] << 1;
        } else {
            self->raw_buffer[self->len++] = ' ' << 1; // Pad with spaces
        }
    }
    
    // Encode SSID byte: 111SSID1 for source (assuming it's the last address)
    self->raw_buffer[self->len++] = 0xE0 | ((ssid & 0x0F) << 1) | 0x01;
    
    // Set up control and PID fields
    self->control = &self->raw_buffer[self->len];
    self->raw_buffer[self->len++] = 0x03; // Control field for UI frames
    
    self->pid = &self->raw_buffer[self->len];
    self->raw_buffer[self->len++] = 0xF0; // Protocol ID for AX.25
    
    self->info = &self->raw_buffer[self->len]; // Point to start of info field
    
    return 1; // Success
}

uint8_t AX25_insert_paths(AX25UIFrame * self, uint8_t ** path_strings, uint8_t paths) {
    if (self == NULL || path_strings == NULL || self->len < 14) {
        return 0; // Error - invalid parameters or source/dest not set
    }
    
    // If we already have control/PID fields, we need to move them
    if (self->control != NULL && self->pid != NULL) {
        // Clear the last address's SSID "last bit"
        self->raw_buffer[self->len - 9] &= 0xFE;
    }
    
    // Add each path
    for (uint8_t path_idx = 0; path_idx < paths; path_idx++) {
        uint8_t *path_string = path_strings[path_idx];
        if (path_string == NULL) continue;
        
        uint8_t callsign[7] = {0}; // Store the callsign part
        uint8_t ssid = 0;          // Default SSID is 0
        uint8_t has_ssid = 0;      // Flag to indicate if SSID was specified
        
        // Look for hyphen to separate callsign from SSID
        uint8_t i;
        for (i = 0; i < 7; i++) {
            if (path_string[i] == '-') {
                has_ssid = 1;
                break;
            }
            if (path_string[i] == 0 || i == 6) break;
            callsign[i] = path_string[i];
        }
        
        // If we found a hyphen, parse the SSID value that follows
        if (has_ssid && i < 6) {
            uint8_t ssid_pos = i + 1;
            // Parse numeric SSID value
            while (ssid_pos < 7 && path_string[ssid_pos] >= '0' && path_string[ssid_pos] <= '9') {
                ssid = ssid * 10 + (path_string[ssid_pos] - '0');
                ssid_pos++;
            }
        }
        
        // Encode callsign (left-shifted by 1 bit)
        for (i = 0; i < 6; i++) {
            if (i < strlen((char*)callsign)) {
                self->raw_buffer[self->len++] = callsign[i] << 1;
            } else {
                self->raw_buffer[self->len++] = ' ' << 1; // Pad with spaces
            }
        }
        
        // For the last path, set the last bit to 1
        uint8_t last_bit = (path_idx == paths - 1) ? 0x01 : 0x00;
        
        // Encode SSID byte: 111SSIDL where L is the last bit
        self->raw_buffer[self->len++] = 0xE0 | ((ssid & 0x0F) << 1) | last_bit;
    }
    
    // Set up control and PID fields after all addresses
    self->control = &self->raw_buffer[self->len];
    self->raw_buffer[self->len++] = 0x03; // Control field for UI frames
    
    self->pid = &self->raw_buffer[self->len];
    self->raw_buffer[self->len++] = 0xF0; // Protocol ID for AX.25
    
    self->info = &self->raw_buffer[self->len]; // Point to start of info field
    
    return 1; // Success
}

uint8_t AX25_insert_info(AX25UIFrame * self, const char* format, ...) {
    if (self == NULL || format == NULL || self->info == NULL) {
        return 0; // Error - invalid parameters or frame not properly initialized
    }
    
    va_list args;
    va_start(args, format);
    
    // Calculate available space in the raw_buffer
    size_t available_space = AX25_IFRAME_MAX_SIZE - (self->info - self->raw_buffer);
    
    // Use vsnprintf to format the string with variable arguments
    int written = vsnprintf(self->info, available_space, format, args);
    
    va_end(args);
    
    // Check if the formatting was successful
    if (written < 0 || written >= available_space) {
        return 0; // Error - formatting failed or raw_buffer too small
    }
    
    // Update the length (add the length of the formatted string)
    self->len += written;
    
    self->readable = 1; // Frame is now complete and readable
    
    return 1; // Success
}
