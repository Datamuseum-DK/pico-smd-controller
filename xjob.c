#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "base.h"
#include "pin_config.h"
#include "xjob.h"

#define ERROR_MASK                \
	( (1 << GPIO_FAULT)       \
	| (1 << GPIO_SEEK_ERROR)  \
	)

#define TAG_STROBE_SLEEP() sleep_us(2)
// I haven't seen anything in the docs about how long a "tag pin" should be
// held high before the drive registers the signal. For specific
// operations/pins I'm seeing quotes of 250 ns to 1.0 µs, so 2.0 µs should be
// abundant?

unsigned last_job_id;
unsigned last_completed_job_id;
unsigned last_failed_job_id;
enum xjob_status last_job_status;

static void set_bits(unsigned value)
{
	#define PUT(N) gpio_put(GPIO_BIT ## N, value & (1<<N))
	PUT(0); PUT(1); PUT(2); PUT(3); PUT(4);
	PUT(5); PUT(6); PUT(7); PUT(8); PUT(9);
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
	case TAG1: case TAG2: case TAG3:
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
	set_bits(0);
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

__attribute__ ((noreturn))
static void HALT(void)
{
	while (1) {}
}

__attribute__ ((noreturn))
static void OK(void)
{
	last_completed_job_id = last_job_id;
	HALT();
}

__attribute__ ((noreturn))
static void ERROR(enum xjob_status error_code)
{
	last_failed_job_id = last_job_id;
	last_job_status = error_code;
	HALT();
}

static void check_drive_error(void)
{
	if ((gpio_get_all() & ERROR_MASK) != 0) {
		ERROR(XST_ERR_DRIVE_ERROR);
	}
}

static void pin_mask_wait(unsigned mask, unsigned value, unsigned timeout_us)
{
	const absolute_time_t t0 = get_absolute_time();
	while (1) {
		if ((gpio_get_all() & mask) == value) break;
		check_drive_error();
		if ((get_absolute_time() - t0) > timeout_us) {
			ERROR(XST_ERR_TIMEOUT);
		}
		sleep_us(1);
	}
}

static void pin_wait(unsigned gpio, unsigned value, unsigned timeout_us)
{
	pin_mask_wait((1<<gpio), value ? (1<<gpio) : 0, timeout_us);
}

static void pin1_wait(unsigned gpio, unsigned timeout_us)
{
	pin_wait(gpio, 1, timeout_us);
}

static void pin0_wait(unsigned gpio, unsigned timeout_us)
{
	pin_wait(gpio, 0, timeout_us);
}

static void control_clear(void)
{
	tag(TAG3, 0);
}

static void rtz_seek(void)
{
	tag(TAG3, TAG3BIT_RTZ_SEEK);
	sleep_us(1000);
	control_clear();
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
	pin1_wait(GPIO_UNIT_SELECTED, 100000);
}

static void tag_select_cylinder(unsigned cylinder)
{
	tag(TAG1, cylinder & ((1<<10)-1));
}

static void select_cylinder(unsigned cylinder)
{
	tag_select_cylinder(cylinder);
	pin0_wait(GPIO_ON_CYLINDER, 50000);
	// NOTE: the drive should signal SEEK_ERROR (which IS caught by
	// pin_mask_wait()) if the seek does not complete within 500ms
	const unsigned bits = (1<<GPIO_ON_CYLINDER) | (1<<GPIO_SEEK_END);
	// NOTE: drive doc says that "Seek End is a combination of ON CYL or
	// SEEK ERROR" suggesting it's a simple OR-gate of those signals. But
	// it's a good sanity check nevertheless (cable/drive may be broken).
	pin_mask_wait(bits, bits, 1000000);
}

static void tag_select_head(unsigned head)
{
	tag(TAG2, head & ((1<<3)-1));
}

static void select_head(unsigned head)
{
	tag_select_head(head);
	// a Christian Rovsing manual ("CR8044M DISK CONTROLLER PRODUCT
	// SPECIFICATION", section 3.1.1.3) says they have 32/33 sectors per
	// track, but the last one is a dummy sector and is only "about a
	// thirteenth of each of the other sectors and is used as a space for
	// head change.". So sleep for a thirteenth of a sector:
	sleep_us(((1000000 / (3600/60)) / 32) / 13); // ~40µs
}

static inline void reset(void)
{
	multicore_reset_core1(); // waits until core1 is down
}

static xjob run(void(*fn)(void))
{
	unsigned job_id = ++last_job_id;
	multicore_launch_core1(fn);
	return job_id;
}

union {
	struct {
		unsigned cylinder;
	} select_cylinder;
	struct {
		unsigned head;
	} select_head;
} job_args;


////////////////////////////////////
// select unit 0 ///////////////////
void job_select_unit0(void)
{
	select_unit0();
	OK();
}
xjob xjob_select_unit0(void)
{
	reset();
	return run(job_select_unit0);
}


/////////////////////////////////////////////////////////////////////////////
// select cylinder //////////////////////////////////////////////////////////
void job_select_cylinder(void)
{
	select_cylinder(job_args.select_cylinder.cylinder);
	OK();
}
xjob xjob_select_cylinder(unsigned cylinder)
{
	reset();
	job_args.select_cylinder.cylinder = cylinder;
	return run(job_select_cylinder);
}


/////////////////////////////////////////////////////////////////////////////
// select head //////////////////////////////////////////////////////////////
void job_select_head(void)
{
	select_head(job_args.select_head.head);
	OK();
}
xjob xjob_select_head(unsigned head)
{
	reset();
	job_args.select_head.head = head;
	return run(job_select_head);
}


/////////////////////////////////////////////////////////////////////////////
// rtz / return to track zero ///////////////////////////////////////////////
void job_rtz(void)
{
	rtz_seek();
	OK();
}
xjob xjob_rtz(void)
{
	reset();
	return run(job_rtz);
}


/////////////////////////////////////////////////////////////////////////////
// cancel ///////////////////////////////////////////////////////////////////
void job_cancel(void)
{
	rtz_seek();
	OK();
}
xjob xjob_cancel(void)
{
	reset();
	return run(job_cancel);
}
