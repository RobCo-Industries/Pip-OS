#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include <kernel/io.h>
#include <stdint.h>

typedef struct {
    uint8_t channel: 4;
    uint32_t data: 28;
} mail_message_t;

typedef struct {
    uint32_t reserved: 30;
    uint8_t empty: 1;
    uint8_t full:1;
} mail_status_t;

mail_message_t mailbox_read(uint8_t channel);
void mailbox_send(mail_message_t msg, uint8_t channel);

#endif // __MAILBOX_H__