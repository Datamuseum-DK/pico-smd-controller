#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "base.h"
#include "config.h"
#include "xjob.h"

#define ERROR_MASK                \
	  (1 << GPIO_FAULT)       \
	| (1 << GPIO_SEEK_ERROR)

#define TAG_STROBE_SLEEP() sleep_us(2)
// I haven't seen anything in the docs about how long a "tag pin" should be
// held high before the drive registers the signal. For specific
// operations/pins I'm seeing quotes of 250 ns to 1.0 µs, so 2.0 µs should be
// abundant?

unsigned job_counter;
unsigned job_completion_counter;
unsigned job_error_counter;

static void set_bits(int value)
{
	#define PUT(N) gpio_put(GPIO_BIT ## N, value & (1<<N))
	PUT(0); PUT(1); PUT(2); PUT(3); PUT(4); PUT(5); PUT(5); PUT(6); PUT(7); PUT(8); PUT(9);
	#undef PUT
}

enum tag { TAG_CLEAR, TAG_UNIT_SELECT, TAG1, TAG2, TAG3 };
static inline void set_tag(enum tag tag)
{
	gpio_put(GPIO_UNIT_SELECT_TAG, tag == TAG_UNIT_SELECT);
	gpio_put(GPIO_TAG1,            tag == TAG1);
	gpio_put(GPIO_TAG2,            tag == TAG2);
	gpio_put(GPIO_TAG3,            tag == TAG3);
}

static void tag(enum tag tag, unsigned value)
{
	set_tag(TAG_CLEAR);
	switch (tag) {
	case TAG1:
	case TAG2:
	case TAG3:
		set_bits(value);
		break;
	case TAG_UNIT_SELECT:
		// NOTE: UNIT SELECT BIT 0-3 must be hardwired to zeroes. Here
		// we assert that only unit 0 is selected.
		if (value != 0) PANIC(PANIC_BOUNDS_CHECK_FAILED);
		break;
	default: PANIC(PANIC_XXX);
	}
	set_tag(tag);
	TAG_STROBE_SLEEP();
	set_tag(TAG_CLEAR);
}

enum { // TABLE 3-1
	TAG3BIT_WRITE_GATE             = 1<<0,
	TAG3BIT_READ_GATE              = 1<<1,
	TAG3BIT_SERVO_OFFSET_POSITIVE  = 1<<2, // nudge ~250µin off servo track; towards spindle
	TAG3BIT_SERVO_OFFSET_NEGATIVE  = 1<<3, // nudge ~250µin off servo track; away from spindle
	TAG3BIT_FAULT_CLEAR            = 1<<4, // "100 ns (minimum)"
	TAG3BIT_ADDRESS_MARK_ENABLE    = 1<<5,
	TAG3BIT_RTZ_SEEK               = 1<<6, // Return to Track Zero? "250 ns to 1.0 µs wide"
	TAG3BIT_DATA_STROBE_EARLY      = 1<<7,
	TAG3BIT_DATA_STROBE_LATE       = 1<<8,
	TAG3BIT_RELEASE                = 1<<9,
};

static void rtz_seek(void)
{
	tag(TAG3, TAG3BIT_RTZ_SEEK);
}

static void read_enable(void)
{
	tag(TAG3, TAG3BIT_READ_GATE);
}

static void read_enable_with_servo_offset(int offset)
{
	const unsigned bits = TAG3BIT_READ_GATE
		| (offset > 0 ? TAG3BIT_SERVO_OFFSET_POSITIVE
		:  offset < 0 ? TAG3BIT_SERVO_OFFSET_NEGATIVE
		: 0);
	tag(TAG3, bits);
}

static void select_unit0(void)
{
	tag(TAG_UNIT_SELECT, 0);
}

static void select_cylinder(unsigned cylinder)
{
	tag(TAG1, cylinder & ((1<<10)-1));
}

static void select_head(unsigned head)
{
	tag(TAG2, head & ((1<<3)-1));
}

static void STOP(void) { while (1) {}; }

static void DONE(void)
{
	job_completion_counter++;
	STOP();
}

static xjob RUN(void(*fn)(void))
{
	multicore_reset_core1();
	multicore_launch_core1(fn);
	return job_counter++;
}

static void ERROR(void)
{
	job_error_counter++;
	STOP();
}

void job_select_unit0(void)
{
	select_unit0();
	DONE();
}

void job_cancel(void)
{
	rtz_seek();
	DONE();
}

xjob xjob_select_unit0(void)
{
	return RUN(job_select_unit0);
}

xjob xjob_cancel(void)
{
	return RUN(job_cancel);
}
