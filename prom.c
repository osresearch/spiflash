/**
 * \file PROM dancer
 *
 * Read up to 40 pin DIP PROMs using a Teensy++ 2.0
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "usb_serial.h"
#include "bits.h"

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
	

static const uint8_t ports[] = {
	[ 1]	= 0xB6,
	[ 2]	= 0xB5,
	[ 3]	= 0xB4,
	[ 4]	= 0xB3,
	[ 5]	= 0xB2,
	[ 6]	= 0xB1,
	[ 7]	= 0xB0,
	[ 8]	= 0xE7,
	[ 9]	= 0xE6,
	[10]	= 0xA2,
	[11]	= 0xA1,
	[12]	= 0xF0,
	[13]	= 0xF1,
	[14]	= 0xF2,
	[15]	= 0xF3,
	[16]	= 0xF4,
	[17]	= 0xF5,
	[18]	= 0xF6,
	[19]	= 0xF7,
	[20]	= 0xA3,

	[21]	= 0xA7,
	[22]	= 0xC7,
	[23]	= 0xC6,
	[24]	= 0xC5,
	[25]	= 0xC4,
	[26]	= 0xC3,
	[27]	= 0xC2,
	[28]	= 0xC1,
	[29]	= 0xC0,
	[30]	= 0xE1,
	[31]	= 0xE0,
	[32]	= 0xD7,
	[33]	= 0xD6,
	[34]	= 0xD5,
	[35]	= 0xD4,
	[36]	= 0xD3,
	[37]	= 0xD2,
	[38]	= 0xD1,
	[39]	= 0xD0,
	[40]	= 0xB7,
};


/** M27C512
 * 28 total pins
 * 16 pins of address,
 * 8 pins of data,
 * some hi, some low
 */
static const uint8_t chip_pins = 28;

static const uint8_t addr_pins[] = 
{
	10, 9, 8, 7, 6, 5, 4, 3, 25, 24, 21, 23, 2, 26, 27, 1
};

static const uint8_t data_pins[] =
{
	11, 12, 13, 15, 16, 17, 18, 19,
};

static const uint8_t hi_pins[] =
{
	28,
};

static const uint8_t low_pins[] =
{
	22, 20, 14,
};



/** Manipulate pins, based on the total number of pins */
#define array_count(X) (sizeof(X) / sizeof(*X))

static inline uint8_t
chip_pin(
	const uint8_t pin
)
{
	if (pin <= chip_pins / 2)
		return ports[pin];
	else
		return ports[pin + 40 - chip_pins];
}
		

static void
set_address(
	uint16_t addr
)
{
	for (uint8_t i = 0 ; i < array_count(addr_pins) ; i++)
	{
		out(chip_pin(addr_pins[i]), addr & 1);
		addr >>= 1;
	}
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
		asm("nop");
		asm("nop");
		asm("nop");
	}

	uint8_t b = 0;
	for (uint8_t i = 0 ; i < 8 ; i++)
	{
		uint8_t bit = in(chip_pin(data_pins[i])) ? 0x80 : 0;
		b = (b >> 1) | bit;
	}

	return b;
}


int main(void)
{
	// set for 16 MHz clock
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
	CPU_PRESCALE(0);

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
	for (uint8_t i = 0 ; i < array_count(addr_pins) ; i++)
		ddr(chip_pin(addr_pins[i]), 1);

	// Configure all of the data pins as inputs
	for (uint8_t i = 0 ; i < array_count(data_pins) ; i++)
		ddr(chip_pin(data_pins[i]), 0);

	// Configure all of the hi and low pins as outputs
	for (uint8_t i = 0 ; i < array_count(hi_pins) ; i++)
	{
		out(chip_pin(hi_pins[i]), 1);
		ddr(chip_pin(hi_pins[i]), 1);
	}

	for (uint8_t i = 0 ; i < array_count(low_pins) ; i++)
	{
		out(chip_pin(low_pins[i]), 0);
		ddr(chip_pin(low_pins[i]), 1);
	}

	//DDRB |= (1 << 7);
	//PORTB |= (1 << 7);

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	send_str(PSTR("Press enter to dump\r\n"));

#if 1

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
				_delay_ms(50);
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
