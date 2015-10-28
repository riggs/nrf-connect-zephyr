/* hci_core.h - Bluetooth HCI core access */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Enabling debug increases stack size requirement considerably */
#if defined(CONFIG_BLUETOOTH_DEBUG)
#define BT_STACK_DEBUG_EXTRA	512
#else
#define BT_STACK_DEBUG_EXTRA	0
#endif

#define BT_STACK(name, size) \
		char __stack name[(size) + BT_STACK_DEBUG_EXTRA]
#define BT_STACK_NOINIT(name, size) \
		char __noinit __stack name[(size) + BT_STACK_DEBUG_EXTRA]

/* LMP feature helpers */
#define lmp_bredr_capable(dev)	(!((dev).features[4] & BT_LMP_NO_BREDR))
#define lmp_le_capable(dev)	((dev).features[4] & BT_LMP_LE)

/* LL connection parameters */
#define LE_CONN_MIN_INTERVAL	0x0028
#define LE_CONN_MAX_INTERVAL	0x0038
#define LE_CONN_LATENCY		0x0000
#define LE_CONN_TIMEOUT		0x002a

/* bt_dev flags: the flags defined here represent BT controller state */
enum {
	BT_DEV_ADVERTISING,

	BT_DEV_SCANNING,
	BT_DEV_SCAN_FILTER_DUP,
};

/* State tracking for the local Bluetooth controller */
struct bt_dev {
	/* Local Bluetooth Device Address */
	bt_addr_t		bdaddr;

	/* Controller version & manufacturer information */
	uint8_t			hci_version;
	uint16_t		hci_revision;
	uint16_t		manufacturer;

	/* BR/EDR features page 0 */
	uint8_t			features[8];

	/* LE features */
	uint8_t			le_features[8];

	atomic_t		flags[1];

	/* Controller buffer information */
	uint8_t			le_pkts;
	uint16_t		le_mtu;
	struct nano_sem		le_pkts_sem;

	/* Number of commands controller can accept */
	uint8_t			ncmd;
	struct nano_sem		ncmd_sem;

	/* Last sent HCI command */
	struct net_buf		*sent_cmd;

	/* Queue for incoming HCI events & ACL data */
	struct nano_fifo	rx_queue;

	/* Queue for high priority HCI events which may unlock waiters
	 * in other fibers. Such events include Number of Completed
	 * Packets, as well as the Command Complete/Status events.
	 */
	struct nano_fifo	rx_prio_queue;

	/* Queue for outgoing HCI commands */
	struct nano_fifo	cmd_tx_queue;

	/* Registered HCI driver */
	struct bt_driver	*drv;
};

extern struct bt_dev bt_dev;

static inline int bt_addr_cmp(const bt_addr_t *a, const bt_addr_t *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline int bt_addr_le_cmp(const bt_addr_le_t *a, const bt_addr_le_t *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline void bt_addr_copy(bt_addr_t *dst, const bt_addr_t *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline void bt_addr_le_copy(bt_addr_le_t *dst, const bt_addr_le_t *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline bool bt_addr_le_is_rpa(const bt_addr_le_t *addr)
{
	if (addr->type != BT_ADDR_LE_RANDOM)
		return false;

	if ((addr->val[5] & 0xc0) == 0x40)
	       return true;

	return false;
}

static inline bool bt_addr_le_is_identity(const bt_addr_le_t *addr)
{
	if (addr->type == BT_ADDR_LE_PUBLIC)
		return true;

	/* Check for Random Static address type */
	if ((addr->val[5] & 0xc0) == 0xc0)
		return true;

	return false;
}

static inline bool bt_le_conn_params_valid(uint16_t min, uint16_t max,
					uint16_t latency, uint16_t timeout)
{
	uint16_t max_latency;

	if (min > max || min < 6 || max > 3200) {
		return false;
	}

	if (timeout < 10 || timeout > 3200) {
		return false;
	}

	/* calculation based on BT spec 4.2 [Vol3, PartA, 4.20]
	 * max_latency = ((timeout * 10)/(max * 1.25 * 2)) - 1;
	 */
	max_latency = (timeout * 4 / max) - 1;
	if (latency > 499 || latency > max_latency) {
		return false;
	}

	return true;
}

struct net_buf *bt_hci_cmd_create(uint16_t opcode, uint8_t param_len);
int bt_hci_cmd_send(uint16_t opcode, struct net_buf *buf);
int bt_hci_cmd_send_sync(uint16_t opcode, struct net_buf *buf,
			 struct net_buf **rsp);

/* The helper is only safe to be called from internal fibers as it's
 * not multi-threading safe
 */
#if defined(CONFIG_BLUETOOTH_DEBUG)
const char *bt_addr_str(const bt_addr_t *addr);
const char *bt_addr_le_str(const bt_addr_le_t *addr);
#endif

int bt_le_scan_update(void);

/* Buffer handling */

/** @def BT_BUF_MAX_DATA
 *  @brief Maximum amount of data that can fit in a buffer.
 *
 *  The biggest foreseeable buffer size requirement right now comes from
 *  the Bluetooth 4.2 SMP MTU which is 65. This then become 65 + 4 (L2CAP
 *  header) + 4 (ACL header) + 1 (H4 header) = 74. This also covers the
 *  biggest HCI commands and events which are a bit under the 70 byte
 *  mark.
 */
#define BT_BUF_MAX_DATA 74

#define BT_BUF_ACL_IN_MAX  7
#define BT_BUF_ACL_OUT_MAX 7

struct bt_hci_data {
	/** Type of data contained in a buffer (bt_buf_type) */
	uint8_t type;

	/** The command OpCode that the buffer contains */
	uint16_t opcode;

	/** Used by bt_hci_cmd_send_sync. Initially contains the waiting
	 *  semaphore, as the semaphore is given back contains the bt_buf
	 *  for the return parameters.
	 */
	void *sync;

};
struct bt_acl_data {
	/** Type of data contained in a buffer (bt_buf_type) */
	uint8_t type;

	/** ACL connection handle */
	uint16_t handle;
};

#define bt_hci(buf) ((struct bt_hci_data *)net_buf_user_data(buf))
#define bt_acl(buf) ((struct bt_acl_data *)net_buf_user_data(buf))
#define bt_type(buf) (*((uint8_t *)net_buf_user_data(buf)))

/** @brief Get a new buffer from the pool.
 *
 *  Get buffer from the available buffers pool with specified type and
 *  reserved headroom.
 *
 *  @param type Buffer type.
 *  @param reserve_head How much headroom to reserve.
 *
 *  @return New buffer or NULL if out of buffers.
 *
 *  @warning If there are no available buffers and the function is
 *  called from a task or fiber the call will block until a buffer
 *  becomes available in the pool.
 */
/* Temporary include for enum definition */
#include <bluetooth/driver.h>
struct net_buf *bt_buf_get(enum bt_buf_type type, size_t reserve_head);
