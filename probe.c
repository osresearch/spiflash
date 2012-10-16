/**
 * \file SPI Flash reader.
 *
 * Very fast reader for SPI flashes.
 *
 * Black = ground
 * Red = 3.3 V
 * Green = clock
 * White = CS#
 * Blue = MOSI (SI on chip)
 * Brown = MISO (SO on chip)
 *
 *   White   CS   --- 1    8 --- VCC     Red
 *   Brown   SO   --- 2    7 --- HOLD#
 *           WP   --- 3    6 --- SCLK    Green
 *   Black   GND  --- 4    5 --- SI      Blue
 *
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>
#include <util/delay.h>
#include "usb_serial.h"
#include "bits.h"

#define SPI_SS   0xB0 // white
#define SPI_SCLK 0xB1 // green
#define SPI_MOSI 0xB2 // blue
#define SPI_MISO 0xB3 // brown
#define SPI_POW  0xB7 // red

static inline void
spi_power(int i)
{
	out(SPI_POW, i);
}

static inline void
spi_cs(int i)
{
	out(SPI_SS, !i);
}


static char
hexdigit(
	uint8_t x
)
{
	x &= 0xF;
	if (x < 0xA)
		return x + '0';
	else
		return x + 'A' - 0xA;
}


static inline uint8_t
spi_send(
	uint8_t c
)
{
	SPDR = c;
	uint8_t bits[80];
	uint8_t i = 0;

	while (bit_is_clear(SPSR, SPIF))
	{
		if (i == sizeof(bits) - 2 - 6)
			continue;
		//int x = in(SPI_MISO);
		bits[i++] = hexdigit(PINB);
	}

	uint8_t val = SPDR;

	bits[i++] = ' ';
	bits[i++] = hexdigit(c >> 4);
	bits[i++] = hexdigit(c >> 0);
	bits[i++] = ' ';
	bits[i++] = hexdigit(val >> 4);
	bits[i++] = hexdigit(val >> 0);
	bits[i++] = '\r';
	bits[i++] = '\n';
	usb_serial_write(bits, i);
	return val;
}


static void
spi_passthrough(void)
{
	int c;
	while ((c = usb_serial_getchar()) == -1)
		;

	SPDR = c;
	while (bit_is_clear(SPSR, SPIF))
		;
	uint8_t val = SPDR;

	char buf[2];
	buf[0] = hexdigit(val >> 4);
	buf[1] = hexdigit(val >> 0);
	usb_serial_write(buf, 2);
}


/** Read electronic manufacturer and device id */
static void
spi_rdid(void)
{
	spi_power(1);
	//_delay_ms(2);

	spi_cs(1);
	//_delay_ms(1);

	// JEDEC RDID: 1 byte out, three bytes back
	spi_send(0x9F);

	// read 3 bytes back
	uint8_t b1 = spi_send(0x00);
	uint8_t b2 = spi_send(0x00);
	uint8_t b3 = spi_send(0x00);
	//uint8_t b4 = spi_send(0x00);
	uint8_t b4 = 99;

	spi_cs(0);
	_delay_ms(1);
	spi_power(0);

	char buf[16];
	uint8_t off = 0;
	buf[off++] = hexdigit(b1 >> 4);
	buf[off++] = hexdigit(b1 >> 0);
	buf[off++] = hexdigit(b2 >> 4);
	buf[off++] = hexdigit(b2 >> 0);
	buf[off++] = hexdigit(b3 >> 4);
	buf[off++] = hexdigit(b3 >> 0);
	buf[off++] = hexdigit(b4 >> 4);
	buf[off++] = hexdigit(b4 >> 0);
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}

static void
spi_read(uint16_t page)
{
	spi_power(1);
	_delay_ms(2);

	spi_cs(1);
	//_delay_ms(1);

	// read a page
	spi_send(0x03);
	spi_send(page >> 8);
	spi_send(page >> 0);
	spi_send(0);

	uint8_t data[16];

	for (int i = 0 ; i < 16 ; i++)
		data[i] = spi_send(0);

	spi_cs(0);
	spi_power(0);

	char buf[16*3+2];
	uint8_t off = 0;
	for (int i = 0 ; i < 16 ; i++)
	{
		buf[off++] = hexdigit(data[i] >> 4);
		buf[off++] = hexdigit(data[i] >> 0);
		buf[off++] = ' ';
	}
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}

int main(void)
{
	// set for 16 MHz clock
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
	CPU_PRESCALE(0);

	// Disable the ADC
	ADMUX = 0;

	// initialize the USB, and then wait for the host
	// to set configuration.  If the Teensy is powered
	// without a PC connected to the USB port, this 
	// will wait forever.
	usb_init();
	while (!usb_configured())
		continue;

	ddr(0xD6, 1);
	out(0xD6, 1);

	_delay_ms(500);

	// wait for the user to run their terminal emulator program
	// which sets DTR to indicate it is ready to receive.
	while (!(usb_serial_get_control() & USB_SERIAL_DTR))
		continue;

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	// Make sure that everything is tri-stated
	ddr(SPI_MISO, 0);
	ddr(SPI_MOSI, 1);
	ddr(SPI_SCLK, 1);
	ddr(SPI_SS, 1);
	ddr(SPI_POW, 1);

	// No pull ups enabled
	out(SPI_MISO, 0);

	// keep it off and unselected
	spi_power(0);
	spi_cs(0);

	// Enable SPI in master mode, clock/128
	// Clocked on falling edge (CPHA=1, PIC terms == CKP=0, CKE=1)
	SPCR = 0
		| (1 << SPE)
		| (1 << MSTR)
		| (1 << SPR1)
		| (1 << SPR0)
		| (0 << CPOL)
		| (1 << CPHA)
		;

	// Wait for any transactions to complete (shouldn't happen)
	if (bit_is_set(SPCR, SPIF))
		(void) SPDR;

	while (1)
	{
		int c = usb_serial_getchar();
		if (c == -1)
			continue;
		switch(c)
		{
		case 'i': spi_rdid(); break;
		case 'r': spi_read(1); break;
		default:
			usb_serial_putchar('?');
			break;
		}
	}

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
