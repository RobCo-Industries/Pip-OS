#include <kernel/mailbox.h>

#define MAIL0_READ (((mail_message_t *)(MAIL_READ)))
#define MAIL0_STATUS (((mail_status_t *)(MAIL_RSTATUS)))
#define MAIL0_WRITE (((mail_message_t *)(MAIL_WRITE)))

mail_message_t mailbox_read(uint8_t channel) {
    mail_status_t stat;
    mail_message_t res;

    // Make sure that the message is from the right channel
    do {
        // Make sure there is mail to recieve
        do {
            stat = *MAIL0_STATUS;
        } while (stat.empty);

        // Get the message
        res = *MAIL0_READ;
    } while (res.channel != channel);

    return res;
}

void mailbox_send(mail_message_t msg, uint8_t channel) {
    mail_status_t stat;
    msg.channel = channel;

    // Make sure you can send mail
    do {
        stat = *MAIL0_STATUS;
    } while (stat.full);

    // send the message
    *MAIL0_WRITE = msg;
}