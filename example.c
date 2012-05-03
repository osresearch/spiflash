/**
 * \file RTTY decoder
 *
 * RTTY rest details:
 *	http://www.aa5au.com/gettingstarted/rtty_diddles_technical.htm
 * 45.45 baud -- each bit is 22 ms
 * Tones are 5500 clock ticks == 2900 Hz (or 1450?)
 * and 4850 ticks == 3300 Hz (or 1650?).
 *
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
		| (0 << ADPS2)
		| (1 << ADPS1)
		| (1 << ADPS0)
		;
	DDRF = 0;
	DIDR0 = (1 << 0);
}


static inline void
adc_start(void)
{
	ADCSRA |= (1 << ADSC);
}


static uint16_t
adc_read_block(void)
{
	// Wait for the conversion to complete
	while ((ADCSRA & (1 << ADSC)))
		continue;

	// Read the value
	return ADC;
}


/** Blocking read of the incoming pulse.
 * Returns the length in ticks.
 */
static uint16_t
rtty_pulse_read(void)
{
	uint16_t start_crossing = 0;
	uint8_t zero_count = 0; // require at least 8 zeros before triggering

	while (1)
	{
		const uint16_t val = adc_read_block();
		adc_start();

		// If this is the first zero crossing of the cycle,
		// record the start time of it.
		if (val < 8)
		{
#if 1
			if (zero_count++ < 4)
				continue;
#endif
			if (start_crossing == 0)
				start_crossing = TCNT1;
			continue;
		}

		// Non-zero; reset our counter
		zero_count = 0;

		// If this is the first non-zero reading,
		// after a period of zero, display the length
		// of time that we were at zero
		if (start_crossing == 0)
			continue;

		uint16_t delta = TCNT1 - start_crossing;
		return delta;
	}
}

static void
a16(
	char * h,
	uint16_t v
)
{
	h[0] = hexdigit(v >> 12);
	h[1] = hexdigit(v >>  8);
	h[2] = hexdigit(v >>  4);
	h[3] = hexdigit(v >>  0);
}


static void
adc_loop(void)
{
	uint8_t i = 0;
	uint8_t buf[64];
	adc_start();

	while (1)
	{
		uint16_t val = adc_read_block();
		adc_start();
		buf[i++] = val >> 2;

		if (i < sizeof(buf))
			continue;

		usb_serial_write(buf, sizeof(buf));
		i = 0;
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
	adc_start();

	uint8_t bits = 0;
	uint8_t byte = 0;
	uint8_t i = 0;

	if (0)
		adc_loop();

#define BUFFER_LEN 64
	char buf[BUFFER_LEN + 2];
	memset(buf, ' ', sizeof(buf));
	buf[BUFFER_LEN + 0] = '\r';
	buf[BUFFER_LEN + 1] = '\n';

	uint16_t deltas[8];
	uint8_t old_state = 0;

	while (1)
	{
		// Read the pulse width of the input
		const uint16_t delta = rtty_pulse_read();

		// If we have had more than two at the same state,
		// assume that we are in the new state
		const uint8_t state = delta > 5200;
		if (state != old_state)
		{
			old_state = state;
			continue;
		}

		// Store 8 bits at a time
		byte = (byte << 1) | state;
		if (++bits < 4)
			continue;

		buf[i] = hexdigit(byte);
		byte = bits = 0;

		if (++i < BUFFER_LEN)
			continue;
#if 0
		if (++i < 8)
			continue;

		a16(&buf[0*5], deltas[0]);
		a16(&buf[1*5], deltas[1]);
		a16(&buf[2*5], deltas[2]);
		a16(&buf[3*5], deltas[3]);
		a16(&buf[4*5], deltas[4]);
		a16(&buf[5*5], deltas[5]);
		a16(&buf[6*5], deltas[6]);
		a16(&buf[7*5], deltas[7]);
#endif

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
