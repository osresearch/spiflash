/**
 * \file RTTY decoder
 *
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <stdint.h>
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


static void
rtty_pulse_init(void)
{
	// Start TCNT1 to count at clk/1
	TCCR1B = 0
		| (0 << CS12)
		| (0 << CS11)
		| (1 << CS10)
		;

	// Enable ADC and select input ADC0 / F0
	// RTTY is about 1.5 KHz, so we need to sample at least 3 KHz,
	// although 10 KHz would give us plenty of extra.
	// System clock is 16 MHz on teensy, 8 MHz on tiny,
	// conversions take 13 ticks, so divisor == 128 (1,1,1) should
	// give 9.6 KHz of samples.
	ADMUX = 0 | (0 << REFS1) | (1 << REFS0);
	ADCSRA = 0
		| (1 << ADEN)
		| (1 << ADSC)
		| (1 << ADPS2)
		| (1 << ADPS1)
		| (1 << ADPS0)
		;
	DDRF = 0;
	DIDR0 = (1 << 0);
}


/** Blocking read of the incoming pulse.
 * Returns the length in ticks.
 */
static uint16_t
rtty_pulse_read(void)
{
	uint16_t start_crossing = 0;

	while (1)
	{
		// Wait for the conversion to complete
		while ((ADCSRA & (1 << ADSC)))
			continue;

		// Read the value and start the next conversion
		const uint16_t val = ADC;
		ADCSRA |= (1 << ADSC);

		// If this is the first zero crossing of the cycle,
		// record the start time of it.
		if (val < 16)
		{
			if (start_crossing == 0)
				start_crossing = TCNT1;
			continue;
		}

		// If this is the first non-zero reading,
		// after a period of zero, display the length
		// of time that we were at zero
		if (start_crossing == 0)
			continue;

		uint16_t delta = TCNT1 - start_crossing;
		return delta;
	}
}


// Basic command interpreter for controlling port pins
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

	// discard anything that was received prior.  Sometimes the
	// operating system or other software will send a modem
	// "AT command", which can still be buffered.
	usb_serial_flush_input();

	// print a nice welcome message
	send_str(PSTR("\r\nRTTY decoder\r\n"));
	rtty_pulse_init();

	uint8_t bits = 0;
	uint8_t byte = 0;
	uint8_t i = 0;

#define BUFFER_LEN 64
	uint8_t buf[BUFFER_LEN + 2];
	buf[BUFFER_LEN + 0] = '\r';
	buf[BUFFER_LEN + 1] = '\n';

	while (1)
	{
		const uint16_t delta = rtty_pulse_read();

		// At 16 MHz, 3000 Hz == 5333 ticks == 0x14D5
		// We use 0x1600 as an approximate mid point
		if (delta < 0x1600)
			byte = (byte << 1) | 1;
		else
			byte = (byte << 1) | 0;

		if (++bits < 4)
			continue;

		buf[i] = hexdigit(byte);
		byte = bits = 0;

		if (++i < BUFFER_LEN)
			continue;
		i = 0;

		usb_serial_write(buf, sizeof(buf));
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
