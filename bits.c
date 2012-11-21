/** \file Access to AVR pins via constants.
 *
 * ddr(0xA3, 1) == enable DDRA |= (1 << 3)
 * out(0xA3, 1) == PORTA |= (1 << 3)
 * in(0xA3) == PINA & (1 << 3)
 */
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "bits.h"

void
__bits_ddr(
	const uint8_t id,
	const uint8_t value
)
{
	__inline_ddr(id, value);
}


void
__bits_out(
	const uint8_t id,
	const uint8_t value
)
{
	__inline_out(id, value);
}


uint8_t
__bits_in(
	const uint8_t id
)
{
	return __inline_in(id);
}
