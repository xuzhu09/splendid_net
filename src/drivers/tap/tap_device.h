#ifndef TAP_DEVICE_H
#define TAP_DEVICE_H

#include <stdint.h>

#define TAP_OK  0
#define TAP_ERR -1

int tap_device_init(const char *dev_name);
int tap_device_send(const void *data, uint16_t len);
int tap_device_read(void *buffer, uint16_t max_len);
void tap_device_get_mac(uint8_t *mac_buf);

#endif // TAP_DEVICE_H