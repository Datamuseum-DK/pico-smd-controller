#ifndef CONFIG_H

#define LISTEN_PORT 6942

static const ip_addr_t ip_addr    = IPADDR4_INIT_BYTES(192, 168, 42, 69);
static const ip_addr_t ip_netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip_addr_t ip_gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

#define DHCP_LEASE (24*60*60)
static dhcp_entry_t dhcp_config_entries[] = {
	{ {0}, IPADDR4_INIT_BYTES(192, 168, 42, 66), DHCP_LEASE },
	{ {0}, IPADDR4_INIT_BYTES(192, 168, 42, 67), DHCP_LEASE },
	{ {0}, IPADDR4_INIT_BYTES(192, 168, 42, 68), DHCP_LEASE },
};

static const dhcp_config_t dhcp_config = {
	.router = IPADDR4_INIT_BYTES(0, 0, 0, 0), // router address
	.port = 67, // listen port
	.dns = IPADDR4_INIT_BYTES(0, 0, 0, 0), // dns server
	"", // dns suffix
	TU_ARRAY_SIZE(dhcp_config_entries),
	dhcp_config_entries,
};

#define CONFIG_H
#endif
