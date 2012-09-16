/**
 * \file Badass gauge driver
 *
 * Drives mA gauges via PWM.
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
	while (!usb_configured()) /* wait */ ;
	_delay_ms(1000);

	// wait for the user to run their terminal emulator program
	// which sets DTR to indicate it is ready to receive.
	while (!(usb_serial_get_control() & USB_SERIAL_DTR))
		continue;

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

#define GAUGE1 0xC6

	ddr(GAUGE1, 1);
	out(GAUGE1, 0);

	// Configure OC3A in fast-PWM mode, 10-bit
	sbi(TCCR3B, WGM32);
	sbi(TCCR3A, WGM31);
	sbi(TCCR3A, WGM30);

	// Configure output mode to clear on match, set at top
	sbi(TCCR3A, COM3A1);
	cbi(TCCR3A, COM3A0);

	// Configure clock 3 at clk/1
	cbi(TCCR3B, CS32);
	cbi(TCCR3B, CS31);
	sbi(TCCR3B, CS30);

/*
	// or configure output mode to clear on match, set at top
	cbi(TCCR3A, COM3A1);
	sbi(TCCR3A, COM3A0);
*/

	OCR3A = 255;
	uint16_t val = 0;

	send_str(PSTR("badass gauge\r\n"));
	while (1)
	{
		int c = usb_serial_getchar();
		if (c == -1)
			continue;

		if (c == '!')
		{
			OCR3A = 0;
			val = 0;
			continue;
		}
		if (c == '@')
		{
			OCR3A = 1023;
			val = 0;
			continue;
		}

		if ('0' <= c && c <= '9')
		{
			val = (val << 4) | (c - '0' + 0x0);
			continue;
		}

		if ('a' <= c && c <= 'f')
		{
			val = (val << 4) | (c - 'a' + 0xA);
			continue;
		}

		if ('A' <= c && c <= 'F')
		{
			val = (val << 4) | (c - 'A' + 0xA);
			continue;
		}

		if (c == '\n')
			continue;

		if (c == '\r')
		{
			send_str(PSTR("!\r\n"));
			OCR3A = val;
			val = 0;
			continue;
		}

		send_str(PSTR("?\r\n"));
		val = 0;
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
