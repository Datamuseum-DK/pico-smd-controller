// blink codes for PANIC()
#define PANIC_BIND_FAILED            (0x21)
#define PANIC_NET_FAILED_TO_COME_UP  (0x22)
#define PANIC_DHSERV_FAILED          (0x23)
#define PANIC_NULL_NETIF             (0x24)
#define PANIC_STOP                   (0x1)
#define PANIC_XXX                    (0xAAA)

// deps
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/altcp.h"

// local
#include "config.h"
#include "common.h"
#include "log.h"
#include "low_level_controller.pio.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;
static void set_led(int x)
{
	gpio_put(LED_PIN, !!x);
}

static void blink(int on_ms, int off_ms)
{
	set_led(1);
	if (on_ms > 0) sleep_ms(on_ms);
	set_led(0);
	if (off_ms > 0) sleep_ms(off_ms);
}

// Panic and display error code using blink codes with the following repeating
// sequence:
//  - Fast strobe (signalling start of digit sequence)
//  - Pause
//  - For each non-zero hex digit, most significant first:
//     - Show hex digit as a series of blinks, one for 0x1, two for 0x2, etc
//     - Pause
// ( inspired by tinkering with an Audi 100 :-)
__attribute__ ((noreturn))
static void PANIC(uint32_t error)
{
	const int strobe_on_ms = 40;
	const int strobe_off_ms = 40;
	const int strobe_duration_ms = 500;
	const int digit_on_ms = 300;
	const int digit_off_ms = 200;
	const int pause_ms = 1500;
	const int n_hex_digits = sizeof(error)*2;
	while (1) {
		// strobe
		for (int t = 0; t <= strobe_duration_ms; t += (strobe_on_ms+strobe_off_ms)) {
			blink(strobe_on_ms, strobe_off_ms);
		}

		sleep_ms(pause_ms);

		// digits
		for (int i0 = 0; i0 < n_hex_digits; i0++) {
			int digit = (error >> (((n_hex_digits-1)-i0)<<2)) & 0xf;
			if (digit == 0) continue;
			for (int i1 = 0; i1 < digit; i1++) {
				blink(digit_on_ms, digit_off_ms);
			}
			sleep_ms(pause_ms);
		}
	}
}

static struct netif netif_context;

// XXX MAC address ought to be generated from the unique id returned by
// pico_get_unique_board_id(), but tinyusb links against this symbol and wants
// it to be "const uint8_t[]"...
const uint8_t tud_network_mac_address[6] = { 0x02, 0x12, 0x23, 0x45, 0x56, 0xf0 };

auto_init_mutex(lwip_mutex);
static int lwip_mutex_count = 0;
sys_prot_t sys_arch_protect(void)
{
	uint32_t owner;
	if (!mutex_try_enter(&lwip_mutex, &owner)) {
		if (owner != get_core_num()) {
			mutex_enter_blocking(&lwip_mutex);
		}
	}
	lwip_mutex_count++;
	return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
	if (!lwip_mutex_count) return;
	lwip_mutex_count--;
	if (lwip_mutex_count) return;
	mutex_exit(&lwip_mutex);
}

uint32_t sys_now(void)
{
	return to_ms_since_boot(get_absolute_time());
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
	struct pbuf *p = (struct pbuf *)ref;
	struct pbuf *q;
	uint16_t len = 0;
	for(q = p; q != NULL; q = q->next) {
		memcpy(dst, (char *)q->payload, q->len);
		dst += q->len;
		len += q->len;
		if (q->len == q->tot_len) break;
	}
	return len;
}

enum {
	CONFIG_ID_RNDIS,
	CONFIG_ID_COUNT,
};

enum {
	ITF_NUM_CDC,
	ITF_NUM_CDC_DATA,
	ITF_NUM_COUNT,
};

enum {
	STR_ID_LANGID,
	STR_ID_MANUFACTURER,
	STR_ID_PRODUCT,
	STR_ID_SERIAL,
	STR_ID_INTERFACE,
	STR_ID_MAC,
	STR_ID_COUNT,
};

static char const* string_desc_arr [] = {
	[STR_ID_LANGID]       = (const char[]) { 0x09, 0x04 }, // English (0x0409)
	[STR_ID_MANUFACTURER] = "TinyUSB",
	[STR_ID_PRODUCT]      = "smd-pico-controller listening on 192.168.42.69:6942",
	[STR_ID_INTERFACE]    = "TinyUSB Network Interface",
};

static tusb_desc_device_t const desc_device = {
	.bLength            = sizeof(tusb_desc_device_t),
	.bDescriptorType    = TUSB_DESC_DEVICE,
	.bcdUSB             = 0x0200,

	.bDeviceClass       = TUSB_CLASS_MISC,
	.bDeviceSubClass    = MISC_SUBCLASS_COMMON,
	.bDeviceProtocol    = MISC_PROTOCOL_IAD,

	.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

	.idVendor           = 0xbe3f,
	.idProduct          = 0x4269,
	.bcdDevice          = 0x0101,

	.iManufacturer      = STR_ID_MANUFACTURER,
	.iProduct           = STR_ID_PRODUCT,
	.iSerialNumber      = STR_ID_SERIAL,

	.bNumConfigurations = CONFIG_ID_COUNT
};

uint8_t const* tud_descriptor_device_cb(void)
{
	return (uint8_t const*) &desc_device;
}

static char hex_digit(int v)
{
	if (!(0 <= v && v <= 0xf)) PANIC(PANIC_XXX);
	return "0123456789ABCDEF"[v];
}

static uint16_t _desc_str[64];
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	uint16_t* wp = &_desc_str[1];
	switch (index) {
	case STR_ID_LANGID: {
		memcpy(wp++, string_desc_arr[STR_ID_LANGID], 2);
	} break;
	case STR_ID_SERIAL: {
		pico_unique_board_id_t id;
		pico_get_unique_board_id(&id);
		for (int i = 0; i < sizeof(id.id); i++) {
			*(wp++) = hex_digit((id.id[i] >> 4) & 0xf);
			*(wp++) = hex_digit((id.id[i] >> 0) & 0xf);
		}
	} break;
	case STR_ID_MAC: {
		for (int i = 0; i < sizeof(tud_network_mac_address); i++) {
			*(wp++) = hex_digit((tud_network_mac_address[i] >> 4) & 0xf);
			*(wp++) = hex_digit((tud_network_mac_address[i] >> 0) & 0xf);
		}
	} break;
	default: {
		if (index >= ARRAY_LENGTH(string_desc_arr)) return NULL;
		const char* sp = string_desc_arr[index];
		if (sp == NULL) return NULL;
		while (wp < (ARRAY_END(_desc_str))) {
			char c = *(sp++);
			if (c == 0) break;
			*(wp++) = c;
		}
	} break;
	}

	// encode type and length in bytes (including header)
	_desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*(wp - _desc_str));
	return _desc_str;
}

static struct pbuf *received_frame;
void tud_network_init_cb(void)
{
        if (!received_frame) return;
	pbuf_free(received_frame);
	received_frame = NULL;
}

bool tud_network_recv_cb(const uint8_t* src, uint16_t size)
{
	if (received_frame) return false;
	if (size) {
		struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
		if (p) {
			memcpy(p->payload, src, size);
			received_frame = p;
		}
	}
	return true;
}

#define EPNUM_NET_NOTIF   0x81
#define EPNUM_NET_OUT     0x02
#define EPNUM_NET_IN      0x82
static uint8_t const rndis_config[] = {
	TUD_CONFIG_DESCRIPTOR(
		CONFIG_ID_RNDIS+1,
		ITF_NUM_COUNT,
		0,
		(TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN),
		0,
		100),
	TUD_RNDIS_DESCRIPTOR(
		ITF_NUM_CDC,
		STR_ID_INTERFACE,
		EPNUM_NET_NOTIF,
		8,
		EPNUM_NET_OUT,
		EPNUM_NET_IN,
		CFG_TUD_NET_ENDPOINT_SIZE),
};

static uint8_t const * const desc_configs[CONFIG_ID_COUNT] = {
	[CONFIG_ID_RNDIS] = rndis_config,
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
	return (index < CONFIG_ID_COUNT) ? desc_configs[index] : NULL;
}

static err_t linkoutput_cb(struct netif *netif, struct pbuf *p)
{
	for (;;) {
		if (!tud_ready()) return ERR_USE;
		if (tud_network_can_xmit(p->tot_len)) {
			tud_network_xmit(p, 0 /* NOTE: `0` is passed to tud_network_xmit_cb() as `arg` */);
			return ERR_OK;
		}
		tud_task();
	}
}

static err_t netif_init_cb(struct netif *netif)
{
	if (netif == NULL) PANIC(PANIC_NULL_NETIF);
	netif->mtu = CFG_TUD_NET_MTU;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
	netif->state = NULL;
	netif->name[0] = 'E';
	netif->name[1] = 'X';
	netif->linkoutput = linkoutput_cb;
	netif->output = etharp_output;
	return ERR_OK;
}

#define TCP_PRIORITY TCP_PRIO_MIN

static err_t conn_recv(void* arg, struct altcp_pcb* pcb, struct pbuf *p, err_t err)
{
	PANIC(PANIC_XXX);
}

static void conn_err(void *arg, err_t err)
{
	PANIC(PANIC_XXX);
}

static err_t conn_sent(void *arg, struct altcp_pcb *pcb, u16_t len)
{
	PANIC(PANIC_XXX);
}

static err_t accept_connection(void* arg, struct altcp_pcb* pcb, err_t err) {
	if (err != ERR_OK) return ERR_VAL;
	if (pcb == NULL)   return ERR_VAL;

	altcp_setprio(pcb, TCP_PRIORITY);
	altcp_arg(pcb, NULL); // NOTE value passed to callbacks as `void* arg`

	altcp_recv(pcb, conn_recv);
	altcp_err(pcb, conn_err);
	altcp_sent(pcb, conn_sent);

	return ERR_OK;
}

static void init(void)
{
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, 0);

	tusb_init();
	lwip_init();

	{
		struct netif* n = &netif_context;
		n->hwaddr_len = sizeof(tud_network_mac_address);
		memcpy(n->hwaddr, tud_network_mac_address, sizeof tud_network_mac_address);
		n = netif_add(n, &ip_addr, &ip_netmask, &ip_gateway, NULL, netif_init_cb, ip_input);
		netif_set_default(n);
		int attempts = 0;
		while (!netif_is_up(n)) {
			sleep_ms(25);
			attempts++;
			if (attempts > 40) PANIC(PANIC_NET_FAILED_TO_COME_UP);
		}
	}

	{
		int attempts = 0;
		while (dhserv_init(&dhcp_config) != ERR_OK) {
			sleep_ms(25);
			attempts++;
			if (attempts > 40) PANIC(PANIC_DHSERV_FAILED);
		}
	}

	struct altcp_pcb* pcb = altcp_tcp_new();
	altcp_setprio(pcb, TCP_PRIORITY);
	err_t err = altcp_bind(pcb, IP_ANY_TYPE, LISTEN_PORT);
	if (err != ERR_OK) PANIC(PANIC_BIND_FAILED);
	pcb = altcp_listen(pcb);
	altcp_accept(pcb, accept_connection);
}

void service_traffic(void)
{
	if (received_frame) {
		ethernet_input(received_frame, &netif_context);
		pbuf_free(received_frame);
		received_frame = NULL;
		tud_network_recv_renew();
	}
	sys_check_timeouts();
}

int main()
{
	init();

	blink(50, 0); // "Hi, we're up!"

	for (;;) {
		tud_task();
		service_traffic();
	}

	PANIC(PANIC_STOP);
}
