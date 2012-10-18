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
 * Bus pirate commands:
 * 0x4B 01001011 -- power, no pullup, aux=1, cs=1
 * 0x67 01100111 -- spi speed == 8 MHz
 * 0x8A 10001010 -- spi config 3.3v, CKP idle low, CKE active to idle, sample middle
 * 0x03 00000011 -- cs high
 * 
 * Manual mode:
Bus Pirate v3b                                                                  
Firmware v5.10 (r559)  Bootloader v4.4                                          
DEVID:0x0447 REVID:0x3043 (24FJ64GA002 B5)                                      
http://dangerousprototypes.com                                                  
CFG1:0xFFDF CFG2:0xFF7F                                                         
*----------*                                                                    
Pinstates:                                                                      
1.(BR)  2.(RD)  3.(OR)  4.(YW)  5.(GN)  6.(BL)  7.(PU)  8.(GR)  9.(WT)  0.(Blk) 
GND     3.3V    5.0V    ADC     VPU     AUX     CLK     MOSI    CS      MISO    
P       P       P       I       I       I       O       O       O       I       
GND     3.22V   4.91V   0.00V   0.00V   L       L       L       H       H       
Power supplies ON, Pull-up resistors OFF, Normal outputs (H=3.3v, L=GND)        
MSB set: MOST sig bit first, Number of bits read/write: 8                       
a/A/@ controls AUX pin                                                          
SPI (spd ckp ske smp csl hiz)=( 4 0 1 0 1 0 )                                   
*----------*                                           
 *
 * {0x95,0,0]
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
#include "xmodem.h"

#define SPI_SS   0xB0 // white
#define SPI_SCLK 0xB1 // green
#define SPI_MOSI 0xB2 // blue
#define SPI_MISO 0xB3 // brown
#define SPI_POW  0xB7 // red

#define CONFIG_SPI_HW

static xmodem_block_t xmodem_block;


static inline void
spi_power(int i)
{
	out(SPI_POW, i);
}

static inline void
spi_cs(int i)
{
	//out(SPI_SS, !i);
	if (i)
		cbi(PORTB, 0);
	else
		sbi(PORTB, 0);
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

static inline uint8_t
spi_send(
	uint8_t c
)
{
	uint8_t bits[80];
	uint8_t i = 0;
#ifdef CONFIG_SPI_HW
	SPDR = c;

	while (bit_is_clear(SPSR, SPIF))
	{
#ifdef CONFIG_SPI_DEBUG
		if (i == sizeof(bits) - 2 - 6)
			continue;
		//int x = in(SPI_MISO);
		uint8_t x = PINB;
		bits[i++] = hexdigit(x);
#endif
	}

	uint8_t val = SPDR;

#ifdef CONFIG_SPI_DEBUG
	bits[i++] = ' ';
	bits[i++] = hexdigit(c >> 4);
	bits[i++] = hexdigit(c >> 0);
	bits[i++] = ' ';
	bits[i++] = hexdigit(val >> 4);
	bits[i++] = hexdigit(val >> 0);
	bits[i++] = '\r';
	bits[i++] = '\n';
	usb_serial_write(bits, i);
#endif
	return val;
#else
	// shift out and into one register
	uint8_t val = c;
	for (int i = 0 ; i < 8 ; i++)
	{
		out(SPI_MOSI, val & 0x80);
		val <<= 1;
		asm("nop");
		asm("nop");
		asm("nop");

		out(SPI_SCLK, 1);

		asm("nop");
		asm("nop");
		asm("nop");

		if (in(SPI_MISO))
			val |= 1;

		out(SPI_SCLK, 0);
	}

	out(SPI_MOSI, 0); // return to zero
#endif

	bits[i++] = hexdigit(c >> 4);
	bits[i++] = hexdigit(c >> 0);
	bits[i++] = '-';
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
	_delay_us(100);

#if 0
	// RES -- read electronic id
	spi_send(0x90);
	spi_send(0x0);
	spi_send(0x0);
	spi_send(0x1);
	uint8_t b1 = spi_send(0xFF);
	uint8_t b2 = spi_send(0xFF);
	uint8_t b3 = 0;
	uint8_t b4 = 0;
#else
	// JEDEC RDID: 1 byte out, three bytes back
	spi_send(0x9F);

	// read 3 bytes back
	uint8_t b1 = spi_send(0x01);
	uint8_t b2 = spi_send(0x02);
	uint8_t b3 = spi_send(0x04);
	uint8_t b4 = spi_send(0x17);
	//uint8_t b4 = 99;
#endif

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

static uint8_t
spi_status(void)
{
	spi_cs(1);
	spi_send(0x05);
	uint8_t r1 = spi_send(0x00);
	spi_cs(0);
	return r1;
}


static uint32_t
usb_serial_readhex(void)
{
	uint32_t val = 0;

	while (1)
	{
		int c = usb_serial_getchar();
		if (c == -1)
			continue;
		if ('0' <= c && c <= '9')
			val = (val << 4) | (c - '0');
		else
		if ('A' <= c && c <= 'F')
			val = (val << 4) | (c - 'A' + 0xA);
		else
		if ('a' <= c && c <= 'f')
			val = (val << 4) | (c - 'a' + 0xA);
		else
			return val;
	}
}


#define SPI_WIP 1
#define SPI_WEL 2

static void
spi_write_enable(void)
{
	spi_power(1);
	_delay_ms(2);

	uint8_t r1 = spi_status();

	spi_cs(1);
	spi_send(0x06);
	spi_cs(0);

	uint8_t r2 = spi_status();

	char buf[16];
	uint8_t off =0;
	buf[off++] = hexdigit(r1 >> 4);
	buf[off++] = hexdigit(r1 >> 0);
	buf[off++] = ' ';
	buf[off++] = hexdigit(r2 >> 4);
	buf[off++] = hexdigit(r2 >> 0);
	if ((r2 & SPI_WEL) == 0)
		buf[off++] = '!';

	buf[off++] = '\r';
	buf[off++] = '\n';
	usb_serial_write(buf, off);
}


static void
spi_upload(void)
{
	uint32_t addr = usb_serial_readhex();

	if ((spi_status() & SPI_WEL) == 0)
	{
		send_str(PSTR("wp!\r\n"));
		return;
	}

	usb_serial_putchar('G');

	uint16_t offset = 0;
	const size_t chunk_size = sizeof(xmodem_block.data);
	uint8_t * const buf = xmodem_block.data;

	for (offset = 0 ; offset < 4096 ; offset += chunk_size)
	{
		// read 128 bytes into the xmodem data block
		for (uint8_t i = 0 ; i < chunk_size; i++)
		{
			int c;
			while ((c = usb_serial_getchar()) == -1)
				;
			buf[i] = c;
		}

		spi_cs(1);
		spi_send(0x06);
		spi_cs(0);

		uint8_t r2 = spi_status();

		spi_cs(1);
		spi_send(0x02);
		spi_send(addr >> 16);
		spi_send(addr >>  8);
		spi_send(addr >>  0);
			
		for (uint8_t i = 0 ; i < chunk_size ; i++)
			spi_send(buf[i]);

		spi_cs(0);

		// wait for write to finish
		while (spi_status() & SPI_WIP)
			;
		usb_serial_putchar('.');
		addr += chunk_size;
	}

	send_str(PSTR("done!\r\n"));
}


static void
spi_erase_sector(void)
{
	uint32_t addr = usb_serial_readhex();

	if ((spi_status() & SPI_WEL) == 0)
	{
		send_str(PSTR("wp!\r\n"));
		return;
	}

	spi_cs(1);
	spi_send(0x20);
	spi_send(addr >> 16);
	spi_send(addr >>  8);
	spi_send(addr >>  0);

	spi_cs(0);

	while (spi_status() & SPI_WIP)
		;

	char buf[16];
	uint8_t off = 0;
	buf[off++] = 'E';
	buf[off++] = hexdigit(addr >> 20);
	buf[off++] = hexdigit(addr >> 16);
	buf[off++] = hexdigit(addr >> 12);
	buf[off++] = hexdigit(addr >>  8);
	buf[off++] = hexdigit(addr >>  4);
	buf[off++] = hexdigit(addr >>  0);
	buf[off++] = '\r';
	buf[off++] = '\n';

	usb_serial_write(buf, off);
}
	


static void
spi_read(void)
{
	uint32_t addr = usb_serial_readhex();

	spi_power(1);
	_delay_ms(2);

	spi_cs(1);
	//_delay_ms(1);

	// read a page
	spi_send(0x03);
	spi_send(addr >> 16);
	spi_send(addr >>  8);
	spi_send(addr >>  0);

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


static void
prom_send(void)
{
	// We have already received the first nak.
	// Fire it up!
	if (xmodem_init(&xmodem_block, 1) < 0)
		return;

	//const uint32_t end_addr = 8L << 20;
	const uint32_t end_addr = 8L << 20;

	spi_power(1);
	_delay_ms(1);

	uint32_t addr = 0;

	while (1)
	{
		spi_cs(1);
		spi_send(0x03); // read
		spi_send(addr >> 16);
		spi_send(addr >>  8);
		spi_send(addr >>  0);

		for (uint8_t off = 0 ; off < sizeof(xmodem_block.data) ; off++)
			xmodem_block.data[off] = spi_send(0);

		spi_cs(0);

		if (xmodem_send(&xmodem_block, 1) < 0)
			return;

		addr += sizeof(xmodem_block.data);
		if (addr >= end_addr)
			break;
	}

	spi_power(0);

	xmodem_fini(&xmodem_block);
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

	// turn on led
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
	//ddr(SPI_POW, 1); // do not enable power pin for now

	// No pull ups enabled
	out(SPI_MISO, 0);

	// just to be sure that MISO is configured correctly
	cbi(PORTB, 3); // no pull up
	cbi(DDRB, 3);

	// keep it off and unselected
	spi_power(0);
	spi_cs(0);

	send_str(PSTR("spi\r\n"));

#ifdef CONFIG_SPI_HW
	// Enable SPI in master mode, clock/16 == 1 MHz
	// Clocked on falling edge (CPOL=0, CPHA=1, PIC terms == CKP=0, CKE=1)
	SPCR = 0
		| (1 << SPE)
		| (1 << MSTR)
		| (0 << SPR1)
		| (0 << SPR0)
		| (0 << CPOL)
		| (0 << CPHA)
		;

	// Wait for any transactions to complete (shouldn't happen)
	if (bit_is_set(SPCR, SPIF))
		(void) SPDR;
#endif

	while (1)
	{
		usb_serial_putchar('>');

		int c;
		while ((c = usb_serial_getchar()) == -1)
			continue;

		switch(c)
		{
		case 'i': spi_rdid(); break;
		case 'r': spi_read(); break;
		case 'w': spi_write_enable(); break;
		case 'e': spi_erase_sector(); break;
		case 'u': spi_upload(); break;
		case XMODEM_NAK:
			prom_send();
			send_str(PSTR("xmodem done\r\n"));
			break;
		case 'x': {
			uint8_t x = DDRB;
			usb_serial_putchar(hexdigit(x >> 4));
			usb_serial_putchar(hexdigit(x >> 0));
			break;
		}
		default:
			usb_serial_putchar('?');
			break;
		}
	}

}
