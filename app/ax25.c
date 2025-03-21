
#include <string.h>
#include <stdint.h>

#include "app/ax25.h"
#include "external/printf/printf.h"

int16_t AX25_find_offset(const char *arr, uint16_t arr_length, uint8_t target, uint16_t start_offset) {
    for (uint16_t i = start_offset; i < arr_length; ++i) {
        if (arr[i] == target) {
            return i; // Return the index (offset) where the byte is found
        }
    }
    return -1; // Return -1 if the target byte isn't found
}

uint8_t AX25_insert_destination(AX25UIFrame * self, const char * callsign, uint8_t ssid) {
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

uint8_t AX25_get_source(AX25UIFrame * self, char * dst) {
    if (self == NULL || dst == NULL || self->len < 14) {
        return 0; // Error - invalid parameters or frame too short
    }
    
    // Source address starts at position 7 (after destination address)
    char *src_addr = &self->raw_buffer[7];
    uint8_t i;
    uint8_t ssid;
    
    // Extract callsign (right-shift by 1 bit)
    for (i = 0; i < 6; i++) {
        dst[i] = (src_addr[i] >> 1) & 0x7F;
    }
    
    // Remove trailing spaces
    while (i > 0 && dst[i-1] == ' ') {
        i--;
    }
    
    // Extract SSID from the 7th byte: 111SSID1 format
    ssid = (src_addr[6] >> 1) & 0x0F;
    
    // Add SSID to string if it's non-zero
    if (ssid > 0) {
        dst[i] = '-';
        
        // Convert SSID to string
        if (ssid < 10) {
            // Single digit SSID
            dst[i+1] = '0' + ssid;
            dst[i+2] = '\0';
        } else {
            // Double digit SSID
            dst[i+1] = '0' + (ssid / 10);
            dst[i+2] = '0' + (ssid % 10);
            dst[i+3] = '\0';
        }
    } else {
        // No SSID, just terminate the string
        dst[i] = '\0';
    }
    
    return 1; // Success
}

uint8_t AX25_insert_source(AX25UIFrame * self, const char * callsign, uint8_t ssid) {
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

uint8_t AX25_insert_paths(AX25UIFrame * self, const char ** path_strings, uint8_t paths) {
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
        const char *path_string = path_strings[path_idx];
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
    uint16_t available_space = AX25_IFRAME_MAX_SIZE - (self->info - self->raw_buffer);
    
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

void AX25_clear(AX25UIFrame* frame) {
    frame->readable = 0;
    frame->len = 0;
    frame->control = NULL;
    frame->pid = NULL;
    frame->info = NULL;
    memset(frame->raw_buffer, 0, AX25_IFRAME_MAX_SIZE);
}