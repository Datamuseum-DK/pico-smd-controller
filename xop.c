#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "base.h"
#include "pin_config.h"
#include "drive.h"
#include "controller_protocol.h"
#include "xop.h"
#include "clocked_read.h"

#define ERROR_MASK                \
	( (1 << GPIO_FAULT)       \
	| (1 << GPIO_SEEK_ERROR)  \
	)

#define READY_MASK                   \
	( (1 << GPIO_UNIT_READY)     \
	| (1 << GPIO_UNIT_SELECTED)  \
	)

#define TAG_STROBE_SLEEP() sleep_us(2)
// I haven't seen anything in the docs about how long a "tag pin" should be
// held high before the drive registers the signal. For specific
// operations/pins I'm seeing quotes of 250 ns to 1.0 µs, so 2.0 µs should be
// abundant?

volatile enum xop_status status;

static void set_bits(unsigned value)
{
	#define PUT(N) gpio_put(GPIO_BIT ## N, value & (1<<N))
	PUT(0); PUT(1); PUT(2); PUT(3); PUT(4);
	PUT(5); PUT(6); PUT(7); PUT(8); PUT(9);
	#undef PUT
}

static inline void set_tag(enum tag tag)
{
	gpio_put(GPIO_UNIT_SELECT_TAG, tag == TAG_UNIT_SELECT);
	gpio_put(GPIO_TAG1,            tag == TAG1);
	gpio_put(GPIO_TAG2,            tag == TAG2);
	gpio_put(GPIO_TAG3,            tag == TAG3);
}

static void tag_raw(enum tag tag, unsigned value)
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

__attribute__ ((noreturn))
static void HALT(void)
{
	while (1) {}
}

__attribute__ ((noreturn))
static void DONE(void)
{
	status = XST_DONE;
	HALT();
}

__attribute__ ((noreturn))
static void ERROR(enum xop_status error_code)
{
	status = error_code;
	HALT();
}

static void check_drive_error(void)
{
	unsigned pins = gpio_get_all();
	if ((pins & ERROR_MASK) != 0)          ERROR(XST_ERR_DRIVE_ERROR);
	if ((pins & READY_MASK) != READY_MASK) ERROR(XST_ERR_DRIVE_NOT_READY);
}

static void pin_mask_wait(unsigned mask, unsigned value, unsigned timeout_us)
{
	const absolute_time_t t0 = get_absolute_time();
	while (1) {
		check_drive_error();
		if ((gpio_get_all() & mask) == value) break;
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
	tag_raw(TAG3, 0);
}

static void select_unit0(void)
{
	tag_raw(TAG_UNIT_SELECT, 0);
	pin1_wait(GPIO_UNIT_SELECTED, 100000);
	check_drive_error();
}

static void select_unit0_if_not_selected(void)
{
	if (!gpio_get(GPIO_UNIT_SELECTED)) {
		select_unit0();
	}
}

static void rtz_seek(void)
{
	check_drive_error();
	tag_raw(TAG3, TAG3BIT_RTZ_SEEK);
	sleep_us(1000);
	control_clear();
}

static void read_enable_ex(int servo_offset, int data_strobe_delay)
{
	check_drive_error();
	const unsigned bits = TAG3BIT_READ_GATE

		| (servo_offset > 0 ? TAG3BIT_SERVO_OFFSET_POSITIVE
		:  servo_offset < 0 ? TAG3BIT_SERVO_OFFSET_NEGATIVE
		: 0)

		| (data_strobe_delay > 0 ? TAG3BIT_DATA_STROBE_LATE
		:  data_strobe_delay < 0 ? TAG3BIT_DATA_STROBE_EARLY
		: 0);

	tag_raw(TAG3, bits);
}

static void tag_select_cylinder(unsigned cylinder)
{
	check_drive_error();
	tag_raw(TAG1, cylinder & ((1<<10)-1));
}

static void select_cylinder(unsigned cylinder)
{
	check_drive_error();
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
	check_drive_error();
	tag_raw(TAG2, head & ((1<<3)-1));
}

static void select_head(unsigned head)
{
	check_drive_error();
	tag_select_head(head);
	// a Christian Rovsing manual ("CR8044M DISK CONTROLLER PRODUCT
	// SPECIFICATION", section 3.1.1.3) says they have 32/33 sectors per
	// track, but the last one is a dummy sector and is only "about a
	// thirteenth of each of the other sectors and is used as a space for
	// head change.". So sleep for a thirteenth of a sector:
	sleep_us(((1000000 / DRIVE_RPS) / 32) / 13); // ~40µs
}

static inline void read_data(unsigned buffer_index, unsigned n_32bit_words, unsigned index_sync, unsigned skip_checks)
{
	if (!skip_checks) check_drive_error();
	if (index_sync) {
		pin0_wait(GPIO_INDEX, (1000000 / DRIVE_RPS) / 10);
		pin1_wait(GPIO_INDEX, (1000000 / (DRIVE_RPS/3))); // wait at most 3 revolutions
	}
	clocked_read_into_buffer(buffer_index, n_32bit_words);
	while (1) {
		if (!clocked_read_is_running()) break;
		if (!skip_checks) check_drive_error();
		sleep_us(1);
	}
	wrote_buffer(buffer_index);
}

#if 0
static void read_data_normal(unsigned buffer_index, unsigned n_32bit_words)
{
	read_data(buffer_index, n_32bit_words, /*index_sync=*/1, /*skip_checks=*/0);
}
#endif

static inline void reset(void)
{
	multicore_reset_core1(); // waits until core1 is down
}

static void run(void(*fn)(void))
{
	status = XST_RUNNING;
	multicore_launch_core1(fn);
}

enum xop_status poll_xop_status(void)
{
	return status;
}

void terminate_op(void)
{
	reset();
}

union {
	struct {
		int fail;
	} blink_test;
	struct {
		unsigned tag;
		unsigned argument;
	} raw_tag;
	struct {
		unsigned cylinder;
	} select_cylinder;
	struct {
		unsigned head;
	} select_head;
	struct {
		int servo_offset;
		int data_strobe_delay;
	} read_enable;
	struct {
		unsigned buffer_index;
		unsigned n_32bit_words;
		unsigned index_sync;
		unsigned skip_checks;
	} read_data;
	struct {
		unsigned n_32bit_words_per_track;
		unsigned cylinder0;
		unsigned cylinder1;
		unsigned head_set;
		int servo_offset;
		int data_strobe_delay;
	} batch_read;

} job_args;

////////////////////////////////////
// blink test //////////////////////
void job_blink_test(void)
{
	for (int i = 0; i < 15; i++) {
		gpio_put(LED_PIN, 1);
		sleep_ms(50);
		gpio_put(LED_PIN, 0);
		sleep_ms(50);
	}
	if (!job_args.blink_test.fail) {
		DONE();
	} else {
		ERROR(XST_ERR_TEST);
	}
}
void xop_blink_test(int fail)
{
	reset();
	job_args.blink_test.fail = fail;
	run(job_blink_test);
}


////////////////////////////////////
// raw tag /////////////////////////
void job_raw_tag(void)
{
	tag_raw(job_args.raw_tag.tag, job_args.raw_tag.argument);
	DONE();
}
void xop_raw_tag(enum tag tag, unsigned argument)
{
	reset();
	job_args.raw_tag.tag = tag;
	job_args.raw_tag.argument = argument;
	run(job_raw_tag);
}


////////////////////////////////////
// select unit 0 ///////////////////
void job_select_unit0(void)
{
	select_unit0();
	DONE();
}
void xop_select_unit0(void)
{
	reset();
	run(job_select_unit0);
}


/////////////////////////////////////////////////////////////////////////////
// rtz / return to track zero ///////////////////////////////////////////////
void job_rtz(void)
{
	select_unit0_if_not_selected();
	rtz_seek();
	DONE();
}
void xop_rtz(void)
{
	reset();
	run(job_rtz);
}


/////////////////////////////////////////////////////////////////////////////
// select cylinder //////////////////////////////////////////////////////////
void job_select_cylinder(void)
{
	select_unit0_if_not_selected();
	select_cylinder(job_args.select_cylinder.cylinder);
	DONE();
}
void xop_select_cylinder(unsigned cylinder)
{
	reset();
	job_args.select_cylinder.cylinder = cylinder;
	run(job_select_cylinder);
}


/////////////////////////////////////////////////////////////////////////////
// select head //////////////////////////////////////////////////////////////
void job_select_head(void)
{
	select_unit0_if_not_selected();
	select_head(job_args.select_head.head);
	DONE();
}
void xop_select_head(unsigned head)
{
	reset();
	job_args.select_head.head = head;
	run(job_select_head);
}

/////////////////////////////////////////////////////////////////////////////
// read enable //////////////////////////////////////////////////////////////
void job_read_enable(void)
{
	select_unit0_if_not_selected();
	read_enable_ex(
		job_args.read_enable.servo_offset,
		job_args.read_enable.data_strobe_delay);
	DONE();
}
void xop_read_enable(int servo_offset, int data_strobe_delay)
{
	reset();
	job_args.read_enable.servo_offset = servo_offset;
	job_args.read_enable.data_strobe_delay = data_strobe_delay;
	run(job_read_enable);
}

/////////////////////////////////////////////////////////////////////////////
// read data ////////////////////////////////////////////////////////////////
void job_read_data(void)
{
	select_unit0_if_not_selected();
	read_data(
		job_args.read_data.buffer_index,
		job_args.read_data.n_32bit_words,
		job_args.read_data.index_sync,
		job_args.read_data.skip_checks);
	DONE();
}
unsigned xop_read_data(unsigned n_32bit_words, unsigned index_sync, unsigned skip_checks)
{
	reset();
	unsigned buffer_index = allocate_buffer(4*n_32bit_words);
	job_args.read_data.buffer_index = buffer_index;
	job_args.read_data.n_32bit_words = n_32bit_words;
	job_args.read_data.index_sync = index_sync;
	job_args.read_data.skip_checks = skip_checks;
	run(job_read_data);
	return buffer_index;
}


/////////////////////////////////////////////////////////////////////////////
// batch read ///////////////////////////////////////////////////////////////
void job_batch_read(void)
{
	select_unit0_if_not_selected();
	check_drive_error();
	const unsigned cylinder0 = job_args.batch_read.cylinder0;
	const unsigned cylinder1 = job_args.batch_read.cylinder1;
	const unsigned head_set = job_args.batch_read.head_set;
	const unsigned n_32bit_words_per_track = job_args.batch_read.n_32bit_words_per_track;
	const int servo_offset = job_args.batch_read.servo_offset;
	const int data_strobe_delay = job_args.batch_read.data_strobe_delay;

	for (unsigned cylinder = cylinder0; cylinder <= cylinder1; cylinder++) {
		select_cylinder(cylinder);
		// The CDC docs lists "read while off cylinder" as one of the
		// conditions that can trigger a FAULT. Although the following
		// section suggests the fault is only generated if requested
		// while seeking?:
		//   "(Read or Write) and Off Cylinder Fault"
		//   "This fault is generated if the drive is in an Off
		//   Cylinder condition and it receives a Read or Write gate
		//   from the controller."
		read_enable_ex(servo_offset, data_strobe_delay);
		unsigned mask = 1;
		for (unsigned head = 0; head < DRIVE_HEAD_COUNT; head++, mask <<= 1) {
			if ((head_set & mask) == 0) continue;
			select_head(head);
			const absolute_time_t t0 = get_absolute_time();
			while (!can_allocate_buffer()) {
				if ((get_absolute_time() - t0) > 10000000) {
					ERROR(XST_ERR_TIMEOUT);
				}
				sleep_us(5);
			}
			read_data(
				// XXX combine these 2? I don't like the redundancy
				allocate_buffer(n_32bit_words_per_track),
				n_32bit_words_per_track,
				/*index_sync=*/1,
				/*skip_checks=*/0);
		}
		control_clear();
	}
	DONE();
}
void xop_read_batch(unsigned cylinder0, unsigned cylinder1, unsigned head_set, unsigned n_32bit_words_per_track, int servo_offset, int data_strobe_delay)
{
	reset();
	job_args.batch_read.n_32bit_words_per_track = n_32bit_words_per_track;
	job_args.batch_read.cylinder0 = cylinder0;
	job_args.batch_read.cylinder1 = cylinder1;
	job_args.batch_read.head_set = head_set;
	job_args.batch_read.servo_offset = servo_offset;
	job_args.batch_read.data_strobe_delay = data_strobe_delay;
	run(job_batch_read);
}
