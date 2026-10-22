/* SPDX-License-Identifier: APSL-2.0
 * Adapted from haxi0/hackios-xnu-kernel ce8ef8f, instantnoodle_touch.c,
 * derived from OOSEX GANG HackiOS redwood_goodix.c and ios-guacamole/ios-sweet.
 * UniXNU adaptation: native digitizer publication, opt-in init, mapping and cmd-db checks.
 *
 * The touch screen of the OnePlus 8 (instantnoodle, SM8250): a Samsung S6SY761 ("SY771") at
 * I2C address 0x48, as a source for UniXNU NativeHidInput.
 * On only with boot-arg instantnoodle-touch=1.
 *
 * Its bus is serial engine 13 (i2c@a94000, pins gpio36/gpio37 with external pull-ups); this
 * driver does not use the engine but drives the two pins itself (open drain: low = output 0,
 * high = input), as ios-instantnoodle and ios-guacamole (OnePlus 7 Pro, same chip) did: the
 * engine's FIFO path is disabled on these phones and would need GPI DMA. Reset is gpio38, the
 * report line gpio39 (unused: polled every 10 ms). Supplies, from the phone's device tree: the
 * chip's 1.8 V I/O from LDO1 of the PM8150A ("ldoc1"), its 3 V analog from LDO13 of the PM8150
 * ("ldoa13"), both through RPMh. All four pins are in the SOUTH tile of the SM8250 TLMM
 * (0x0f500000; pins at 0x1000 * gpio, Linux pinctrl-sm8250.c).
 *
 * Protocol: Linux drivers/input/touchscreen/s6sy761.c and the vendor's sec_ts driver. On this
 * phone the configuration commands (TOUCH_FUNC, SENSE_ON) are not acknowledged, yet the chip
 * reports touches after its boot-complete event; they are sent anyway.
 *
 * Coordinates: the panel's own, 1080 x 2400 (device tree touchpanel,panel-coords), scaled to
 * NativeHidInput's x 0..30480 and y 0..20320.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <IOKit/IOLib.h>
#include "S6SY761.h"
#include "TouchPower.h"

extern volatile uint32_t native_touch_seq, native_touch_count;
extern volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
extern void native_touch_published(void);
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
void native_touch_probe(void);

static vm_offset_t tlmm, rsc, cmd_db;

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
	__asm__ volatile ("dsb osh" ::: "memory");
}

static void
sleep_us(uint32_t us)
{
	delay_for_interval(us, NSEC_PER_USEC);
}

static void
sleep_ms(uint32_t ms)
{
	IOSleep(ms);
}

/* ---- pins (TLMM, SOUTH tile) --------------------------------------------------------------------- */

#define TLMM_SOUTH_PHYS         0x0f500000UL
#define TLMM_SIZE               0x100000UL
#define GPIO_CFG(n)             (tlmm + 0x1000UL * (n))         /* [1:0] pull, [5:2] function, [8:6] drive, [9] output */
#define GPIO_IO(n)              (tlmm + 0x1000UL * (n) + 4)     /* bit 0 in, bit 1 out */
#define PULL_NONE               0
#define PULL_UP                 3
#define PIN_SDA                 36
#define PIN_SCL                 37
#define PIN_RESET               38
#define PIN_IRQ                 39

static void
gpio_configure(unsigned pin, unsigned pull, unsigned ma, bool output)
{
	wr(GPIO_CFG(pin), pull | 0u << 2 | (ma / 2 - 1) << 6 | (output ? 1u << 9 : 0));
}

static void
gpio_output(unsigned pin, bool high, unsigned pull, unsigned ma)
{
	wr(GPIO_IO(pin), high ? 2 : 0);         /* the level first, so that the pin never drives the wrong one */
	gpio_configure(pin, pull, ma, true);
}

/* Open drain: "low" drives 0, "high" lets go (input; the external pull-up makes the level). */
static void
line(unsigned pin, bool high)
{
	if (high) {
		gpio_configure(pin, PULL_NONE, 2, false);
	} else {
		wr(GPIO_IO(pin), 0);
		gpio_configure(pin, PULL_NONE, 2, true);
	}
}

static bool
level(unsigned pin)
{
	return rd(GPIO_IO(pin)) & 1;
}

/* ---- I2C on the two pins, about 100 kHz ---------------------------------------------------------- */

static void
bb_delay(void)
{
	delay(4);
}

static void
bb_start(void)
{
	line(PIN_SDA, true);
	line(PIN_SCL, true);
	bb_delay();
	line(PIN_SDA, false);
	bb_delay();
	line(PIN_SCL, false);
	bb_delay();
}

static void
bb_stop(void)
{
	line(PIN_SDA, false);
	bb_delay();
	line(PIN_SCL, true);
	bb_delay();
	line(PIN_SDA, true);
	bb_delay();
}

static void
bb_tx(uint8_t b)
{
	for (int i = 7; i >= 0; i--) {
		line(PIN_SDA, (b >> i) & 1);
		bb_delay();
		line(PIN_SCL, true);
		bb_delay();
		line(PIN_SCL, false);
		bb_delay();
	}
}

/* 0 when the target pulled SDA low for the byte just sent */
static int
bb_ack(void)
{
	int nack;

	line(PIN_SDA, true);
	bb_delay();
	line(PIN_SCL, true);
	bb_delay();
	nack = level(PIN_SDA);
	line(PIN_SCL, false);
	bb_delay();
	return nack ? ENXIO : 0;
}

static uint8_t
bb_rx(bool ack)
{
	uint8_t v = 0;

	line(PIN_SDA, true);
	for (int i = 7; i >= 0; i--) {
		bb_delay();
		line(PIN_SCL, true);
		bb_delay();
		v = (uint8_t)(v << 1 | level(PIN_SDA));
		line(PIN_SCL, false);
	}
	line(PIN_SDA, !ack);
	bb_delay();
	line(PIN_SCL, true);
	bb_delay();
	line(PIN_SCL, false);
	line(PIN_SDA, true);
	bb_delay();
	return v;
}

/* nine clocks, so that a target holding SDA low lets go */
static void
bb_recover(void)
{
	line(PIN_SDA, true);
	for (int i = 0; i < 9; i++) {
		line(PIN_SCL, false);
		bb_delay();
		line(PIN_SCL, true);
		bb_delay();
	}
	bb_stop();
}

#define TS_ADDR                 0x48

static unsigned i2c_errors;

static int
ts_write(const uint8_t *tx, unsigned n)
{
	int err;

	bb_start();
	bb_tx(TS_ADDR << 1);
	err = bb_ack();
	for (unsigned i = 0; i < n && err == 0; i++) {
		bb_tx(tx[i]);
		err = bb_ack();
	}
	bb_stop();
	if (err) {
		i2c_errors++;
	}
	return err;
}

static int
ts_read(uint8_t reg, uint8_t *buf, unsigned n)
{
	int err;

	bb_start();
	bb_tx(TS_ADDR << 1);
	err = bb_ack();
	if (err == 0) {
		bb_tx(reg);
		err = bb_ack();
	}
	if (err == 0) {
		bb_start();                     /* repeated start */
		bb_tx(TS_ADDR << 1 | 1);
		err = bb_ack();
	}
	for (unsigned i = 0; i < n && err == 0; i++) {
		buf[i] = bb_rx(i + 1 < n);
	}
	bb_stop();
	if (err) {
		i2c_errors++;
	}
	return err;
}

/* ---- the supplies (RPMh), as bsd/dev/redwood_goodix.c --------------------------------------------- */

#define CMD_DB_PHYS             0x80860000UL
#define CMD_DB_SIZE             0x20000UL
#define RSC_PHYS                0x18220000UL    /* the application processor's "DRV 2" */
#define RSC_SIZE                0x10000UL
#define TCS(reg)                (rsc + 0xd00 + (reg))           /* command slot set 0, an "active" one */
#define TCS_IRQ_ENABLE          0x00
#define TCS_IRQ_STATUS          0x04
#define TCS_IRQ_CLEAR           0x08
#define TCS_WAIT_FOR_COMPLETION 0x10
#define TCS_CONTROL             0x14
#define TCS_CONTROL_ENABLE      (1u << 16)
#define TCS_CONTROL_TRIGGER     (1u << 24)
#define TCS_STATUS              0x18            /* 0 while busy */
#define TCS_CMD_ENABLE          0x1c
#define TCS_CMD_MSGID(n)        (0x30 + 20 * (n))
#define TCS_CMD_ADDR(n)         (0x34 + 20 * (n))
#define TCS_CMD_DATA(n)         (0x38 + 20 * (n))
#define MSGID_WRITE_4_BYTES     0x10008
#define MSGID_RESPONSE          0x100
#define VRM_VOLTAGE             0
#define VRM_ENABLE              4
#define VRM_MODE                8
#define VRM_MODE_LDO_HPM        7

static uint32_t
rpmh_address(const char *name)
{
    return noodle_regulator_address((const void *)cmd_db, CMD_DB_SIZE, name);
}

static bool
rpmh_wait(uint32_t reg, uint32_t mask, uint32_t value)
{
	for (int i = 0; i < 2000; i++) {
		if ((rd(TCS(reg)) & mask) == value) {
			return true;
		}
		sleep_us(100);
	}
	return false;
}

static bool
rpmh_write(const uint32_t *address, const uint32_t *data, unsigned count)
{
	uint32_t irq_enable = rd(TCS(TCS_IRQ_ENABLE));
	bool done;

	for (int i = 0; rd(TCS(TCS_STATUS)) == 0; i++) {
		if (i > 2000) {
			printf("touch: RPMh: the command slots stay busy\n");
			return false;
		}
		sleep_us(100);
	}
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	for (unsigned i = 0; i < count; i++) {
		wr(TCS(TCS_CMD_MSGID(i)), MSGID_WRITE_4_BYTES | MSGID_RESPONSE);
		wr(TCS(TCS_CMD_ADDR(i)), address[i]);
		wr(TCS(TCS_CMD_DATA(i)), data[i]);
	}
	wr(TCS(TCS_WAIT_FOR_COMPLETION), (1u << count) - 1);
	wr(TCS(TCS_CMD_ENABLE), (1u << count) - 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable | 1);
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CONTROL), TCS_CONTROL_ENABLE | TCS_CONTROL_TRIGGER);
	done = rpmh_wait(TCS_IRQ_STATUS, 1, 1);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_TRIGGER);
	wr(TCS(TCS_CONTROL), rd(TCS(TCS_CONTROL)) & ~TCS_CONTROL_ENABLE);
	wr(TCS(TCS_CMD_ENABLE), 0);
	wr(TCS(TCS_WAIT_FOR_COMPLETION), 0);
	wr(TCS(TCS_IRQ_CLEAR), 1);
	wr(TCS(TCS_IRQ_ENABLE), irq_enable);
	return done;
}

static bool
supply_on(const char *name, uint32_t mv)
{
	uint32_t base = rpmh_address(name);
	uint32_t address[3] = { base + VRM_VOLTAGE, base + VRM_MODE, base + VRM_ENABLE };
	uint32_t data[3] = { mv, VRM_MODE_LDO_HPM, 1 };         /* enable last */

	if (base == 0 || (base >> 16 & 0xf) != 4) {
		printf("touch: RPMh: no regulator \"%s\" (address %#x)\n", name, base);
		return false;
	}
	if (!rpmh_write(address, data, 3)) {
		printf("touch: RPMh: %s %u mV: no answer\n", name, mv);
		return false;
	}
	printf("touch: %s at %#x on, %u mV\n", name, base, mv);
	return true;
}

/* ---- the S6SY761 ---------------------------------------------------------------------------------- */

#define CMD_SENSE_ON            0x10
#define CMD_FW_INTEGRITY        0x21
#define CMD_TOUCH_FUNC          0x30
#define CMD_DEVICE_ID           0x52
#define CMD_BOOT_STATUS         0x55
#define CMD_READ_EVENT          0x60
#define CMD_READ_ALL            0x61
#define EVENT_LEN               8
#define EVENT_INFO              0x02
#define EVENT_VENDOR            0x07
#define ACK_BOOT_COMPLETE       0x00
#define MASK_TOUCH              0x01
#define MAX_FINGERS             10
#define PANEL_W                 1080
#define PANEL_H                 2400

static struct noodle_touch_frame fingers;
static unsigned events;

static int
wait_boot_complete(void)
{
	uint8_t ev[EVENT_LEN];

	for (int i = 0; i < 50; i++) {
		if (ts_read(CMD_READ_EVENT, ev, sizeof(ev)) == 0) {
			unsigned status = ev[0] >> 2 & 0xf;

			if ((status == EVENT_INFO || status == EVENT_VENDOR) && ev[1] == ACK_BOOT_COMPLETE) {
				return 0;
			}
		}
		sleep_ms(20);
	}
	return -1;
}

static void
publish(void)
{
    native_touch_seq++;
    __asm__ volatile("dmb ish" ::: "memory");
    unsigned count = 0;
    for (unsigned i = 0; i < NOODLE_FINGERS; i++) {
        if (!fingers.down[i]) continue;
        native_touch_xs[count] = fingers.x[i];
        native_touch_ys[count] = fingers.y[i];
        native_touch_ids[count++] = (int)i;
    }
    native_touch_count = count;
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_seq++;
    native_touch_published();
}

static void
handle_event(const uint8_t *ev)
{
    if (noodle_touch_event(&fingers, ev)) {
        events++;
        publish();
    }
}

static int
poll_events(void)
{
	uint8_t ev[EVENT_LEN], stack[MAX_FINGERS * EVENT_LEN];
	unsigned left;
	int err = ts_read(CMD_READ_EVENT, ev, sizeof(ev));

	if (err) {
		return err;
	}
	handle_event(ev);
	left = ev[7] & 0x3f;
	if (left > MAX_FINGERS) {
		left = MAX_FINGERS;
	}
	if (left && ts_read(CMD_READ_ALL, stack, left * EVENT_LEN) == 0) {
		for (unsigned i = 0; i < left; i++) {
			handle_event(&stack[i * EVENT_LEN]);
		}
	}
	return 0;
}

static void
release_all(void)
{
    memset(&fingers, 0, sizeof(fingers));
    publish();
}

static void
touch_thread(void *parameter __unused, wait_result_t result __unused)
{
	uint8_t id[3] = { 0, 0, 0 }, boot = 0, fw = 0, func[3] = { CMD_TOUCH_FUNC, MASK_TOUCH, 0 }, sense = CMD_SENSE_ON;
	unsigned errors = 0;

	tlmm = ml_io_map(TLMM_SOUTH_PHYS, TLMM_SIZE);
	rsc = ml_io_map(RSC_PHYS, RSC_SIZE);
	cmd_db = ml_io_map_wcomb(CMD_DB_PHYS, CMD_DB_SIZE);    /* memory, not registers */
    if (!tlmm || !rsc || !cmd_db) {
        printf("instantnoodle touch: MMIO mapping failed\n");
        goto out;
    }
    /* This board has RPMh RSC v2, with active TCS0 at +0xd00. */
    if (rd(rsc) != 0x20000) {
        printf("instantnoodle touch: unexpected RSC id %#x\n", rd(rsc));
        goto out;
    }
	printf("touch: S6SY761: pins sda %u scl %u reset %u irq %u: cfg %#x %#x %#x %#x\n", PIN_SDA, PIN_SCL, PIN_RESET,
	    PIN_IRQ, rd(GPIO_CFG(PIN_SDA)), rd(GPIO_CFG(PIN_SCL)), rd(GPIO_CFG(PIN_RESET)), rd(GPIO_CFG(PIN_IRQ)));

	line(PIN_SDA, true);
	line(PIN_SCL, true);
	gpio_configure(PIN_IRQ, PULL_NONE, 2, false);
	gpio_output(PIN_RESET, false, PULL_UP, 8);     /* in reset while the supplies come up */
	if (!supply_on("ldoc1", 1800) || (sleep_ms(1), !supply_on("ldoa13", 3008))) {
		printf("touch: no supplies: no touch screen\n");
		goto out;
	}
	sleep_ms(20);
	gpio_output(PIN_RESET, true, PULL_UP, 8);
	sleep_ms(140);                                  /* Linux s6sy761: power-on wait */
	bb_recover();

	printf("touch: boot-complete event: %s\n", wait_boot_complete() == 0 ? "seen" : "NOT SEEN (going on)");
	if (ts_read(CMD_DEVICE_ID, id, 3) != 0) {
		printf("touch: no answer at address %#x: no touch screen\n", TS_ADDR);
		goto out;
	}
	ts_read(CMD_BOOT_STATUS, &boot, 1);
	ts_read(CMD_FW_INTEGRITY, &fw, 1);
	printf("touch: device id %02x %02x %02x, boot status %#x (0x20: application), firmware integrity %#x\n", id[0], id[1],
	    id[2], boot, fw);
	/* not acknowledged on this phone, harmless (see the top) */
	ts_write(func, 3);
	ts_write(&sense, 1);
	printf("touch: polling every 10 ms\n");

	for (unsigned tick = 1;; tick++) {
		if (poll_events() != 0) {
			if (++errors == 10) {
				release_all();
				bb_recover();
			}
		} else {
			errors = 0;
		}
		if (tick % 6000 == 0) {                 /* once a minute */
			printf("touch: %u events, %u bus errors so far\n", events, i2c_errors);
		}
		sleep_ms(10);
	}
out:
	thread_terminate(current_thread());
}

void
native_touch_probe(void)
{
    static bool started;
    uint32_t enabled = 0;
    thread_t thread;
    if (started || !PE_parse_boot_argn("instantnoodle-touch", &enabled, sizeof(enabled)) || enabled != 1) return;
    if (kernel_thread_start(touch_thread, NULL, &thread) == KERN_SUCCESS) {
        started = true;
        thread_deallocate(thread);
    } else {
        printf("instantnoodle touch: thread creation failed\n");
    }
}
