/**
 * \file PROM reader.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "usb_serial.h"

#define LED_CONFIG	(DDRD |= (1<<6))
#define LED_ON		(PORTD |= (1<<6))
#define LED_OFF		(PORTD &= ~(1<<6))
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))

void send_str(const char *s);
uint8_t recv_str(char *buf, uint8_t size);
void parse_and_execute_command(const char *buf, uint8_t num);

static uint8_t
hexdigit(
	uint8_t x
)
{
	x &= 0xF;
	if (x < 0xA)
		return x + '0' - 0x0;
	else
		return x + 'A' - 0xA;
}


static uint8_t
printable(
	uint8_t x
)
{
	if ('A' <= x && x <= 'Z')
		return 1;
	if ('a' <= x && x <= 'z')
		return 1;
	if ('0' <= x && x <= '9')
		return 1;
	if (x == ' ')
		return 1;

	return 0;
}
	

/** Colors
 * 0 White
 * 1 Gray
 * 2 Purple
 * 3 Blue
 * 4 Green
 * 5 Yellow
 * 6 Orange
 * 7 Brown
 *
 * 8 White
 * 9 Gray
 *10 Purple
 *11 Blue
 *12 Green
 *13 Yellow
 *14 Orange
 *15 Brown
 */


/** \todo Use port_base to map these into an array and avoid the defines */
#define ADDR_PORT_0	PORTB
#define ADDR_DDR_0	DDRB
#define ADDR_PIN_0	0

#define ADDR_PORT_1	PORTB
#define ADDR_DDR_1	DDRB
#define ADDR_PIN_1	1

#define ADDR_PORT_2	PORTB
#define ADDR_DDR_2	DDRB
#define ADDR_PIN_2	2

#define ADDR_PORT_3	PORTE
#define ADDR_DDR_3	DDRE
#define ADDR_PIN_3	6

#define ADDR_PORT_4	PORTB
#define ADDR_DDR_4	DDRB
#define ADDR_PIN_4	3

#define ADDR_PORT_5	PORTB
#define ADDR_DDR_5	DDRB
#define ADDR_PIN_5	7

#define ADDR_PORT_6	PORTD
#define ADDR_DDR_6	DDRD
#define ADDR_PIN_6	0

#define ADDR_PORT_7	PORTD
#define ADDR_DDR_7	DDRD
#define ADDR_PIN_7	1

#define ADDR_PORT_8	PORTD
#define ADDR_DDR_8	DDRD
#define ADDR_PIN_8	2

#define ADDR_PORT_9	PORTD
#define ADDR_DDR_9	DDRD
#define ADDR_PIN_9	3

#define ADDR_PORT_10	PORTC
#define ADDR_DDR_10	DDRC
#define ADDR_PIN_10	6

#define ADDR_PORT_11	PORTC
#define ADDR_DDR_11	DDRC
#define ADDR_PIN_11	7

#define ADDR_PORT_12	PORTD
#define ADDR_DDR_12	PORTD
#define ADDR_PIN_12	5

#define ADDR_PORT_13	PORTD
#define ADDR_DDR_13	PORTD
#define ADDR_PIN_13	4

#define ADDR_PORT_14	PORTD
#define ADDR_DDR_14	PORTD
#define ADDR_PIN_14	6

#define ADDR_PORT_15	PORTD
#define ADDR_DDR_15	PORTD
#define ADDR_PIN_15	7


#define DATA_PORT_0	PINB
#define DATA_DDR_0	DDRB
#define DATA_PIN_0	4

#define DATA_PORT_1	PINB
#define DATA_DDR_1	DDRB
#define DATA_PIN_1	5

#define DATA_PORT_2	PINB
#define DATA_DDR_2	DDRB
#define DATA_PIN_2	6

#define DATA_PORT_3	PINF
#define DATA_DDR_3	DDRF
#define DATA_PIN_3	7

#define DATA_PORT_4	PINF
#define DATA_DDR_4	DDRF
#define DATA_PIN_4	6

#define DATA_PORT_5	PINF
#define DATA_DDR_5	DDRF
#define DATA_PIN_5	5

#define DATA_PORT_6	PINF
#define DATA_DDR_6	DDRF
#define DATA_PIN_6	4

#define DATA_PORT_7	PINF
#define DATA_DDR_7	DDRF
#define DATA_PIN_7	1


#define sbi(PORT, PIN) (PORT) |= (1 << (PIN))
#define cbi(PORT, PIN) (PORT) &= ~(1 << (PIN))

static inline void
set_address(
	uint16_t addr
)
{
#define PORT(X) PORT##X
#define DDR(X) DDR##X
#define PIN(X) PIN##X
#define CAT(X,Y) X##Y


#define ADDR_BIT(ID, bit) do { \
	if (bit) \
		sbi(CAT(ADDR_PORT_, ID), CAT(ADDR_PIN_, ID)); \
	else \
		cbi(CAT(ADDR_PORT_, ID), CAT(ADDR_PIN_, ID)); \
} while(0)

	ADDR_BIT(0, addr & 1); addr >>= 1;
	ADDR_BIT(1, addr & 1); addr >>= 1;
	ADDR_BIT(2, addr & 1); addr >>= 1;
	ADDR_BIT(3, addr & 1); addr >>= 1;
	ADDR_BIT(4, addr & 1); addr >>= 1;
	ADDR_BIT(5, addr & 1); addr >>= 1;
	ADDR_BIT(6, addr & 1); addr >>= 1;
	ADDR_BIT(7, addr & 1); addr >>= 1;
	ADDR_BIT(8, addr & 1); addr >>= 1;
	ADDR_BIT(9, addr & 1); addr >>= 1;
	ADDR_BIT(10, addr & 1); addr >>= 1;
	ADDR_BIT(11, addr & 1); addr >>= 1;
	ADDR_BIT(12, addr & 1); addr >>= 1;
	ADDR_BIT(13, addr & 1); addr >>= 1;
	//ADDR_BIT(14, addr & 1); addr >>= 1;
	//ADDR_BIT(15, addr & 1); addr >>= 1;
	ADDR_BIT(14, 1); addr >>= 1;
	ADDR_BIT(15, 1); addr >>= 1;
}


static uint8_t
read_byte(
	uint16_t addr
)
{
	set_address(addr);
	for(uint8_t i = 0 ; i < 255; i++)
	{
		asm("nop");
	}

#define DATA_BIT(ID) \
	(CAT(DATA_PORT_, ID) & (1 << CAT(DATA_PIN_, ID)) ? 1 : 0)

	uint8_t b = 0;
	b |= DATA_BIT(7); b <<= 1;
	b |= DATA_BIT(6); b <<= 1;
	b |= DATA_BIT(5); b <<= 1;
	b |= DATA_BIT(4); b <<= 1;
	b |= DATA_BIT(3); b <<= 1;
	b |= DATA_BIT(2); b <<= 1;
	b |= DATA_BIT(1); b <<= 1;
	b |= DATA_BIT(0);

	return b;
}


int main(void)
{
	// set for 16 MHz clock, and turn on the LED
	CPU_PRESCALE(0);
	LED_CONFIG;
	LED_ON;

	// initialize the USB, and then wait for the host
	// to set configuration.  If the Teensy is powered
	// without a PC connected to the USB port, this 
	// will wait forever.
	usb_init();
	while (!usb_configured()) /* wait */ ;
	_delay_ms(1000);

	// wait for the user to run their terminal emulator program
	// which sets DTR to indicate it is ready to receive.
	while (!(usb_serial_get_control() & USB_SERIAL_DTR))
		continue;

	// Configure all of the address pins as outputs

#define SET_DDR(ID) \
	sbi(CAT(ADDR_DDR_,ID), CAT(ADDR_PIN_,ID))
#define CLR_DDR(ID) \
	cbi(CAT(DATA_DDR_,ID), CAT(DATA_PIN_,ID))

	SET_DDR(0);
	SET_DDR(1);
	SET_DDR(2);
	SET_DDR(3);
	SET_DDR(4);
	SET_DDR(5);
	SET_DDR(6);
	SET_DDR(7);
	SET_DDR(8);
	SET_DDR(9);
	SET_DDR(10);
	SET_DDR(11);
	SET_DDR(12);
	SET_DDR(13);
	SET_DDR(14);
	SET_DDR(15);

	CLR_DDR(0);
	CLR_DDR(1);
	CLR_DDR(2);
	CLR_DDR(3);
	CLR_DDR(4);
	CLR_DDR(5);
	CLR_DDR(6);
	CLR_DDR(7);

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	send_str(PSTR("Press enter to dump\r\n"));

#if 0

	uint16_t addr = 0;
	char line[64];
	uint8_t off = 0;

	while (1)
	{
		addr++;
		if (addr == 0)
			send_str(PSTR("wrap\r\n"));

		uint8_t byte = read_byte(addr);
		if (byte == 0)
			continue;

		if (off == 0)
		{
			line[off++] = hexdigit(addr >> 12);
			line[off++] = hexdigit(addr >>  8);
			line[off++] = hexdigit(addr >>  4);
			line[off++] = hexdigit(addr >>  0);
			line[off++] = '=';
		}

		if (printable(byte))
		{
			line[off++] = byte;
			if (off < sizeof(line) - 2)
				continue;
		} else {
			line[off++] = hexdigit(byte >> 4);
			line[off++] = hexdigit(byte >> 0);
		}

		line[off++] = '\r';
		line[off++] = '\n';
		usb_serial_write(line, off);
		off = 0;
	}
#else
	uint16_t addr = 0;
	while (1)
	{
		if (addr == 0)
		{
			// wait for input
			while (!usb_serial_available())
			{
				LED_OFF;
				_delay_ms(50);
				LED_ON;
				_delay_ms(50);
			}

			while (usb_serial_available())
				usb_serial_getchar();
		}
	
		uint8_t byte = read_byte(addr++);
		usb_serial_putchar(byte);
	}
#endif
}


// Send a string to the USB serial port.  The string must be in
// flash memory, using PSTR
//
void send_str(const char *s)
{
	char c;
	while (1) {
		c = pgm_read_byte(s++);
		if (!c) break;
		usb_serial_putchar(c);
	}
}
