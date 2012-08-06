#ifndef _prom_bits_h_
#define _prom_bits_h_

#include <stdint.h>

#define sbi(PORT, PIN) ((PORT) |=  (1 << (PIN)))
#define cbi(PORT, PIN) ((PORT) &= ~(1 << (PIN)))


extern void
ddr(
	const uint8_t port,
	const uint8_t value
);


extern void
out(
	const uint8_t port,
	uint8_t value
);


extern uint8_t
in(
	uint8_t port
);

#endif
