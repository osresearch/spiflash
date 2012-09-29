/**
 * \file Generic serial and other probe for a Teensy
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

	_delay_ms(500);

	// wait for the user to run their terminal emulator program
	// which sets DTR to indicate it is ready to receive.
	while (!(usb_serial_get_control() & USB_SERIAL_DTR))
		continue;

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	// Configure the uart in 38400 mode
	ddr(0xD2, 0); // rx
	out(0xD2, 0); // no pull up

	ddr(0xD3, 1); // tx
	out(0xD3, 1); // set high for now

	// 38400 == 25
	// 57600 == 16
	// 115200 == 8
	UBRR1 = 8;
	sbi(UCSR1B, RXEN1);
	sbi(UCSR1B, TXEN1);

	send_str(PSTR("serial bridge\r\n"));
	while (1)
	{
		int c = usb_serial_getchar();
		if (c != -1)
		{
			while (bit_is_clear(UCSR1A, UDRE1))
				;
			UDR1 = c;
		}

		if (bit_is_set(UCSR1A, RXC1))
		{
			c = UDR1;
			usb_serial_putchar(c);
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
