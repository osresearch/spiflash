/** \file
 * xmodem file transfer protocol.
 */
#ifndef _xmodem_h_
#define _xmodem_h_

#include <avr/io.h>
#include <stdint.h>


typedef struct
{
	uint8_t soh;
	uint8_t block_num;
	uint8_t block_num_complement;
	uint8_t data[128];
	uint8_t cksum;
} __attribute__((__packed__))
xmodem_block_t;

#define XMODEM_SOH 0x01
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_CAN 0x18
#define XMODEM_C 0x43
#define XMODEM_NAK 0x15
#define XMODEM_EOF 0x1a

#ifdef __cplusplus
extern "C" {
#endif

int
xmodem_init(
	xmodem_block_t * const block,
	int already_received_first_nak
);


int
xmodem_send(
	xmodem_block_t * const block,
	int wait_for_ack
);


int xmodem_fini(
	xmodem_block_t * const block
);

#ifdef __cplusplus
}
#endif

#endif
