#include "mailbox.h"

#include <stdarg.h>

#include "k_libc/k_string.h"

#define MAIL0_READ (((uint32_t *)(MAIL_READ)))
#define MAIL0_STATUS (((uint32_t *)(MAIL_RSTATUS)))
#define MAIL0_WRITE (((uint32_t *)(MAIL_WRITE)))

#define CHANNEL_MASK 0x0000000F  // channels: lower 4 bits
#define DATA_MASK    0xFFFFFFF0  // data: remaining bits

#define MAILBOX_END_TAG 0x00000000

struct __attribute__((__packed__, aligned(4))) MailBoxRegisters {
	const uint32_t Read0;											// 0x00         Read data from VC to ARM
	uint32_t Unused[3];												// 0x04-0x0F
	uint32_t Peek0;													// 0x10
	uint32_t Sender0;												// 0x14
	uint32_t Status0;												// 0x18         Status of VC to ARM
	uint32_t Config0;												// 0x1C        
	uint32_t Write1;												// 0x20         Write data from ARM to VC
	uint32_t Unused2[3];											// 0x24-0x2F
	uint32_t Peek1;													// 0x30
	uint32_t Sender1;												// 0x34
	uint32_t Status1;												// 0x38         Status of ARM to VC
	uint32_t Config1;												// 0x3C 
};

#define MAILBOX ((volatile __attribute__((aligned(4))) struct MailBoxRegisters*)(uintptr_t)(MAIL_BASE))

static uint32_t property_data[8192] __attribute__((aligned(16)));

uint32_t mailbox_read(MAILBOX_CHANNEL channel) {
    uint32_t value;													
	if (channel > MB_CHANNEL_GPU)
        return 0xFEEDDEAD;				
	do {
		do {
			value = MAILBOX->Status0;								
		} while ((value & MAIL_EMPTY) != 0);						
		value = MAILBOX->Read0;										
	} while ((value & CHANNEL_MASK) != channel);			    	
	return (value & DATA_MASK);
}

void mailbox_write(uint32_t message, MAILBOX_CHANNEL channel) {
    uint32_t value;
	do {
		value = MAILBOX->Status1;
	} while ((value & MAIL_FULL) != 0);
	MAILBOX->Write1 = (message & DATA_MASK | (channel & CHANNEL_MASK));
}

bool mailbox_tag_message(uint32_t* response_buf, uint8_t data_count, ...)
{
	uint32_t __attribute__((aligned(16))) message[32];
	va_list list;
	va_start(list, data_count);
	message[0] = (data_count + 3) * 4;
	message[data_count + 2] = MAILBOX_END_TAG;
	message[1] = RPI_FIRMWARE_STATUS_REQUEST;
	for (int i = 0; i < data_count; i++) 
	{
		message[2 + i] = va_arg(list, uint32_t);
	}
	va_end(list);							
	mailbox_write((uint32_t)(void*)message, MB_CHANNEL_TAGS);
	mailbox_read(MB_CHANNEL_TAGS);
	if (message[1] == RPI_FIRMWARE_STATUS_SUCCESS) 
	{
		if (response_buf) 
		{
			for (int i = 0; i < data_count; i++)
			{
				response_buf[i] = message[2 + i];
			}
		}
		return true;
	}
	return false;
}

void mailbox_process(mailbox_tag_t *tag, uint32_t tag_size) {

	// https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
    uint32_t buffer_size = tag_size + 4 /*uint32_t size*/ + 4 /*uint32_t code*/ + 4 /*uint32_t end tag*/;

	property_data[0] = buffer_size;                           // size
	property_data[1] = RPI_FIRMWARE_STATUS_REQUEST;           // code
    k_memcpy(&property_data[2], tag, tag_size);               // tags
	property_data[buffer_size / 4 - 1] = MAILBOX_END_TAG;     // End tag

    mailbox_write((uint32_t)(void *)property_data, MB_CHANNEL_TAGS);
	mailbox_read(MB_CHANNEL_TAGS);
	k_memcpy(tag, &property_data[2], tag_size);
}

void mailbox_generic_cmd_id(uint32_t tag_id, uint32_t id, uint32_t *value)
{
	typedef struct {
		mailbox_tag_t tag;
		uint32_t id;
		uint32_t value;
	} mailbox_generic_t;

	mailbox_generic_t cmd;
	cmd.tag.id = tag_id;
	cmd.tag.value_length = 0x00;
	cmd.tag.buffer_size = sizeof(mailbox_generic_t) - sizeof(mailbox_tag_t);
	cmd.id = id;
	cmd.value = *value;

	mailbox_process((mailbox_tag_t*)&cmd, sizeof(cmd));

	*value = cmd.value;
}

void mailbox_generic_cmd(uint32_t tag_id, uint32_t *value)
{
	typedef struct {
		mailbox_tag_t tag;
		uint32_t value;
	} mailbox_generic_t;

	mailbox_generic_t cmd;
	cmd.tag.id = tag_id;
	cmd.tag.value_length = 0x00;
	cmd.tag.buffer_size = sizeof(mailbox_generic_t) - sizeof(mailbox_tag_t);
	cmd.value = *value;

	mailbox_process((mailbox_tag_t*)&cmd, sizeof(cmd));

	*value = cmd.value;
}

uint32_t mailbox_get_id(uint32_t tag_id, uint32_t id)
{
	uint32_t val = 0;
	mailbox_generic_cmd_id(tag_id, id, &val);
	return val;
}

uint32_t mailbox_get(uint32_t tag_id)
{
	uint32_t val = 0;
	mailbox_generic_cmd(tag_id, &val);
	return val;
}