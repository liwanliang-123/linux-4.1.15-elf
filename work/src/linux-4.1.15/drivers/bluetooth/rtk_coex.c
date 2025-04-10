/*
*
*  Realtek Bluetooth USB driver
*
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/dcache.h>
#include <linux/version.h>
#include <net/sock.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "rtk_coex.h"

/* Software coex message can be sent to and receive from WiFi driver by
 * UDP socket or exported symbol */
/* #define RTK_COEX_OVER_SYMBOL */

#if BTRTL_HCI_IF == BTRTL_HCIUSB
#include <linux/usb.h>
#include "rtk_bt.h"
#undef RTKBT_DBG
#undef RTKBT_INFO
#undef RTKBT_WARN
#undef RTKBT_ERR

#elif BTRTL_HCI_IF == BTRTL_HCIUART
/* #define HCI_VERSION_CODE KERNEL_VERSION(3, 14, 41) */
#define HCI_VERSION_CODE LINUX_VERSION_CODE

#else
#error "Please set type of HCI interface"
#endif

#define RTK_VERSION "1.2"

#if 0
#define RTKBT_DBG(fmt, arg...) printk(KERN_INFO "rtk_btcoex: " fmt "\n" , ## arg)
#define RTKBT_INFO(fmt, arg...) printk(KERN_INFO "rtk_btcoex: " fmt "\n" , ## arg)
#else
#define RTKBT_DBG(fmt, arg...)
#define RTKBT_INFO(fmt, arg...)
#endif

#define RTKBT_WARN(fmt, arg...) printk(KERN_WARNING "rtk_btcoex: " fmt "\n", ## arg)
#define RTKBT_ERR(fmt, arg...) printk(KERN_WARNING "rtk_btcoex: " fmt "\n", ## arg)

static struct rtl_coex_struct btrtl_coex;

#ifdef RTB_SOFTWARE_MAILBOX
#ifdef RTK_COEX_OVER_SYMBOL
static struct sk_buff_head rtw_q;
static struct workqueue_struct *rtw_wq;
static struct work_struct rtw_work;
static u8 rtw_coex_on;
#endif
#endif

#define is_profile_connected(profile)   ((btrtl_coex.profile_bitmap & BIT(profile)) > 0)
#define is_profile_busy(profile)        ((btrtl_coex.profile_status & BIT(profile)) > 0)

#ifdef RTB_SOFTWARE_MAILBOX
static void rtk_handle_event_from_wifi(uint8_t * msg);
#endif

static int rtl_alloc_buff(struct rtl_coex_struct *coex)
{
	struct rtl_hci_ev *ev;
	struct rtl_l2_buff *l2;
	int i;
	int order;
	unsigned long addr;
	unsigned long addr2;
	int ev_size;
	int l2_size;
	int n;

	spin_lock_init(&coex->buff_lock);

	INIT_LIST_HEAD(&coex->ev_used_list);
	INIT_LIST_HEAD(&coex->ev_free_list);

	INIT_LIST_HEAD(&coex->l2_used_list);
	INIT_LIST_HEAD(&coex->l2_free_list);

	n = NUM_RTL_HCI_EV * sizeof(struct rtl_hci_ev);
	ev_size = ALIGN(n, sizeof(unsigned long));

	n = L2_MAX_PKTS * sizeof(struct rtl_l2_buff);
	l2_size = ALIGN(n, sizeof(unsigned long));

	RTKBT_DBG("alloc buffers %d, %d for ev and l2", ev_size, l2_size);

	order = get_order(ev_size + l2_size);
	addr = __get_free_pages(GFP_KERNEL, order);
	if (!addr) {
		RTKBT_ERR("failed to alloc buffers for ev and l2.");
		return -ENOMEM;
	}
	memset((void *)addr, 0, ev_size + l2_size);

	coex->pages_addr = addr;
	coex->buff_size = ev_size + l2_size;

	ev = (struct rtl_hci_ev *)addr;
	for (i = 0; i < NUM_RTL_HCI_EV; i++) {
		list_add_tail(&ev->list, &coex->ev_free_list);
		ev++;
	}

	addr2 = addr + ev_size;
	l2 = (struct rtl_l2_buff *)addr2;
	for (i = 0; i < L2_MAX_PKTS; i++) {
		list_add_tail(&l2->list, &coex->l2_free_list);
		l2++;
	}

	return 0;
}

static void rtl_free_buff(struct rtl_coex_struct *coex)
{
	struct rtl_hci_ev *ev;
	struct rtl_l2_buff *l2;
	unsigned long flags;

	spin_lock_irqsave(&coex->buff_lock, flags);

	while (!list_empty(&coex->ev_used_list)) {
		ev = list_entry(coex->ev_used_list.next, struct rtl_hci_ev,
				list);
		list_del(&ev->list);
	}

	while (!list_empty(&coex->ev_free_list)) {
		ev = list_entry(coex->ev_free_list.next, struct rtl_hci_ev,
				list);
		list_del(&ev->list);
	}

	while (!list_empty(&coex->l2_used_list)) {
		l2 = list_entry(coex->l2_used_list.next, struct rtl_l2_buff,
				list);
		list_del(&l2->list);
	}

	while (!list_empty(&coex->l2_free_list)) {
		l2 = list_entry(coex->l2_free_list.next, struct rtl_l2_buff,
				list);
		list_del(&l2->list);
	}

	spin_unlock_irqrestore(&coex->buff_lock, flags);

	if (coex->buff_size > 0) {
		free_pages(coex->pages_addr, get_order(coex->buff_size));
		coex->pages_addr = 0;
		coex->buff_size = 0;
	}
}

static struct rtl_hci_ev *rtl_ev_node_get(struct rtl_coex_struct *coex)
{
	struct rtl_hci_ev *ev;
	unsigned long flags;

	if (!coex->buff_size)
		return NULL;

	spin_lock_irqsave(&coex->buff_lock, flags);
	if (!list_empty(&coex->ev_free_list)) {
		ev = list_entry(coex->ev_free_list.next, struct rtl_hci_ev,
				list);
		list_del(&ev->list);
	} else
		ev = NULL;
	spin_unlock_irqrestore(&coex->buff_lock, flags);
	return ev;
}

static int rtl_ev_node_to_used(struct rtl_coex_struct *coex,
		struct rtl_hci_ev *ev)
{
	unsigned long flags;

	spin_lock_irqsave(&coex->buff_lock, flags);
	list_add_tail(&ev->list, &coex->ev_used_list);
	spin_unlock_irqrestore(&coex->buff_lock, flags);

	return 0;
}

static struct rtl_l2_buff *rtl_l2_node_get(struct rtl_coex_struct *coex)
{
	struct rtl_l2_buff *l2;
	unsigned long flags;

	if (!coex->buff_size)
		return NULL;

	spin_lock_irqsave(&coex->buff_lock, flags);

	if(!list_empty(&coex->l2_free_list)) {
		l2 = list_entry(coex->l2_free_list.next, struct rtl_l2_buff,
				list);
		list_del(&l2->list);
	} else
		l2 = NULL;

	spin_unlock_irqrestore(&coex->buff_lock, flags);
	return l2;
}

static int rtl_l2_node_to_used(struct rtl_coex_struct *coex,
		struct rtl_l2_buff *l2)
{
	unsigned long flags;

	spin_lock_irqsave(&coex->buff_lock, flags);
	list_add_tail(&l2->list, &coex->l2_used_list);
	spin_unlock_irqrestore(&coex->buff_lock, flags);

	return 0;
}

static int8_t psm_to_profile_index(uint16_t psm)
{
	switch (psm) {
	case PSM_AVCTP:
	case PSM_SDP:
		return -1;	//ignore

	case PSM_HID:
	case PSM_HID_INT:
		return profile_hid;

	case PSM_AVDTP:
		return profile_a2dp;

	case PSM_PAN:
	case PSM_OPP:
	case PSM_FTP:
	case PSM_BIP:
	case PSM_RFCOMM:
		return profile_pan;

	default:
		return profile_pan;
	}
}

static rtk_prof_info *find_by_psm(u16 psm)
{
	struct list_head *head = &btrtl_coex.profile_list;
	struct list_head *iter = NULL;
	struct list_head *temp = NULL;
	rtk_prof_info *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_prof_info, list);
		if (desc->psm == psm)
			return desc;
	}

	return NULL;
}

static void rtk_check_setup_timer(int8_t profile_index)
{
	if (profile_index == profile_a2dp) {
		btrtl_coex.a2dp_packet_count = 0;
		btrtl_coex.a2dp_count_timer.expires =
		    jiffies + msecs_to_jiffies(1000);
		mod_timer(&btrtl_coex.a2dp_count_timer,
			  btrtl_coex.a2dp_count_timer.expires);
	}

	if (profile_index == profile_pan) {
		btrtl_coex.pan_packet_count = 0;
		btrtl_coex.pan_count_timer.expires =
		    jiffies + msecs_to_jiffies(1000);
		mod_timer(&btrtl_coex.pan_count_timer,
			  btrtl_coex.pan_count_timer.expires);
	}

	/* hogp & voice share one timer now */
	if ((profile_index == profile_hogp) || (profile_index == profile_voice)) {
		if ((0 == btrtl_coex.profile_refcount[profile_hogp])
		    && (0 == btrtl_coex.profile_refcount[profile_voice])) {
			btrtl_coex.hogp_packet_count = 0;
			btrtl_coex.voice_packet_count = 0;
			btrtl_coex.hogp_count_timer.expires =
			    jiffies + msecs_to_jiffies(1000);
			mod_timer(&btrtl_coex.hogp_count_timer,
				  btrtl_coex.hogp_count_timer.expires);
		}
	}
}

static void rtk_check_del_timer(int8_t profile_index)
{
	if (profile_a2dp == profile_index) {
		btrtl_coex.a2dp_packet_count = 0;
		del_timer_sync(&btrtl_coex.a2dp_count_timer);
	}
	if (profile_pan == profile_index) {
		btrtl_coex.pan_packet_count = 0;
		del_timer_sync(&btrtl_coex.pan_count_timer);
	}
	if (profile_hogp == profile_index) {
		btrtl_coex.hogp_packet_count = 0;
		if (btrtl_coex.profile_refcount[profile_voice] == 0) {
			del_timer_sync(&btrtl_coex.hogp_count_timer);
		}
	}
	if (profile_voice == profile_index) {
		btrtl_coex.voice_packet_count = 0;
		if (btrtl_coex.profile_refcount[profile_hogp] == 0) {
			del_timer_sync(&btrtl_coex.hogp_count_timer);
		}
	}
}



static rtk_conn_prof *find_connection_by_handle(struct rtl_coex_struct * coex,
						uint16_t handle)
{
	struct list_head *head = &coex->conn_hash;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_conn_prof *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_conn_prof, list);
		if ((handle & 0xEFF) == desc->handle) {
			return desc;
		}
	}
	return NULL;
}

static rtk_conn_prof *allocate_connection_by_handle(uint16_t handle)
{
	rtk_conn_prof *phci_conn = NULL;
	phci_conn = kmalloc(sizeof(rtk_conn_prof), GFP_ATOMIC);
	if (phci_conn)
		phci_conn->handle = handle;

	return phci_conn;
}

static void init_connection_hash(struct rtl_coex_struct * coex)
{
	struct list_head *head = &coex->conn_hash;
	INIT_LIST_HEAD(head);
}

static void add_connection_to_hash(struct rtl_coex_struct * coex,
				   rtk_conn_prof * desc)
{
	struct list_head *head = &coex->conn_hash;
	list_add_tail(&desc->list, head);
}

static void delete_connection_from_hash(rtk_conn_prof * desc)
{
	if (desc) {
		list_del(&desc->list);
		kfree(desc);
	}
}

static void flush_connection_hash(struct rtl_coex_struct * coex)
{
	struct list_head *head = &coex->conn_hash;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_conn_prof *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_conn_prof, list);
		if (desc) {
			list_del(&desc->list);
			kfree(desc);
		}
	}
	//INIT_LIST_HEAD(head);
}

static void init_profile_hash(struct rtl_coex_struct * coex)
{
	struct list_head *head = &coex->profile_list;
	INIT_LIST_HEAD(head);
}

static uint8_t list_allocate_add(uint16_t handle, uint16_t psm,
				 int8_t profile_index, uint16_t dcid,
				 uint16_t scid)
{
	rtk_prof_info *pprof_info = NULL;

	if (profile_index < 0) {
		RTKBT_ERR("PSM 0x%x do not need parse", psm);
		return FALSE;
	}

	pprof_info = kmalloc(sizeof(rtk_prof_info), GFP_ATOMIC);

	if (NULL == pprof_info) {
		RTKBT_ERR("list_allocate_add: allocate error");
		return FALSE;
	}

	/* Check if it is the second l2cap connection for a2dp
	 * a2dp signal channel will be created first than media channel.
	 */
	if (psm == PSM_AVDTP) {
		rtk_prof_info *pinfo = find_by_psm(psm);
		if (!pinfo) {
			pprof_info->flags = A2DP_SIGNAL;
			RTKBT_INFO("%s: Add a2dp signal channel", __func__);
		} else {
			pprof_info->flags = A2DP_MEDIA;
			RTKBT_INFO("%s: Add a2dp media channel", __func__);
		}
	}

	pprof_info->handle = handle;
	pprof_info->psm = psm;
	pprof_info->scid = scid;
	pprof_info->dcid = dcid;
	pprof_info->profile_index = profile_index;
	list_add_tail(&(pprof_info->list), &(btrtl_coex.profile_list));

	return TRUE;
}

static void delete_profile_from_hash(rtk_prof_info * desc)
{
	RTKBT_DBG("Delete profile: hndl 0x%04x, psm 0x%04x, dcid 0x%04x, "
		  "scid 0x%04x", desc->handle, desc->psm, desc->dcid,
		  desc->scid);
	if (desc) {
		list_del(&desc->list);
		kfree(desc);
		desc = NULL;
	}
}

static void flush_profile_hash(struct rtl_coex_struct * coex)
{
	struct list_head *head = &coex->profile_list;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_prof_info *desc = NULL;

	spin_lock(&btrtl_coex.spin_lock_profile);
	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_prof_info, list);
		delete_profile_from_hash(desc);
	}
	//INIT_LIST_HEAD(head);
	spin_unlock(&btrtl_coex.spin_lock_profile);
}

static rtk_prof_info *find_profile_by_handle_scid(struct rtl_coex_struct *
						  coex, uint16_t handle,
						  uint16_t scid)
{
	struct list_head *head = &coex->profile_list;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_prof_info *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_prof_info, list);
		if (((handle & 0xFFF) == desc->handle) && (scid == desc->scid)) {
			return desc;
		}
	}
	return NULL;
}

static rtk_prof_info *find_profile_by_handle_dcid(struct rtl_coex_struct *
						  coex, uint16_t handle,
						  uint16_t dcid)
{
	struct list_head *head = &coex->profile_list;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_prof_info *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_prof_info, list);
		if (((handle & 0xFFF) == desc->handle) && (dcid == desc->dcid)) {
			return desc;
		}
	}
	return NULL;
}

static rtk_prof_info *find_profile_by_handle_dcid_scid(struct rtl_coex_struct
						       * coex, uint16_t handle,
						       uint16_t dcid,
						       uint16_t scid)
{
	struct list_head *head = &coex->profile_list;
	struct list_head *iter = NULL, *temp = NULL;
	rtk_prof_info *desc = NULL;

	list_for_each_safe(iter, temp, head) {
		desc = list_entry(iter, rtk_prof_info, list);
		if (((handle & 0xFFF) == desc->handle) && (dcid == desc->dcid)
		    && (scid == desc->scid)) {
			return desc;
		}
	}
	return NULL;
}

static void rtk_vendor_cmd_to_fw(uint16_t opcode, uint8_t parameter_len,
				 uint8_t * parameter)
{
	int len = HCI_CMD_PREAMBLE_SIZE + parameter_len;
	uint8_t *p;
	struct sk_buff *skb;
	struct hci_dev *hdev = btrtl_coex.hdev;

	if (!hdev) {
		RTKBT_ERR("No HCI device");
		return;
	} else if (!test_bit(HCI_UP, &hdev->flags)) {
		RTKBT_WARN("HCI device is down");
		return;
	}

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb) {
		RTKBT_DBG("there is no room for cmd 0x%x", opcode);
		return;
	}

	p = (uint8_t *) skb_put(skb, HCI_CMD_PREAMBLE_SIZE);
	UINT16_TO_STREAM(p, opcode);
	*p++ = parameter_len;

	if (parameter_len)
		memcpy(skb_put(skb, parameter_len), parameter, parameter_len);

	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;

#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
#if HCI_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	bt_cb(skb)->opcode = opcode;
#else
	bt_cb(skb)->hci.opcode = opcode;
#endif
#endif

	/* Stand-alone HCI commands must be flagged as
	 * single-command requests.
	 */
#if HCI_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#if HCI_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	bt_cb(skb)->req.start = true;
#else

#if HCI_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
	bt_cb(skb)->hci.req_start = true;
#else

	bt_cb(skb)->hci.req_flags |= HCI_REQ_START;
#endif

#endif /* 4.4.0 */
#endif /* 3.10.0 */
	RTKBT_DBG("%s: opcode 0x%x", __func__, opcode);

	/* It is harmless if set skb->dev twice. The dev will be used in
	 * btusb_send_frame() after or equal to kernel/hci 3.13.0,
	 * the hdev will not come from skb->dev. */
#if HCI_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
	skb->dev = (void *)btrtl_coex.hdev;
#endif
	/* Put the skb to the global hdev->cmd_q */
	skb_queue_tail(&hdev->cmd_q, skb);

#if HCI_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	tasklet_schedule(&hdev->cmd_task);
#else
	queue_work(hdev->workqueue, &hdev->cmd_work);
#endif

	return;
}

static void rtk_notify_profileinfo_to_fw(void)
{
	struct list_head *head = NULL;
	struct list_head *iter = NULL;
	struct list_head *temp = NULL;
	rtk_conn_prof *hci_conn = NULL;
	uint8_t handle_number = 0;
	uint32_t buffer_size = 0;
	uint8_t *p_buf = NULL;
	uint8_t *p = NULL;

	head = &btrtl_coex.conn_hash;
	list_for_each_safe(iter, temp, head) {
		hci_conn = list_entry(iter, rtk_conn_prof, list);
		if (hci_conn && hci_conn->profile_bitmap)
			handle_number++;
	}

	buffer_size = 1 + handle_number * 3 + 1;

	p_buf = kmalloc(buffer_size, GFP_ATOMIC);

	if (NULL == p_buf) {
		RTKBT_ERR("%s: alloc error", __func__);
		return;
	}
	p = p_buf;

	RTKBT_DBG("%s: BufferSize %u", __func__, buffer_size);
	*p++ = handle_number;
	RTKBT_DBG("%s: NumberOfHandles %u", __func__, handle_number);
	head = &btrtl_coex.conn_hash;
	list_for_each(iter, head) {
		hci_conn = list_entry(iter, rtk_conn_prof, list);
		if (hci_conn && hci_conn->profile_bitmap) {
			UINT16_TO_STREAM(p, hci_conn->handle);
			RTKBT_DBG("%s: handle 0x%04x", __func__,
					hci_conn->handle);
			*p++ = hci_conn->profile_bitmap;
			RTKBT_DBG("%s: profile_bitmap 0x%02x", __func__,
					hci_conn->profile_bitmap);
			handle_number--;
		}
		if (0 == handle_number)
			break;
	}

	*p++ = btrtl_coex.profile_status;
	RTKBT_DBG("%s: profile_status 0x%02x", __func__,
			btrtl_coex.profile_status);

	rtk_vendor_cmd_to_fw(HCI_VENDOR_SET_PROFILE_REPORT_COMMAND, buffer_size,
			     p_buf);

	kfree(p_buf);
	return;
}

static void update_profile_state(uint8_t profile_index, uint8_t is_busy)
{
	uint8_t need_update = FALSE;

	if ((btrtl_coex.profile_bitmap & BIT(profile_index)) == 0) {
		RTKBT_ERR("%s: : ERROR!!! profile(Index: %x) does not exist",
				__func__, profile_index);
		return;
	}

	if (is_busy) {
		if ((btrtl_coex.profile_status & BIT(profile_index)) == 0) {
			need_update = TRUE;
			btrtl_coex.profile_status |= BIT(profile_index);
		}
	} else {
		if ((btrtl_coex.profile_status & BIT(profile_index)) > 0) {
			need_update = TRUE;
			btrtl_coex.profile_status &= ~(BIT(profile_index));
		}
	}

	if (need_update) {
		RTKBT_DBG("%s: btrtl_coex.profie_bitmap = %x",
				__func__, btrtl_coex.profile_bitmap);
		RTKBT_DBG("%s: btrtl_coex.profile_status = %x",
				__func__, btrtl_coex.profile_status);
		rtk_notify_profileinfo_to_fw();
	}
}

static void update_profile_connection(rtk_conn_prof * phci_conn,
				      int8_t profile_index, uint8_t is_add)
{
	uint8_t need_update = FALSE;
	uint8_t kk;

	RTKBT_DBG("%s: is_add %d, profile_index %x", __func__,
			is_add, profile_index);
	if (profile_index < 0)
		return;

	if (is_add) {
		if (btrtl_coex.profile_refcount[profile_index] == 0) {
			need_update = TRUE;
			btrtl_coex.profile_bitmap |= BIT(profile_index);

			/* SCO is always busy */
			if (profile_index == profile_sco)
				btrtl_coex.profile_status |=
				    BIT(profile_index);

			rtk_check_setup_timer(profile_index);
		}
		btrtl_coex.profile_refcount[profile_index]++;

		if (0 == phci_conn->profile_refcount[profile_index]) {
			need_update = TRUE;
			phci_conn->profile_bitmap |= BIT(profile_index);
		}
		phci_conn->profile_refcount[profile_index]++;
	} else {
		if (!btrtl_coex.profile_refcount[profile_index]) {
			RTKBT_WARN("profile %u refcount is already zero",
				   profile_index);
			return;
		}
		btrtl_coex.profile_refcount[profile_index]--;
		RTKBT_DBG("%s: btrtl_coex.profile_refcount[%x] = %x",
				__func__, profile_index,
				btrtl_coex.profile_refcount[profile_index]);
		if (btrtl_coex.profile_refcount[profile_index] == 0) {
			need_update = TRUE;
			btrtl_coex.profile_bitmap &= ~(BIT(profile_index));

			/* if profile does not exist, status is meaningless */
			btrtl_coex.profile_status &= ~(BIT(profile_index));
			rtk_check_del_timer(profile_index);
		}

		phci_conn->profile_refcount[profile_index]--;
		if (0 == phci_conn->profile_refcount[profile_index]) {
			need_update = TRUE;
			phci_conn->profile_bitmap &= ~(BIT(profile_index));

			/* clear profile_hid_interval if need */
			if ((profile_hid == profile_index)
			    && (phci_conn->
				profile_bitmap & (BIT(profile_hid_interval)))) {
				phci_conn->profile_bitmap &=
				    ~(BIT(profile_hid_interval));
				btrtl_coex.
				    profile_refcount[profile_hid_interval]--;
			}
		}
	}

	RTKBT_DBG("%s: btrtl_coex.profile_bitmap 0x%02x", __func__,
			btrtl_coex.profile_bitmap);
	for (kk = 0; kk < 8; kk++)
		RTKBT_DBG("%s: btrtl_coex.profile_refcount[%d] = %d",
				__func__, kk,
				btrtl_coex.profile_refcount[kk]);

	if (need_update)
		rtk_notify_profileinfo_to_fw();
}

static void update_hid_active_state(uint16_t handle, uint16_t interval)
{
	uint8_t need_update = 0;
	rtk_conn_prof *phci_conn =
	    find_connection_by_handle(&btrtl_coex, handle);

	if (phci_conn == NULL)
		return;

	RTKBT_DBG("%s: handle 0x%04x, interval %u", __func__, handle, interval);
	if (((phci_conn->profile_bitmap) & (BIT(profile_hid))) == 0) {
		RTKBT_DBG("HID not connected, nothing to be down");
		return;
	}

	if (interval < 60) {
		if ((phci_conn->profile_bitmap & (BIT(profile_hid_interval))) ==
		    0) {
			need_update = 1;
			phci_conn->profile_bitmap |= BIT(profile_hid_interval);

			btrtl_coex.profile_refcount[profile_hid_interval]++;
			if (btrtl_coex.
			    profile_refcount[profile_hid_interval] == 1)
				btrtl_coex.profile_status |=
				    BIT(profile_hid);
		}
	} else {
		if ((phci_conn->profile_bitmap & (BIT(profile_hid_interval)))) {
			need_update = 1;
			phci_conn->profile_bitmap &=
			    ~(BIT(profile_hid_interval));

			btrtl_coex.profile_refcount[profile_hid_interval]--;
			if (btrtl_coex.
			    profile_refcount[profile_hid_interval] == 0)
				btrtl_coex.profile_status &=
				    ~(BIT(profile_hid));
		}
	}

	if (need_update)
		rtk_notify_profileinfo_to_fw();
}

static uint8_t handle_l2cap_con_req(uint16_t handle, uint16_t psm,
				    uint16_t scid, uint8_t direction)
{
	uint8_t status = FALSE;
	rtk_prof_info *prof_info = NULL;
	int8_t profile_index = psm_to_profile_index(psm);

	if (profile_index < 0) {
		RTKBT_DBG("PSM(0x%04x) do not need parse", psm);
		return status;
	}

	spin_lock(&btrtl_coex.spin_lock_profile);
	if (direction)		//1: out
		prof_info =
		    find_profile_by_handle_scid(&btrtl_coex, handle, scid);
	else			// 0:in
		prof_info =
		    find_profile_by_handle_dcid(&btrtl_coex, handle, scid);

	if (prof_info) {
		RTKBT_DBG("%s: this profile is already exist!", __func__);
		spin_unlock(&btrtl_coex.spin_lock_profile);
		return status;
	}

	if (direction)		//1: out
		status = list_allocate_add(handle, psm, profile_index, 0, scid);
	else			// 0:in
		status = list_allocate_add(handle, psm, profile_index, scid, 0);

	spin_unlock(&btrtl_coex.spin_lock_profile);

	if (!status)
		RTKBT_ERR("%s: list_allocate_add failed!", __func__);

	return status;
}

static uint8_t handle_l2cap_con_rsp(uint16_t handle, uint16_t dcid,
				    uint16_t scid, uint8_t direction,
				    uint8_t result)
{
	rtk_prof_info *prof_info = NULL;
	rtk_conn_prof *phci_conn = NULL;

	spin_lock(&btrtl_coex.spin_lock_profile);
	if (!direction)		//0, in
		prof_info =
		    find_profile_by_handle_scid(&btrtl_coex, handle, scid);
	else			//1, out
		prof_info =
		    find_profile_by_handle_dcid(&btrtl_coex, handle, scid);

	if (!prof_info) {
		//RTKBT_DBG("handle_l2cap_con_rsp: prof_info Not Find!!");
		spin_unlock(&btrtl_coex.spin_lock_profile);
		return FALSE;
	}

	if (!result) {		//success
		RTKBT_DBG("l2cap connection success, update connection");
		if (!direction)	//0, in
			prof_info->dcid = dcid;
		else		//1, out
			prof_info->scid = dcid;

		phci_conn = find_connection_by_handle(&btrtl_coex, handle);
		if (phci_conn)
			update_profile_connection(phci_conn,
						  prof_info->profile_index,
						  TRUE);
	}

	spin_unlock(&btrtl_coex.spin_lock_profile);
	return TRUE;
}

static uint8_t handle_l2cap_discon_req(uint16_t handle, uint16_t dcid,
				       uint16_t scid, uint8_t direction)
{
	rtk_prof_info *prof_info = NULL;
	rtk_conn_prof *phci_conn = NULL;
	RTKBT_DBG("%s: handle 0x%04x, dcid 0x%04x, scid 0x%04x, dir %u",
			__func__, handle, dcid, scid, direction);

	spin_lock(&btrtl_coex.spin_lock_profile);
	if (!direction)		//0: in
		prof_info =
		    find_profile_by_handle_dcid_scid(&btrtl_coex, handle,
						     scid, dcid);
	else			//1: out
		prof_info =
		    find_profile_by_handle_dcid_scid(&btrtl_coex, handle,
						     dcid, scid);

	if (!prof_info) {
		//LogMsg("handle_l2cap_discon_req: prof_info Not Find!");
		spin_unlock(&btrtl_coex.spin_lock_profile);
		return 0;
	}

	phci_conn = find_connection_by_handle(&btrtl_coex, handle);
	if (!phci_conn) {
		spin_unlock(&btrtl_coex.spin_lock_profile);
		return 0;
	}

	update_profile_connection(phci_conn, prof_info->profile_index, FALSE);
	if (prof_info->profile_index == profile_a2dp &&
	    (phci_conn->profile_bitmap & BIT(profile_sink)))
		update_profile_connection(phci_conn, profile_sink, FALSE);

	delete_profile_from_hash(prof_info);
	spin_unlock(&btrtl_coex.spin_lock_profile);

	return 1;
}

static const char sample_freqs[4][8] = {
	"16", "32", "44.1", "48"
};

static const uint8_t sbc_blocks[4] = { 4, 8, 12, 16 };

static const char chan_modes[4][16] = {
	"MONO", "DUAL_CHANNEL", "STEREO", "JOINT_STEREO"
};

static const char alloc_methods[2][12] = {
	"LOUDNESS", "SNR"
};

static const uint8_t subbands[2] = { 4, 8 };

void print_sbc_header(struct sbc_frame_hdr *hdr)
{
	RTKBT_DBG("syncword: %02x", hdr->syncword);
	RTKBT_DBG("freq %skHz", sample_freqs[hdr->sampling_frequency]);
	RTKBT_DBG("blocks %u", sbc_blocks[hdr->blocks]);
	RTKBT_DBG("channel mode %s", chan_modes[hdr->channel_mode]);
	RTKBT_DBG("allocation method %s",
		  alloc_methods[hdr->allocation_method]);
	RTKBT_DBG("subbands %u", subbands[hdr->subbands]);
}

static void packets_count(uint16_t handle, uint16_t scid, uint16_t length,
			  uint8_t direction, u8 *user_data)
{
	rtk_prof_info *prof_info = NULL;

	rtk_conn_prof *hci_conn =
	    find_connection_by_handle(&btrtl_coex, handle);
	if (NULL == hci_conn)
		return;

	if (0 == hci_conn->type) {
		if (!direction)	//0: in
			prof_info =
			    find_profile_by_handle_scid(&btrtl_coex, handle,
							scid);
		else		//1: out
			prof_info =
			    find_profile_by_handle_dcid(&btrtl_coex, handle,
							scid);

		if (!prof_info) {
			//RTKBT_DBG("packets_count: prof_info Not Find!");
			return;
		}

		/* avdtp media data */
		if (prof_info->profile_index == profile_a2dp &&
		    prof_info->flags == A2DP_MEDIA) {
			if (!is_profile_busy(profile_a2dp)) {
				struct sbc_frame_hdr *sbc_header;
				struct rtp_header *rtph;
				u8 bitpool;

				update_profile_state(profile_a2dp, TRUE);
				if (!direction) {
					if (!(hci_conn->profile_bitmap & BIT(profile_sink))) {
						btrtl_coex.profile_bitmap |= BIT(profile_sink);
						hci_conn->profile_bitmap |= BIT(profile_sink);
						update_profile_connection(hci_conn, profile_sink, 1);
					}
					update_profile_state(profile_sink, TRUE);
				}

				/* We assume it is SBC if the packet length
				 * is bigger than 100 bytes
				 */
				if (length > 100) {
					RTKBT_INFO("Length %u", length);
					rtph = (struct rtp_header *)user_data;

					RTKBT_DBG("rtp: v %u, cc %u, pt %u",
						  rtph->v, rtph->cc, rtph->pt);
					/* move forward */
					user_data += sizeof(struct rtp_header) +
						rtph->cc * 4 + 1;

					/* point to the sbc frame header */
					sbc_header = (struct sbc_frame_hdr *)user_data;
					bitpool = sbc_header->bitpool;

					print_sbc_header(sbc_header);

					RTKBT_DBG("bitpool %u", bitpool);

					rtk_vendor_cmd_to_fw(HCI_VENDOR_SET_BITPOOL,
							1, &bitpool);
				}
			}
			btrtl_coex.a2dp_packet_count++;
		}

		if (prof_info->profile_index == profile_pan)
			btrtl_coex.pan_packet_count++;
	}
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0)
static void count_a2dp_packet_timeout(struct timer_list *unused)
#else
static void count_a2dp_packet_timeout(unsigned long data)
#endif
{
	if (btrtl_coex.a2dp_packet_count)
		RTKBT_DBG("%s: a2dp_packet_count %d", __func__,
			  btrtl_coex.a2dp_packet_count);
	if (btrtl_coex.a2dp_packet_count == 0) {
		if (is_profile_busy(profile_a2dp)) {
			RTKBT_DBG("%s: a2dp busy->idle!", __func__);
			update_profile_state(profile_a2dp, FALSE);
			if (btrtl_coex.profile_bitmap & BIT(profile_sink))
				update_profile_state(profile_sink, FALSE);
		}
	}
	btrtl_coex.a2dp_packet_count = 0;
	mod_timer(&btrtl_coex.a2dp_count_timer,
		  jiffies + msecs_to_jiffies(1000));
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0)
static void count_pan_packet_timeout(struct timer_list *unused)
#else
static void count_pan_packet_timeout(unsigned long data)
#endif
{
	if (btrtl_coex.pan_packet_count)
		RTKBT_DBG("%s: pan_packet_count %d", __func__,
			  btrtl_coex.pan_packet_count);
	if (btrtl_coex.pan_packet_count < PAN_PACKET_COUNT) {
		if (is_profile_busy(profile_pan)) {
			RTKBT_DBG("%s: pan busy->idle!", __func__);
			update_profile_state(profile_pan, FALSE);
		}
	} else {
		if (!is_profile_busy(profile_pan)) {
			RTKBT_DBG("timeout_handler: pan idle->busy!");
			update_profile_state(profile_pan, TRUE);
		}
	}
	btrtl_coex.pan_packet_count = 0;
	mod_timer(&btrtl_coex.pan_count_timer,
		  jiffies + msecs_to_jiffies(1000));
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0)
static void count_hogp_packet_timeout(struct timer_list *unused)
#else
static void count_hogp_packet_timeout(unsigned long data)
#endif
{
	if (btrtl_coex.hogp_packet_count)
		RTKBT_DBG("%s: hogp_packet_count %d", __func__,
			  btrtl_coex.hogp_packet_count);
	if (btrtl_coex.hogp_packet_count == 0) {
		if (is_profile_busy(profile_hogp)) {
			RTKBT_DBG("%s: hogp busy->idle!", __func__);
			update_profile_state(profile_hogp, FALSE);
		}
	}
	btrtl_coex.hogp_packet_count = 0;

	if (btrtl_coex.voice_packet_count)
		RTKBT_DBG("%s: voice_packet_count %d", __func__,
			  btrtl_coex.voice_packet_count);
	if (btrtl_coex.voice_packet_count == 0) {
		if (is_profile_busy(profile_voice)) {
			RTKBT_DBG("%s: voice busy->idle!", __func__);
			update_profile_state(profile_voice, FALSE);
		}
	}
	btrtl_coex.voice_packet_count = 0;
	mod_timer(&btrtl_coex.hogp_count_timer,
		  jiffies + msecs_to_jiffies(1000));
}

#ifdef RTB_SOFTWARE_MAILBOX

#ifndef RTK_COEX_OVER_SYMBOL
static int udpsocket_send(char *tx_msg, int msg_size)
{
	u8 error = 0;
	struct msghdr udpmsg;
	mm_segment_t oldfs;
	struct iovec iov;

	RTKBT_DBG("send msg %s with len:%d", tx_msg, msg_size);

	if (btrtl_coex.sock_open) {
		iov.iov_base = (void *)tx_msg;
		iov.iov_len = msg_size;
		udpmsg.msg_name = &btrtl_coex.wifi_addr;
		udpmsg.msg_namelen = sizeof(struct sockaddr_in);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
		udpmsg.msg_iov = &iov;
		udpmsg.msg_iovlen = 1;
#else
		iov_iter_init(&udpmsg.msg_iter, WRITE, &iov, 1, msg_size);
#endif
		udpmsg.msg_control = NULL;
		udpmsg.msg_controllen = 0;
		udpmsg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		oldfs = get_fs();
		set_fs(KERNEL_DS);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
		error = sock_sendmsg(btrtl_coex.udpsock, &udpmsg, msg_size);
#else
		error = sock_sendmsg(btrtl_coex.udpsock, &udpmsg);
#endif
		set_fs(oldfs);

		if (error < 0)
			RTKBT_DBG("Error when sendimg msg, error:%d", error);
	}

	return error;
}
#endif

#ifdef RTK_COEX_OVER_SYMBOL
/* Receive message from WiFi */
u8 rtw_btcoex_wifi_to_bt(u8 *msg, u8 msg_size)
{
	struct sk_buff *nskb;

	if (!rtw_coex_on) {
		RTKBT_WARN("Bluetooth is closed");
		return 0;
	}

	nskb = alloc_skb(msg_size, GFP_ATOMIC);
	if (!nskb) {
		RTKBT_ERR("Couldnt alloc skb for WiFi coex message");
		return 0;
	}

	memcpy(skb_put(nskb, msg_size), msg, msg_size);
	skb_queue_tail(&rtw_q, nskb);

	queue_work(rtw_wq, &rtw_work);

	return 1;
}
EXPORT_SYMBOL(rtw_btcoex_wifi_to_bt);

static int rtk_send_coexmsg2wifi(u8 *msg, u8 size)
{
	u8 result;
	u8 (*btmsg_to_wifi)(u8 *, u8);

	btmsg_to_wifi = __symbol_get(VMLINUX_SYMBOL_STR(rtw_btcoex_bt_to_wifi));

	if (!btmsg_to_wifi) {
		/* RTKBT_ERR("Couldnt get symbol"); */
		return -1;
	}

	result = btmsg_to_wifi(msg, size);
	__symbol_put(VMLINUX_SYMBOL_STR(rtw_btcoex_bt_to_wifi));
	if (!result) {
		RTKBT_ERR("Couldnt send coex msg to WiFi");
		return -1;
	} else if (result == 1){
		/* successful to send message */
		return 0;
	} else {
		RTKBT_ERR("Unknown result %d", result);
		return -1;
	}
}

static int rtkbt_process_coexskb(struct sk_buff *skb)
{
	rtk_handle_event_from_wifi(skb->data);
	return 0;
}

static void rtw_work_func(struct work_struct *work)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&rtw_q))) {
		rtkbt_process_coexskb(skb);
		kfree_skb(skb);
	}
}

#endif

static int rtkbt_coexmsg_send(char *tx_msg, int msg_size)
{
#ifdef RTK_COEX_OVER_SYMBOL
	return rtk_send_coexmsg2wifi((uint8_t *)tx_msg, (u8)msg_size);
#else
	return udpsocket_send(tx_msg, msg_size);
#endif
}

#ifndef RTK_COEX_OVER_SYMBOL
static void udpsocket_recv_data(void)
{
	u8 recv_data[512];
	u32 len = 0;
	u16 recv_length;
	struct sk_buff *skb;

	RTKBT_DBG("-");

	spin_lock(&btrtl_coex.spin_lock_sock);
	len = skb_queue_len(&btrtl_coex.sk->sk_receive_queue);

	while (len > 0) {
		skb = skb_dequeue(&btrtl_coex.sk->sk_receive_queue);

		/*important: cut the udp header from skb->data! header length is 8 byte */
		recv_length = skb->len - 8;
		memset(recv_data, 0, sizeof(recv_data));
		memcpy(recv_data, skb->data + 8, recv_length);
		//RTKBT_DBG("received data: %s :with len %u", recv_data, recv_length);

		rtk_handle_event_from_wifi(recv_data);

		len--;
		kfree_skb(skb);
	}

	spin_unlock(&btrtl_coex.spin_lock_sock);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
static void udpsocket_recv(struct sock *sk, int bytes)
#else
static void udpsocket_recv(struct sock *sk)
#endif
{
	spin_lock(&btrtl_coex.spin_lock_sock);
	btrtl_coex.sk = sk;
	spin_unlock(&btrtl_coex.spin_lock_sock);
	queue_delayed_work(btrtl_coex.sock_wq, &btrtl_coex.sock_work, 0);
}

static void create_udpsocket(void)
{
	int err;
	RTKBT_DBG("%s: connect_port: %d", __func__, CONNECT_PORT);
	btrtl_coex.sock_open = 0;

	err = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
			&btrtl_coex.udpsock);
	if (err < 0) {
		RTKBT_ERR("%s: sock create error, err = %d", __func__, err);
		return;
	}

	memset(&btrtl_coex.addr, 0, sizeof(struct sockaddr_in));
	btrtl_coex.addr.sin_family = AF_INET;
	btrtl_coex.addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	btrtl_coex.addr.sin_port = htons(CONNECT_PORT);

	memset(&btrtl_coex.wifi_addr, 0, sizeof(struct sockaddr_in));
	btrtl_coex.wifi_addr.sin_family = AF_INET;
	btrtl_coex.wifi_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	btrtl_coex.wifi_addr.sin_port = htons(CONNECT_PORT_WIFI);

	err =
	    btrtl_coex.udpsock->ops->bind(btrtl_coex.udpsock,
					     (struct sockaddr *)&btrtl_coex.
					     addr, sizeof(struct sockaddr));
	if (err < 0) {
		sock_release(btrtl_coex.udpsock);
		RTKBT_ERR("%s: sock bind error, err = %d",__func__,  err);
		return;
	}

	btrtl_coex.sock_open = 1;
	btrtl_coex.udpsock->sk->sk_data_ready = udpsocket_recv;
}
#endif /* !RTK_COEX_OVER_SYMBOL */

static void rtk_notify_extension_version_to_wifi(void)
{
	uint8_t para_length = 2;
	char p_buf[2 + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_EXTENSION_VERSION_NOTIFY);
	*p++ = para_length;
	UINT16_TO_STREAM(p, HCI_EXTENSION_VERSION);
	RTKBT_DBG("extension version is 0x%x", HCI_EXTENSION_VERSION);
	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_btpatch_version_to_wifi(void)
{
	uint8_t para_length = 4;
	char p_buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_BT_PATCH_VER_NOTIFY);
	*p++ = para_length;
	UINT16_TO_STREAM(p, btrtl_coex.hci_reversion);
	UINT16_TO_STREAM(p, btrtl_coex.lmp_subversion);
	RTKBT_DBG("btpatch ver: len %u, hci_rev 0x%04x, lmp_subver 0x%04x",
			para_length, btrtl_coex.hci_reversion,
			btrtl_coex.lmp_subversion);

	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_afhmap_to_wifi(void)
{
	uint8_t para_length = 13;
	char p_buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;
	uint8_t kk = 0;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_BT_AFH_MAP_NOTIFY);
	*p++ = para_length;
	*p++ = btrtl_coex.piconet_id;
	*p++ = btrtl_coex.mode;
	*p++ = 10;
	memcpy(p, btrtl_coex.afh_map, 10);

	RTKBT_DBG("afhmap, piconet_id is 0x%x, map type is 0x%x",
		  btrtl_coex.piconet_id, btrtl_coex.mode);
	for (kk = 0; kk < 10; kk++)
		RTKBT_DBG("afhmap data[%d] is 0x%x", kk,
			  btrtl_coex.afh_map[kk]);

	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_btcoex_to_wifi(uint8_t opcode, uint8_t status)
{
	uint8_t para_length = 2;
	char p_buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_BT_COEX_NOTIFY);
	*p++ = para_length;
	*p++ = opcode;
	if (!status)
		*p++ = 0;
	else
		*p++ = 1;

	RTKBT_DBG("btcoex, opcode is 0x%x, status is 0x%x", opcode, status);

	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_btoperation_to_wifi(uint8_t operation,
					   uint8_t append_data_length,
					   uint8_t * append_data)
{
	uint8_t para_length = 3 + append_data_length;
	char p_buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;
	uint8_t kk = 0;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_BT_OPERATION_NOTIFY);
	*p++ = para_length;
	*p++ = operation;
	*p++ = append_data_length;
	if (append_data_length)
		memcpy(p, append_data, append_data_length);

	RTKBT_DBG("btoperation: op 0x%02x, append_data_length %u",
		  operation, append_data_length);
	if (append_data_length) {
		for (kk = 0; kk < append_data_length; kk++)
			RTKBT_DBG("append data is 0x%x", *(append_data + kk));
	}

	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_info_to_wifi(uint8_t reason, uint8_t length,
				    uint8_t *report_info)
{
	uint8_t para_length = 4 + length;
	char buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = buf;
	struct rtl_btinfo *report = (struct rtl_btinfo *)report_info;

	if (length) {
		RTKBT_DBG("bt info: cmd %2.2X", report->cmd);
		RTKBT_DBG("bt info: len %2.2X", report->len);
		RTKBT_DBG("bt info: data %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X",
			  report->data[0], report->data[1], report->data[2],
			  report->data[3], report->data[4], report->data[5]);
	}
	RTKBT_DBG("bt info: reason 0x%2x, length 0x%2x", reason, length);

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_BT_INFO_NOTIFY);
	*p++ = para_length;
	*p++ = btrtl_coex.polling_enable;
	*p++ = btrtl_coex.polling_interval;
	*p++ = reason;
	*p++ = length;

	if (length)
		memcpy(p, report_info, length);

	RTKBT_DBG("para length %2x, polling_enable %u, poiiling_interval %u",
	     para_length, btrtl_coex.polling_enable,
	     btrtl_coex.polling_interval);
	/* send BT INFO to Wi-Fi driver */
	if (rtkbt_coexmsg_send(buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

static void rtk_notify_regester_to_wifi(uint8_t * reg_value)
{
	uint8_t para_length = 9;
	char p_buf[para_length + HCI_CMD_PREAMBLE_SIZE];
	char *p = p_buf;
	hci_mailbox_register *reg = (hci_mailbox_register *) reg_value;

	if (!btrtl_coex.wifi_on)
		return;

	UINT16_TO_STREAM(p, HCI_OP_HCI_BT_REGISTER_VALUE_NOTIFY);
	*p++ = para_length;
	memcpy(p, reg_value, para_length);

	RTKBT_DBG("bt register, register type is %x", reg->type);
	RTKBT_DBG("bt register, register offset is %x", reg->offset);
	RTKBT_DBG("bt register, register value is %x", reg->value);

	if (rtkbt_coexmsg_send(p_buf, para_length + HCI_CMD_PREAMBLE_SIZE) < 0)
		RTKBT_ERR("%s: sock send error", __func__);
}

#endif

void rtk_btcoex_parse_cmd(uint8_t *buffer, int count)
{
	u16 opcode = (buffer[0]) + (buffer[1] << 8);

	if (!test_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		RTKBT_INFO("%s: Coex is closed, ignore", __func__);
		return;
	}

	switch (opcode) {
	case HCI_OP_INQUIRY:
	case HCI_OP_PERIODIC_INQ:
		if (!btrtl_coex.isinquirying) {
			btrtl_coex.isinquirying = 1;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci (periodic)inq, notify wifi "
				  "inquiry start");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_INQUIRY_START,
						       0, NULL);
#else
			RTKBT_INFO("hci (periodic)inq start");
#endif
		}
		break;
	case HCI_OP_INQUIRY_CANCEL:
	case HCI_OP_EXIT_PERIODIC_INQ:
		if (btrtl_coex.isinquirying) {
			btrtl_coex.isinquirying = 0;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci (periodic)inq cancel/exit, notify wifi "
				  "inquiry stop");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_INQUIRY_END, 0,
						       NULL);
#else
			RTKBT_INFO("hci (periodic)inq cancel/exit");
#endif
		}
		break;
	case HCI_OP_ACCEPT_CONN_REQ:
		if (!btrtl_coex.ispaging) {
			btrtl_coex.ispaging = 1;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci accept connreq, notify wifi page start");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_PAGE_START, 0,
						       NULL);
#else
			RTKBT_INFO("hci accept conn req");
#endif
		}
		break;
	case HCI_OP_DISCONNECT:
		RTKBT_INFO("HCI Disconnect, handle %04x, reason 0x%02x",
			   ((u16)buffer[4] << 8 | buffer[3]), buffer[5]);
		break;
	default:
		break;
	}
}

static void rtk_handle_inquiry_complete(void)
{
	if (btrtl_coex.isinquirying) {
		btrtl_coex.isinquirying = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("inq complete, notify wifi inquiry end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_INQUIRY_END, 0, NULL);
#else
		RTKBT_INFO("inquiry complete");
#endif
	}
}

static void rtk_handle_pin_code_req(void)
{
	if (!btrtl_coex.ispairing) {
		btrtl_coex.ispairing = 1;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("pin code req, notify wifi pair start");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAIR_START, 0, NULL);
#else
		RTKBT_INFO("pin code request");
#endif
	}
}

static void rtk_handle_io_capa_req(void)
{
	if (!btrtl_coex.ispairing) {
		btrtl_coex.ispairing = 1;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("io cap req, notify wifi pair start");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAIR_START, 0, NULL);
#else
		RTKBT_INFO("io capability request");
#endif
	}
}

static void rtk_handle_auth_request(void)
{
	if (btrtl_coex.ispairing) {
		btrtl_coex.ispairing = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("auth req, notify wifi pair end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAIR_END, 0, NULL);
#else
		RTKBT_INFO("authentication request");
#endif
	}
}

static void rtk_handle_link_key_notify(void)
{
	if (btrtl_coex.ispairing) {
		btrtl_coex.ispairing = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("link key notify, notify wifi pair end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAIR_END, 0, NULL);
#else
		RTKBT_INFO("link key notify");
#endif
	}
}

static void rtk_handle_mode_change_evt(u8 * p)
{
	u16 mode_change_handle, mode_interval;

	p++;
	STREAM_TO_UINT16(mode_change_handle, p);
	p++;
	STREAM_TO_UINT16(mode_interval, p);
	update_hid_active_state(mode_change_handle, mode_interval);
}

#ifdef RTB_SOFTWARE_MAILBOX
static void rtk_parse_vendor_mailbox_cmd_evt(u8 * p, u8 total_len)
{
	u8 status, subcmd;
	u8 temp_cmd[10];

	status = *p++;
	if (total_len <= 4) {
		RTKBT_DBG("receive mailbox cmd from fw, total length <= 4");
		return;
	}
	subcmd = *p++;
	RTKBT_DBG("receive mailbox cmd from fw, subcmd is 0x%x, status is 0x%x",
		  subcmd, status);

	switch (subcmd) {
	case HCI_VENDOR_SUB_CMD_BT_REPORT_CONN_SCO_INQ_INFO:
		if (status == 0)	//success
			rtk_notify_info_to_wifi(POLLING_RESPONSE,
					RTL_BTINFO_LEN, (uint8_t *)p);
		break;

	case HCI_VENDOR_SUB_CMD_WIFI_CHANNEL_AND_BANDWIDTH_CMD:
		rtk_notify_btcoex_to_wifi(WIFI_BW_CHNL_NOTIFY, status);
		break;

	case HCI_VENDOR_SUB_CMD_WIFI_FORCE_TX_POWER_CMD:
		rtk_notify_btcoex_to_wifi(BT_POWER_DECREASE_CONTROL, status);
		break;

	case HCI_VENDOR_SUB_CMD_BT_ENABLE_IGNORE_WLAN_ACT_CMD:
		rtk_notify_btcoex_to_wifi(IGNORE_WLAN_ACTIVE_CONTROL, status);
		break;

	case HCI_VENDOR_SUB_CMD_SET_BT_PSD_MODE:
		rtk_notify_btcoex_to_wifi(BT_PSD_MODE_CONTROL, status);
		break;

	case HCI_VENDOR_SUB_CMD_SET_BT_LNA_CONSTRAINT:
		rtk_notify_btcoex_to_wifi(LNA_CONSTRAIN_CONTROL, status);
		break;

	case HCI_VENDOR_SUB_CMD_BT_AUTO_REPORT_ENABLE:
		break;

	case HCI_VENDOR_SUB_CMD_BT_SET_TXRETRY_REPORT_PARAM:
		break;

	case HCI_VENDOR_SUB_CMD_BT_SET_PTATABLE:
		break;

	case HCI_VENDOR_SUB_CMD_GET_AFH_MAP_L:
		if (status == 0) {
			memcpy(btrtl_coex.afh_map, p + 4, 4);	/* cmd_idx, length, piconet_id, mode */
			temp_cmd[0] = HCI_VENDOR_SUB_CMD_GET_AFH_MAP_M;
			temp_cmd[1] = 2;
			temp_cmd[2] = btrtl_coex.piconet_id;
			temp_cmd[3] = btrtl_coex.mode;
			rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 4,
					     temp_cmd);
		} else {
			memset(btrtl_coex.afh_map, 0, 10);
			rtk_notify_afhmap_to_wifi();
		}
		break;

	case HCI_VENDOR_SUB_CMD_GET_AFH_MAP_M:
		if (status == 0) {
			memcpy(btrtl_coex.afh_map + 4, p + 4, 4);
			temp_cmd[0] = HCI_VENDOR_SUB_CMD_GET_AFH_MAP_H;
			temp_cmd[1] = 2;
			temp_cmd[2] = btrtl_coex.piconet_id;
			temp_cmd[3] = btrtl_coex.mode;
			rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 4,
					     temp_cmd);
		} else {
			memset(btrtl_coex.afh_map, 0, 10);
			rtk_notify_afhmap_to_wifi();
		}
		break;

	case HCI_VENDOR_SUB_CMD_GET_AFH_MAP_H:
		if (status == 0)
			memcpy(btrtl_coex.afh_map + 8, p + 4, 2);
		else
			memset(btrtl_coex.afh_map, 0, 10);

		rtk_notify_afhmap_to_wifi();
		break;

	case HCI_VENDOR_SUB_CMD_RD_REG_REQ:
		if (status == 0)
			rtk_notify_regester_to_wifi(p + 3);	/* cmd_idx,length,regist type */
		break;

	case HCI_VENDOR_SUB_CMD_WR_REG_REQ:
		rtk_notify_btcoex_to_wifi(BT_REGISTER_ACCESS, status);
		break;

	default:
		break;
	}
}
#endif /* RTB_SOFTWARE_MAILBOX */

static void rtk_handle_cmd_complete_evt(u8 total_len, u8 * p)
{
	u16 opcode;

	p++;
	STREAM_TO_UINT16(opcode, p);
	//RTKBT_DBG("cmd_complete, opcode is 0x%x", opcode);

	if (opcode == HCI_OP_PERIODIC_INQ) {
		if (*p++ && btrtl_coex.isinquirying) {
			btrtl_coex.isinquirying = 0;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci period inq, start error, notify wifi "
				  "inquiry stop");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_INQUIRY_END, 0,
						       NULL);
#else
			RTKBT_INFO("hci period inquiry start error");
#endif
		}
	}

	if (opcode == HCI_OP_READ_LOCAL_VERSION) {
		if (!(*p++)) {
			p++;
			STREAM_TO_UINT16(btrtl_coex.hci_reversion, p);
			p += 3;
			STREAM_TO_UINT16(btrtl_coex.lmp_subversion, p);
			RTKBT_DBG("BTCOEX hci_rev 0x%04x",
				  btrtl_coex.hci_reversion);
			RTKBT_DBG("BTCOEX lmp_subver 0x%04x",
				  btrtl_coex.lmp_subversion);
		}
	}

#ifdef RTB_SOFTWARE_MAILBOX
	if (opcode == HCI_VENDOR_MAILBOX_CMD) {
		rtk_parse_vendor_mailbox_cmd_evt(p, total_len);
	}
#endif
}

static void rtk_handle_cmd_status_evt(u8 * p)
{
	u16 opcode;
	u8 status;

	status = *p++;
	p++;
	STREAM_TO_UINT16(opcode, p);
	//RTKBT_DBG("cmd_status, opcode is 0x%x", opcode);
	if ((opcode == HCI_OP_INQUIRY) && (status)) {
		if (btrtl_coex.isinquirying) {
			btrtl_coex.isinquirying = 0;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci inq, start error, notify wifi inq stop");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_INQUIRY_END, 0,
						       NULL);
#else
			RTKBT_INFO("hci inquiry start error");
#endif
		}
	}

	if (opcode == HCI_OP_CREATE_CONN) {
		if (!status && !btrtl_coex.ispaging) {
			btrtl_coex.ispaging = 1;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("hci create conn, notify wifi start page");
			rtk_notify_btoperation_to_wifi(BT_OPCODE_PAGE_START, 0,
						       NULL);
#else
			RTKBT_INFO("hci create connection, start paging");
#endif
		}
	}
}

static void rtk_handle_connection_complete_evt(u8 * p)
{
	u16 handle;
	u8 status, link_type;
	rtk_conn_prof *hci_conn = NULL;

	status = *p++;
	STREAM_TO_UINT16(handle, p);
	p += 6;
	link_type = *p++;

	RTKBT_INFO("connected, handle %04x, status 0x%02x", handle, status);

	if (status == 0) {
		if (btrtl_coex.ispaging) {
			btrtl_coex.ispaging = 0;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("notify wifi page success end");
			rtk_notify_btoperation_to_wifi
			    (BT_OPCODE_PAGE_SUCCESS_END, 0, NULL);
#else
			RTKBT_INFO("Page success");
#endif
		}

		hci_conn = find_connection_by_handle(&btrtl_coex, handle);
		if (hci_conn == NULL) {
			hci_conn = allocate_connection_by_handle(handle);
			if (hci_conn) {
				add_connection_to_hash(&btrtl_coex,
						       hci_conn);
				hci_conn->profile_bitmap = 0;
				memset(hci_conn->profile_refcount, 0, 8);
				if ((0 == link_type) || (2 == link_type)) {	//sco or esco
					hci_conn->type = 1;
					update_profile_connection(hci_conn,
								  profile_sco,
								  TRUE);
				} else
					hci_conn->type = 0;
			} else {
				RTKBT_ERR("hci connection allocate fail");
			}
		} else {
			RTKBT_DBG("hci conn handle 0x%04x already existed!",
				  handle);
			hci_conn->profile_bitmap = 0;
			memset(hci_conn->profile_refcount, 0, 8);
			if ((0 == link_type) || (2 == link_type)) {	//sco or esco
				hci_conn->type = 1;
				update_profile_connection(hci_conn, profile_sco,
							  TRUE);
			} else
				hci_conn->type = 0;
		}
	} else if (btrtl_coex.ispaging) {
		btrtl_coex.ispaging = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("notify wifi page unsuccess end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAGE_UNSUCCESS_END, 0,
					       NULL);
#else
		RTKBT_INFO("Page failed");
#endif
	}
}

static void rtk_handle_le_connection_complete_evt(u8 enhanced, u8 * p)
{
	u16 handle, interval;
	u8 status;
	rtk_conn_prof *hci_conn = NULL;

	status = *p++;
	STREAM_TO_UINT16(handle, p);
	if (!enhanced)
		p += 8;	/* role, address type, address */
	else
		p += (8 + 12); /* plus two bluetooth addresses */
	STREAM_TO_UINT16(interval, p);

	RTKBT_INFO("LE connected, handle %04x, status 0x%02x, interval %u",
		   handle, status, interval);

	if (status == 0) {
		if (btrtl_coex.ispaging) {
			btrtl_coex.ispaging = 0;
#ifdef RTB_SOFTWARE_MAILBOX
			RTKBT_DBG("notify wifi page success end");
			rtk_notify_btoperation_to_wifi
			    (BT_OPCODE_PAGE_SUCCESS_END, 0, NULL);
#else
			RTKBT_INFO("Page success end");
#endif
		}

		hci_conn = find_connection_by_handle(&btrtl_coex, handle);
		if (hci_conn == NULL) {
			hci_conn = allocate_connection_by_handle(handle);
			if (hci_conn) {
				add_connection_to_hash(&btrtl_coex,
						       hci_conn);
				hci_conn->profile_bitmap = 0;
				memset(hci_conn->profile_refcount, 0, 8);
				hci_conn->type = 2;
				update_profile_connection(hci_conn, profile_hid, TRUE);	//for coex, le is the same as hid
				update_hid_active_state(handle, interval);
			} else {
				RTKBT_ERR("hci connection allocate fail");
			}
		} else {
			RTKBT_DBG("hci conn handle 0x%04x already existed!",
				  handle);
			hci_conn->profile_bitmap = 0;
			memset(hci_conn->profile_refcount, 0, 8);
			hci_conn->type = 2;
			update_profile_connection(hci_conn, profile_hid, TRUE);
			update_hid_active_state(handle, interval);
		}
	} else if (btrtl_coex.ispaging) {
		btrtl_coex.ispaging = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("notify wifi page unsuccess end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAGE_UNSUCCESS_END, 0,
					       NULL);
#else
		RTKBT_INFO("Page failed");
#endif
	}
}

static void rtk_handle_le_connection_update_complete_evt(u8 * p)
{
	u16 handle, interval;
	/* u8 status; */

	/* status = *p++; */
	p++;

	STREAM_TO_UINT16(handle, p);
	STREAM_TO_UINT16(interval, p);
	update_hid_active_state(handle, interval);
}

static void rtk_handle_le_meta_evt(u8 * p)
{
	u8 sub_event = *p++;
	switch (sub_event) {
	case HCI_EV_LE_CONN_COMPLETE:
		rtk_handle_le_connection_complete_evt(0, p);
		break;
	case HCI_EV_LE_ENHANCED_CONN_COMPLETE:
		rtk_handle_le_connection_complete_evt(1, p);
		break;

	case HCI_EV_LE_CONN_UPDATE_COMPLETE:
		rtk_handle_le_connection_update_complete_evt(p);
		break;

	default:
		break;
	}
}

static u8 disconn_profile(struct rtl_hci_conn *conn, u8 pfe_index)
{
	u8 need_update = 0;

	if (!btrtl_coex.profile_refcount[pfe_index]) {
		RTKBT_WARN("profile %u ref is 0", pfe_index);
		return 0;
	}

	btrtl_coex.profile_refcount[pfe_index]--;
	RTKBT_INFO("%s: profile_ref[%u] %u", __func__, pfe_index,
		  btrtl_coex.profile_refcount[pfe_index]);

	if (!btrtl_coex.profile_refcount[pfe_index]) {
		need_update = 1;
		btrtl_coex.profile_bitmap &= ~(BIT(pfe_index));

		/* if profile does not exist, status is meaningless */
		btrtl_coex.profile_status &= ~(BIT(pfe_index));
		rtk_check_del_timer(pfe_index);
	}

	if (conn->profile_refcount[pfe_index])
		conn->profile_refcount[pfe_index]--;
	else
		RTKBT_INFO("%s: conn pfe ref[%u] is 0", __func__,
			   conn->profile_refcount[pfe_index]);
	if (!conn->profile_refcount[pfe_index]) {
		need_update = 1;
		conn->profile_bitmap &= ~(BIT(pfe_index));

		/* clear profile_hid_interval if need */
		if ((profile_hid == pfe_index) &&
		    (conn->profile_bitmap & (BIT(profile_hid_interval)))) {
			conn->profile_bitmap &= ~(BIT(profile_hid_interval));
			if (btrtl_coex.profile_refcount[profile_hid_interval])
				btrtl_coex.profile_refcount[profile_hid_interval]--;
		}
	}

	return need_update;
}

static void disconn_acl(u16 handle, struct rtl_hci_conn *conn)
{
	struct rtl_coex_struct *coex = &btrtl_coex;
	rtk_prof_info *prof_info = NULL;
	struct list_head *iter = NULL, *temp = NULL;
	u8 need_update = 0;

	spin_lock(&coex->spin_lock_profile);

	list_for_each_safe(iter, temp, &coex->profile_list) {
		prof_info = list_entry(iter, rtk_prof_info, list);
		if (handle == prof_info->handle) {
			RTKBT_DBG("hci disconn, hndl %x, psm %x, dcid %x, "
				  "scid %x, profile %u", prof_info->handle,
				  prof_info->psm, prof_info->dcid,
				  prof_info->scid, prof_info->profile_index);
			//If both scid and dcid > 0, L2cap connection is exist.
			need_update |= disconn_profile(conn,
						      prof_info->profile_index);
			if ((prof_info->flags & A2DP_MEDIA) &&
			    (conn->profile_bitmap & BIT(profile_sink)))
				need_update |= disconn_profile(conn,
							       profile_sink);
			delete_profile_from_hash(prof_info);
		}
	}
	if (need_update)
		rtk_notify_profileinfo_to_fw();
	spin_unlock(&coex->spin_lock_profile);
}

static void rtk_handle_disconnect_complete_evt(u8 * p)
{
	u16 handle;
	u8 status;
	u8 reason;
	rtk_conn_prof *hci_conn = NULL;

	if (btrtl_coex.ispairing) {	//for slave: connection will be disconnected if authentication fail
		btrtl_coex.ispairing = 0;
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("hci disc complete, notify wifi pair end");
		rtk_notify_btoperation_to_wifi(BT_OPCODE_PAIR_END, 0, NULL);
#else
		RTKBT_INFO("hci disconnection complete");
#endif
	}

	status = *p++;
	STREAM_TO_UINT16(handle, p);
	reason = *p;

	RTKBT_INFO("disconn cmpl evt: status 0x%02x, handle %04x, reason 0x%02x",
		   status, handle, reason);

	if (status == 0) {
		RTKBT_DBG("process disconn complete event.");
		hci_conn = find_connection_by_handle(&btrtl_coex, handle);
		if (hci_conn) {
			switch (hci_conn->type) {
			case 0:
				/* FIXME: If this is interrupted by l2cap rx,
				 * there may be deadlock on spin_lock_profile */
				disconn_acl(handle, hci_conn);
				break;

			case 1:
				update_profile_connection(hci_conn, profile_sco,
							  FALSE);
				break;

			case 2:
				update_profile_connection(hci_conn, profile_hid,
							  FALSE);
				break;

			default:
				break;
			}
			delete_connection_from_hash(hci_conn);
		} else
			RTKBT_ERR("hci conn handle 0x%04x not found", handle);
	}
}

static void rtk_handle_specific_evt(u8 * p)
{
	u16 subcode;

	STREAM_TO_UINT16(subcode, p);
	if (subcode == HCI_VENDOR_PTA_AUTO_REPORT_EVENT) {
#ifdef RTB_SOFTWARE_MAILBOX
		RTKBT_DBG("notify wifi driver with autoreport data");
		rtk_notify_info_to_wifi(AUTO_REPORT, RTL_BTINFO_LEN,
			(uint8_t *)p);
#else
		RTKBT_INFO("auto report data");
#endif
	}
}

static void rtk_parse_event_data(struct rtl_coex_struct *coex,
		u8 *data, u16 len)
{
	u8 *p = data;
	u8 event_code = *p++;
	u8 total_len = *p++;

	(void)coex;
	(void)&len;

	switch (event_code) {
	case HCI_EV_INQUIRY_COMPLETE:
		rtk_handle_inquiry_complete();
		break;

	case HCI_EV_PIN_CODE_REQ:
		rtk_handle_pin_code_req();
		break;

	case HCI_EV_IO_CAPA_REQUEST:
		rtk_handle_io_capa_req();
		break;

	case HCI_EV_AUTH_COMPLETE:
		rtk_handle_auth_request();
		break;

	case HCI_EV_LINK_KEY_NOTIFY:
		rtk_handle_link_key_notify();
		break;

	case HCI_EV_MODE_CHANGE:
		rtk_handle_mode_change_evt(p);
		break;

	case HCI_EV_CMD_COMPLETE:
		rtk_handle_cmd_complete_evt(total_len, p);
		break;

	case HCI_EV_CMD_STATUS:
		rtk_handle_cmd_status_evt(p);
		break;

	case HCI_EV_CONN_COMPLETE:
	case HCI_EV_SYNC_CONN_COMPLETE:
		rtk_handle_connection_complete_evt(p);
		break;

	case HCI_EV_DISCONN_COMPLETE:
		rtk_handle_disconnect_complete_evt(p);
		break;

	case HCI_EV_LE_META:
		rtk_handle_le_meta_evt(p);
		break;

	case HCI_EV_VENDOR_SPECIFIC:
		rtk_handle_specific_evt(p);
		break;

	default:
		break;
	}
}

const char l2_dir_str[][4] = {
	"RX", "TX",
};

void rtl_process_l2_sig(struct rtl_l2_buff *l2)
{
	/* u8 flag; */
	u8 code;
	/* u8 identifier; */
	u16 handle;
	/* u16 total_len; */
	/* u16 pdu_len, channel_id; */
	/* u16 command_len; */
	u16 psm, scid, dcid, result;
	/* u16 status; */
	u8 *pp = l2->data;

	STREAM_TO_UINT16(handle, pp);
	/* flag = handle >> 12; */
	handle = handle & 0x0FFF;
	/* STREAM_TO_UINT16(total_len, pp); */
	pp += 2; /* data total length */

	/* STREAM_TO_UINT16(pdu_len, pp);
	 * STREAM_TO_UINT16(channel_id, pp); */
	pp += 4; /* l2 len and channel id */

	code = *pp++;
	switch (code) {
	case L2CAP_CONN_REQ:
		/* identifier = *pp++; */
		pp++;
		/* STREAM_TO_UINT16(command_len, pp); */
		pp += 2;
		STREAM_TO_UINT16(psm, pp);
		STREAM_TO_UINT16(scid, pp);
		RTKBT_DBG("%s l2cap conn req, hndl 0x%04x, PSM 0x%04x, "
			  "scid 0x%04x", l2_dir_str[l2->out], handle, psm,
			  scid);
		handle_l2cap_con_req(handle, psm, scid, l2->out);
		break;

	case L2CAP_CONN_RSP:
		/* identifier = *pp++; */
		pp++;
		/* STREAM_TO_UINT16(command_len, pp); */
		pp += 2;
		STREAM_TO_UINT16(dcid, pp);
		STREAM_TO_UINT16(scid, pp);
		STREAM_TO_UINT16(result, pp);
		/* STREAM_TO_UINT16(status, pp); */
		pp += 2;
		RTKBT_DBG("%s l2cap conn rsp, hndl 0x%04x, dcid 0x%04x, "
			  "scid 0x%04x, result 0x%04x", l2_dir_str[l2->out],
			  handle, dcid, scid, result);
		handle_l2cap_con_rsp(handle, dcid, scid, l2->out, result);
		break;

	case L2CAP_DISCONN_REQ:
		/* identifier = *pp++; */
		pp++;
		/* STREAM_TO_UINT16(command_len, pp); */
		pp += 2;
		STREAM_TO_UINT16(dcid, pp);
		STREAM_TO_UINT16(scid, pp);
		RTKBT_DBG("%s l2cap disconn req, hndl 0x%04x, dcid 0x%04x, "
			  "scid 0x%04x", l2_dir_str[l2->out], handle, dcid, scid);
		handle_l2cap_discon_req(handle, dcid, scid, l2->out);
		break;
	default:
		RTKBT_DBG("undesired l2 command %u", code);
		break;
	}
}

static void rtl_l2_data_process(u8 *pp, u16 len, int dir)
{
	u8 code;
	u8 flag;
	u16 handle, pdu_len, channel_id;
	/* u16 total_len; */
	struct rtl_l2_buff *l2 = NULL;
	u8 *hd = pp;

	/* RTKBT_DBG("l2 sig data %p, len %u, dir %d", pp, len, dir); */

	STREAM_TO_UINT16(handle, pp);
	flag = handle >> 12;
	handle = handle & 0x0FFF;
	/* STREAM_TO_UINT16(total_len, pp); */
	pp += 2; /* data total length */

	STREAM_TO_UINT16(pdu_len, pp);
	STREAM_TO_UINT16(channel_id, pp);

	if (channel_id == 0x0001) {
		code = *pp++;
		switch (code) {
		case L2CAP_CONN_REQ:
		case L2CAP_CONN_RSP:
		case L2CAP_DISCONN_REQ:
			RTKBT_DBG("l2cap op %u, len %u, out %d", code, len,
				  dir);
			l2 = rtl_l2_node_get(&btrtl_coex);
			if (l2) {
				u16 n;
				n = min_t(uint, len, L2_MAX_SUBSEC_LEN);
				memcpy(l2->data, hd, n);
				l2->out = dir;
				rtl_l2_node_to_used(&btrtl_coex, l2);
				queue_delayed_work(btrtl_coex.fw_wq,
						&btrtl_coex.l2_work, 0);
			} else
				RTKBT_ERR("%s: failed to get l2 node",
					  __func__);
			break;
		case L2CAP_DISCONN_RSP:
			break;
		default:
			break;
		}
	} else {
		if ((flag != 0x01) && (is_profile_connected(profile_a2dp) ||
				       is_profile_connected(profile_pan)))
			/* Do not count the continuous packets */
			packets_count(handle, channel_id, pdu_len, dir, pp);
	}
	return;
}


static void rtl_l2_work(struct work_struct *work)
{
	struct rtl_coex_struct *coex;
	struct rtl_l2_buff *l2;
	unsigned long flags;

	coex = container_of(work, struct rtl_coex_struct, l2_work.work);

	spin_lock_irqsave(&coex->buff_lock, flags);
	while (!list_empty(&coex->l2_used_list)) {
		l2 = list_entry(coex->l2_used_list.next, struct rtl_l2_buff,
				list);
		list_del(&l2->list);

		spin_unlock_irqrestore(&coex->buff_lock, flags);

		rtl_process_l2_sig(l2);

		spin_lock_irqsave(&coex->buff_lock, flags);

		list_add_tail(&l2->list, &coex->l2_free_list);
	}
	spin_unlock_irqrestore(&coex->buff_lock, flags);

	return;
}

static void rtl_ev_work(struct work_struct *work)
{
	struct rtl_coex_struct *coex;
	struct rtl_hci_ev *ev;
	unsigned long flags;

	coex = container_of(work, struct rtl_coex_struct, fw_work.work);

	spin_lock_irqsave(&coex->buff_lock, flags);
	while (!list_empty(&coex->ev_used_list)) {
		ev = list_entry(coex->ev_used_list.next, struct rtl_hci_ev,
				list);
		list_del(&ev->list);
		spin_unlock_irqrestore(&coex->buff_lock, flags);

		rtk_parse_event_data(coex, ev->data, ev->len);

		spin_lock_irqsave(&coex->buff_lock, flags);
		list_add_tail(&ev->list, &coex->ev_free_list);
	}
	spin_unlock_irqrestore(&coex->buff_lock, flags);
}

int ev_filter_out(u8 *buf)
{
	switch (buf[0]) {
	case HCI_EV_INQUIRY_COMPLETE:
	case HCI_EV_PIN_CODE_REQ:
	case HCI_EV_IO_CAPA_REQUEST:
	case HCI_EV_AUTH_COMPLETE:
	case HCI_EV_LINK_KEY_NOTIFY:
	case HCI_EV_MODE_CHANGE:
	case HCI_EV_CMD_COMPLETE:
	case HCI_EV_CMD_STATUS:
	case HCI_EV_CONN_COMPLETE:
	case HCI_EV_SYNC_CONN_COMPLETE:
	case HCI_EV_DISCONN_COMPLETE:
	case HCI_EV_VENDOR_SPECIFIC:
		return 0;
	case HCI_EV_LE_META:
		/* Ignore frequent but not useful events that result in
		 * costing too much space.
		 */
		switch (buf[2]) {
		case HCI_EV_LE_CONN_COMPLETE:
		case HCI_EV_LE_ENHANCED_CONN_COMPLETE:
		case HCI_EV_LE_CONN_UPDATE_COMPLETE:
			return 0;
		}
		return 1;
	default:
		return 1;
	}
}

static void rtk_btcoex_evt_enqueue(__u8 *s, __u16 count)
{
	struct rtl_hci_ev *ev;

	if (ev_filter_out(s))
		return;

	ev = rtl_ev_node_get(&btrtl_coex);
	if (!ev) {
		RTKBT_ERR("%s: no free ev node.", __func__);
		return;
	}

	if (count > MAX_LEN_OF_HCI_EV) {
		memcpy(ev->data, s, MAX_LEN_OF_HCI_EV);
		ev->len = MAX_LEN_OF_HCI_EV;
	} else {
		memcpy(ev->data, s, count);
		ev->len = count;
	}

	rtl_ev_node_to_used(&btrtl_coex, ev);

	queue_delayed_work(btrtl_coex.fw_wq, &btrtl_coex.fw_work, 0);
}

/* Context: in_interrupt() */
void rtk_btcoex_parse_event(uint8_t *buffer, int count)
{
	struct rtl_coex_struct *coex = &btrtl_coex;
	__u8 *tbuff;
	__u16 elen = 0;

	/* RTKBT_DBG("%s: parse ev.", __func__); */
	if (!test_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		/* RTKBT_INFO("%s: Coex is closed, ignore", __func__); */
		RTKBT_INFO("%s: Coex is closed, ignore %x, %x",
			   __func__, buffer[0], buffer[1]);
		return;
	}

	spin_lock(&coex->rxlock);

	/* coex->tbuff will be set to NULL when initializing or
	 * there is a complete frame or there is start of a frame */
	tbuff = coex->tbuff;

	while (count) {
		int len;

		/* Start of a frame */
		if (!tbuff) {
			tbuff = coex->back_buff;
			coex->tbuff = NULL;
			coex->elen = 0;

			coex->pkt_type = HCI_EVENT_PKT;
			coex->expect = HCI_EVENT_HDR_SIZE;
		}

		len = min_t(uint, coex->expect, count);
		memcpy(tbuff, buffer, len);
		tbuff += len;
		coex->elen += len;

		count -= len;
		buffer += len;
		coex->expect -= len;

		if (coex->elen == HCI_EVENT_HDR_SIZE) {
			/* Complete event header */
			coex->expect =
				((struct hci_event_hdr *)coex->back_buff)->plen;
			if (coex->expect > HCI_MAX_EVENT_SIZE - coex->elen) {
				tbuff = NULL;
				coex->elen = 0;
				RTKBT_ERR("tbuff room is not enough");
				break;
			}
		}

		if (coex->expect == 0) {
			/* Complete frame */
			elen = coex->elen;
			spin_unlock(&coex->rxlock);
			rtk_btcoex_evt_enqueue(coex->back_buff, elen);
			spin_lock(&coex->rxlock);

			tbuff = NULL;
			coex->elen = 0;
		}
	}

	/* coex->tbuff would be non-NULL if there isn't a complete frame
	 * And it will be updated next time */
	coex->tbuff = tbuff;
	spin_unlock(&coex->rxlock);
}


void rtk_btcoex_parse_l2cap_data_tx(uint8_t *buffer, int count)
{
	if (!test_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		RTKBT_INFO("%s: Coex is closed, ignore", __func__);
		return;
	}

	rtl_l2_data_process(buffer, count, 1);
	//u16 handle, total_len, pdu_len, channel_ID, command_len, psm, scid,
	//    dcid, result, status;
	//u8 flag, code, identifier;
	//u8 *pp = (u8 *) (skb->data);
	//STREAM_TO_UINT16(handle, pp);
	//flag = handle >> 12;
	//handle = handle & 0x0FFF;
	//STREAM_TO_UINT16(total_len, pp);
	//STREAM_TO_UINT16(pdu_len, pp);
	//STREAM_TO_UINT16(channel_ID, pp);

	//if (channel_ID == 0x0001) {
	//	code = *pp++;
	//	switch (code) {
	//	case L2CAP_CONN_REQ:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(psm, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		RTKBT_DBG("TX l2cap conn req, hndl %x, PSM %x, scid=%x",
	//			  handle, psm, scid);
	//		handle_l2cap_con_req(handle, psm, scid, 1);
	//		break;

	//	case L2CAP_CONN_RSP:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(dcid, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		STREAM_TO_UINT16(result, pp);
	//		STREAM_TO_UINT16(status, pp);
	//		RTKBT_DBG("TX l2cap conn rsp, hndl %x, dcid %x, "
	//			  "scid %x, result %x",
	//			  handle, dcid, scid, result);
	//		handle_l2cap_con_rsp(handle, dcid, scid, 1, result);
	//		break;

	//	case L2CAP_DISCONN_REQ:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(dcid, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		RTKBT_DBG("TX l2cap disconn req, hndl %x, dcid %x, "
	//			  "scid %x", handle, dcid, scid);
	//		handle_l2cap_discon_req(handle, dcid, scid, 1);
	//		break;

	//	case L2CAP_DISCONN_RSP:
	//		break;

	//	default:
	//		break;
	//	}
	//} else {
	//	if ((flag != 0x01) && (is_profile_connected(profile_a2dp) || is_profile_connected(profile_pan)))	//Do not count the continuous packets
	//		packets_count(handle, channel_ID, pdu_len, 1, pp);
	//}
}

void rtk_btcoex_parse_l2cap_data_rx(uint8_t *buffer, int count)
{
	if (!test_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		RTKBT_INFO("%s: Coex is closed, ignore", __func__);
		return;
	}

	rtl_l2_data_process(buffer, count, 0);
	//u16 handle, total_len, pdu_len, channel_ID, command_len, psm, scid,
	//    dcid, result, status;
	//u8 flag, code, identifier;
	//u8 *pp = urb->transfer_buffer;
	//STREAM_TO_UINT16(handle, pp);
	//flag = handle >> 12;
	//handle = handle & 0x0FFF;
	//STREAM_TO_UINT16(total_len, pp);
	//STREAM_TO_UINT16(pdu_len, pp);
	//STREAM_TO_UINT16(channel_ID, pp);

	//if (channel_ID == 0x0001) {
	//	code = *pp++;
	//	switch (code) {
	//	case L2CAP_CONN_REQ:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(psm, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		RTKBT_DBG("RX l2cap conn req, hndl %x, PSM %x, scid %x",
	//			  handle, psm, scid);
	//		handle_l2cap_con_req(handle, psm, scid, 0);
	//		break;

	//	case L2CAP_CONN_RSP:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(dcid, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		STREAM_TO_UINT16(result, pp);
	//		STREAM_TO_UINT16(status, pp);
	//		RTKBT_DBG("RX l2cap conn rsp, hndl %x, dcid %x, "
	//			  "scid %x, result %x",
	//			  handle, dcid, scid, result);
	//		handle_l2cap_con_rsp(handle, dcid, scid, 0, result);
	//		break;

	//	case L2CAP_DISCONN_REQ:
	//		identifier = *pp++;
	//		STREAM_TO_UINT16(command_len, pp);
	//		STREAM_TO_UINT16(dcid, pp);
	//		STREAM_TO_UINT16(scid, pp);
	//		RTKBT_DBG("RX l2cap disconn req, hndl %x, dcid %x, "
	//			  "scid %x", handle, dcid, scid);
	//		handle_l2cap_discon_req(handle, dcid, scid, 0);
	//		break;

	//	case L2CAP_DISCONN_RSP:
	//		break;

	//	default:
	//		break;
	//	}
	//} else {
	//	if ((flag != 0x01) && (is_profile_connected(profile_a2dp) || is_profile_connected(profile_pan)))	//Do not count the continuous packets
	//		packets_count(handle, channel_ID, pdu_len, 0, pp);
	//}
}

#ifdef RTB_SOFTWARE_MAILBOX

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0)
static void polling_bt_info(struct timer_list *unused)
#else
static void polling_bt_info(unsigned long data)
#endif
{
	uint8_t temp_cmd[1];
	RTKBT_DBG("polling timer");
	if (btrtl_coex.polling_enable) {
		//temp_cmd[0] = HCI_VENDOR_SUB_CMD_BT_REPORT_CONN_SCO_INQ_INFO;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_BT_AUTO_REPORT_STATUS_INFO;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 1, temp_cmd);
	}
	mod_timer(&btrtl_coex.polling_timer,
		  jiffies + msecs_to_jiffies(1000 * btrtl_coex.polling_interval));
}

static void rtk_handle_bt_info_control(uint8_t *p)
{
	uint8_t temp_cmd[20];
	struct rtl_btinfo_ctl *ctl = (struct rtl_btinfo_ctl*)p;
	RTKBT_DBG("Received polling_enable %u, polling_time %u, "
		  "autoreport_enable %u", ctl->polling_enable,
		  ctl->polling_time, ctl->autoreport_enable);
	RTKBT_DBG("coex: original polling_enable %u",
		  btrtl_coex.polling_enable);

	if (ctl->polling_enable && !btrtl_coex.polling_enable) {
		/* setup polling timer for getting bt info from firmware */
		btrtl_coex.polling_timer.expires =
		    jiffies + msecs_to_jiffies(ctl->polling_time * 1000);
		mod_timer(&btrtl_coex.polling_timer,
			  btrtl_coex.polling_timer.expires);
	}

	/* Close bt info polling timer */
	if (!ctl->polling_enable && btrtl_coex.polling_enable)
		del_timer(&btrtl_coex.polling_timer);

	if (btrtl_coex.autoreport != ctl->autoreport_enable) {
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_BT_AUTO_REPORT_ENABLE;
		temp_cmd[1] = 1;
		temp_cmd[2] = ctl->autoreport_enable;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 3, temp_cmd);
	}

	btrtl_coex.polling_enable = ctl->polling_enable;
	btrtl_coex.polling_interval = ctl->polling_time;
	btrtl_coex.autoreport = ctl->autoreport_enable;

	rtk_notify_info_to_wifi(HOST_RESPONSE, 0, NULL);
}

static void rtk_handle_bt_coex_control(uint8_t * p)
{
	uint8_t temp_cmd[20];
	uint8_t opcode, opcode_len, value, power_decrease, psd_mode,
	    access_type;

	opcode = *p++;
	RTKBT_DBG("receive bt coex control event from wifi, op 0x%02x", opcode);

	switch (opcode) {
	case BT_PATCH_VERSION_QUERY:
		rtk_notify_btpatch_version_to_wifi();
		break;

	case IGNORE_WLAN_ACTIVE_CONTROL:
		opcode_len = *p++;
		value = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_BT_ENABLE_IGNORE_WLAN_ACT_CMD;
		temp_cmd[1] = 1;
		temp_cmd[2] = value;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 3, temp_cmd);
		break;

	case LNA_CONSTRAIN_CONTROL:
		opcode_len = *p++;
		value = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_SET_BT_LNA_CONSTRAINT;
		temp_cmd[1] = 1;
		temp_cmd[2] = value;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 3, temp_cmd);
		break;

	case BT_POWER_DECREASE_CONTROL:
		opcode_len = *p++;
		power_decrease = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_WIFI_FORCE_TX_POWER_CMD;
		temp_cmd[1] = 1;
		temp_cmd[2] = power_decrease;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 3, temp_cmd);
		break;

	case BT_PSD_MODE_CONTROL:
		opcode_len = *p++;
		psd_mode = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_SET_BT_PSD_MODE;
		temp_cmd[1] = 1;
		temp_cmd[2] = psd_mode;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 3, temp_cmd);
		break;

	case WIFI_BW_CHNL_NOTIFY:
		opcode_len = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_WIFI_CHANNEL_AND_BANDWIDTH_CMD;
		temp_cmd[1] = 3;
		memcpy(temp_cmd + 2, p, 3);	//wifi_state, wifi_centralchannel, chnnels_btnotuse
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 5, temp_cmd);
		break;

	case QUERY_BT_AFH_MAP:
		opcode_len = *p++;
		btrtl_coex.piconet_id = *p++;
		btrtl_coex.mode = *p++;
		temp_cmd[0] = HCI_VENDOR_SUB_CMD_GET_AFH_MAP_L;
		temp_cmd[1] = 2;
		temp_cmd[2] = btrtl_coex.piconet_id;
		temp_cmd[3] = btrtl_coex.mode;
		rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 4, temp_cmd);
		break;

	case BT_REGISTER_ACCESS:
		opcode_len = *p++;
		access_type = *p++;
		if (access_type == 0) {	//read
			temp_cmd[0] = HCI_VENDOR_SUB_CMD_RD_REG_REQ;
			temp_cmd[1] = 5;
			temp_cmd[2] = *p++;
			memcpy(temp_cmd + 3, p, 4);
			rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 7,
					     temp_cmd);
		} else {	//write
			temp_cmd[0] = HCI_VENDOR_SUB_CMD_RD_REG_REQ;
			temp_cmd[1] = 5;
			temp_cmd[2] = *p++;
			memcpy(temp_cmd + 3, p, 8);
			rtk_vendor_cmd_to_fw(HCI_VENDOR_MAILBOX_CMD, 11,
					     temp_cmd);
		}
		break;

	default:
		break;
	}
}

static void rtk_handle_event_from_wifi(uint8_t * msg)
{
	uint8_t *p = msg;
	uint8_t event_code = *p++;
	uint8_t total_length;
	uint8_t extension_event;
	uint8_t operation;
	uint16_t wifi_opcode;
	uint8_t op_status;

	if (memcmp(msg, invite_rsp, sizeof(invite_rsp)) == 0) {
		RTKBT_DBG("receive invite rsp from wifi, wifi is already on");
		btrtl_coex.wifi_on = 1;
		rtk_notify_extension_version_to_wifi();
	}

	if (memcmp(msg, attend_req, sizeof(attend_req)) == 0) {
		RTKBT_DBG("receive attend req from wifi, wifi turn on");
		btrtl_coex.wifi_on = 1;
		rtkbt_coexmsg_send(attend_ack, sizeof(attend_ack));
		rtk_notify_extension_version_to_wifi();
	}

	if (memcmp(msg, wifi_leave, sizeof(wifi_leave)) == 0) {
		RTKBT_DBG("receive wifi leave from wifi, wifi turn off");
		btrtl_coex.wifi_on = 0;
		rtkbt_coexmsg_send(leave_ack, sizeof(leave_ack));
		if (btrtl_coex.polling_enable) {
			btrtl_coex.polling_enable = 0;
			del_timer(&btrtl_coex.polling_timer);
		}
	}

	if (memcmp(msg, leave_ack, sizeof(leave_ack)) == 0) {
		RTKBT_DBG("receive leave ack from wifi");
	}

	if (event_code == 0xFE) {
		total_length = *p++;
		extension_event = *p++;
		switch (extension_event) {
		case RTK_HS_EXTENSION_EVENT_WIFI_SCAN:
			operation = *p;
			RTKBT_DBG("Recv WiFi scan notify event from WiFi, "
				  "op 0x%02x", operation);
			break;

		case RTK_HS_EXTENSION_EVENT_HCI_BT_INFO_CONTROL:
			rtk_handle_bt_info_control(p);
			break;

		case RTK_HS_EXTENSION_EVENT_HCI_BT_COEX_CONTROL:
			rtk_handle_bt_coex_control(p);
			break;

		default:
			break;
		}
	}

	if (event_code == 0x0E) {
		p += 2;		//length, number of complete packets
		STREAM_TO_UINT16(wifi_opcode, p);
		op_status = *p;
		RTKBT_DBG("Recv cmd complete event from WiFi, op 0x%02x, "
			  "status 0x%02x", wifi_opcode, op_status);
	}
}
#endif /* RTB_SOFTWARE_MAILBOX */

static inline void rtl_free_frags(struct rtl_coex_struct *coex)
{
	unsigned long flags;

	spin_lock_irqsave(&coex->rxlock, flags);

	coex->elen = 0;
	coex->tbuff = NULL;

	spin_unlock_irqrestore(&coex->rxlock, flags);
}

void rtk_btcoex_open(struct hci_dev *hdev)
{
	if (test_and_set_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		RTKBT_WARN("RTL COEX is already running.");
		return;
	}

	RTKBT_INFO("Open BTCOEX");

	/* Just for test */
	//struct rtl_btinfo_ctl ctl;

	INIT_DELAYED_WORK(&btrtl_coex.fw_work, (void *)rtl_ev_work);
#ifdef RTB_SOFTWARE_MAILBOX
#ifdef RTK_COEX_OVER_SYMBOL
	INIT_WORK(&rtw_work, rtw_work_func);
	skb_queue_head_init(&rtw_q);
	rtw_coex_on = 1;
#else
	INIT_DELAYED_WORK(&btrtl_coex.sock_work,
			  (void *)udpsocket_recv_data);
#endif
#endif /* RTB_SOFTWARE_MAILBOX */
	INIT_DELAYED_WORK(&btrtl_coex.l2_work, (void *)rtl_l2_work);

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 14, 0)
#ifdef RTB_SOFTWARE_MAILBOX
	timer_setup(&btrtl_coex.polling_timer, polling_bt_info, 0);
#endif
	timer_setup(&btrtl_coex.a2dp_count_timer, count_a2dp_packet_timeout, 0);
	timer_setup(&btrtl_coex.pan_count_timer, count_pan_packet_timeout, 0);
	timer_setup(&btrtl_coex.hogp_count_timer, count_hogp_packet_timeout, 0);
#else
#ifdef RTB_SOFTWARE_MAILBOX
	setup_timer(&btrtl_coex.polling_timer, polling_bt_info, 0);
#endif
	setup_timer(&btrtl_coex.a2dp_count_timer, count_a2dp_packet_timeout, 0);
	setup_timer(&btrtl_coex.pan_count_timer, count_pan_packet_timeout, 0);
	setup_timer(&btrtl_coex.hogp_count_timer, count_hogp_packet_timeout, 0);
#endif

	btrtl_coex.hdev = hdev;
#ifdef RTB_SOFTWARE_MAILBOX
	btrtl_coex.wifi_on = 0;
#endif

	init_profile_hash(&btrtl_coex);
	init_connection_hash(&btrtl_coex);

	btrtl_coex.pkt_type = 0;
	btrtl_coex.expect = 0;
	btrtl_coex.elen = 0;
	btrtl_coex.tbuff = NULL;

#ifdef RTB_SOFTWARE_MAILBOX
#ifndef RTK_COEX_OVER_SYMBOL
	create_udpsocket();
#endif
	rtkbt_coexmsg_send(invite_req, sizeof(invite_req));
#endif

	/* Just for test */
	//ctl.polling_enable = 1;
	//ctl.polling_time = 1;
	//ctl.autoreport_enable = 1;
	//rtk_handle_bt_info_control((u8 *)&ctl);
}

void rtk_btcoex_close(void)
{
	int kk = 0;

	if (!test_and_clear_bit(RTL_COEX_RUNNING, &btrtl_coex.flags)) {
		RTKBT_WARN("RTL COEX is already closed.");
		return;
	}

	RTKBT_INFO("Close BTCOEX");

#ifdef RTB_SOFTWARE_MAILBOX
	/* Close coex socket */
	if (btrtl_coex.wifi_on)
		rtkbt_coexmsg_send(bt_leave, sizeof(bt_leave));
#ifdef RTK_COEX_OVER_SYMBOL
	rtw_coex_on = 0;
	skb_queue_purge(&rtw_q);
	cancel_work_sync(&rtw_work);
#else
	cancel_delayed_work_sync(&btrtl_coex.sock_work);
	if (btrtl_coex.sock_open) {
		btrtl_coex.sock_open = 0;
		RTKBT_DBG("release udp socket");
		sock_release(btrtl_coex.udpsock);
	}
#endif

	/* Delete all timers */
	if (btrtl_coex.polling_enable) {
		btrtl_coex.polling_enable = 0;
		del_timer_sync(&(btrtl_coex.polling_timer));
	}
#endif /* RTB_SOFTWARE_MAILBOX */

	del_timer_sync(&btrtl_coex.a2dp_count_timer);
	del_timer_sync(&btrtl_coex.pan_count_timer);
	del_timer_sync(&btrtl_coex.hogp_count_timer);

	cancel_delayed_work_sync(&btrtl_coex.fw_work);
	cancel_delayed_work_sync(&btrtl_coex.l2_work);

	flush_connection_hash(&btrtl_coex);
	flush_profile_hash(&btrtl_coex);
	btrtl_coex.profile_bitmap = 0;
	btrtl_coex.profile_status = 0;
	for (kk = 0; kk < 8; kk++)
		btrtl_coex.profile_refcount[kk] = 0;

	rtl_free_frags(&btrtl_coex);
	RTKBT_DBG("-x");
}

void rtk_btcoex_probe(struct hci_dev *hdev)
{
	btrtl_coex.hdev = hdev;
	spin_lock_init(&btrtl_coex.spin_lock_sock);
	spin_lock_init(&btrtl_coex.spin_lock_profile);
}

void rtk_btcoex_init(void)
{
	RTKBT_DBG("%s: version: %s", __func__, RTK_VERSION);
	RTKBT_DBG("create workqueue");
#ifdef RTB_SOFTWARE_MAILBOX
#ifdef RTK_COEX_OVER_SYMBOL
	RTKBT_INFO("Coex over Symbol");
	rtw_wq = create_workqueue("btcoexwork");
	skb_queue_head_init(&rtw_q);
#else
	RTKBT_INFO("Coex over UDP");
	btrtl_coex.sock_wq = create_workqueue("btudpwork");
#endif
#endif /* RTB_SOFTWARE_MAILBOX */
	btrtl_coex.fw_wq = create_workqueue("btfwwork");
	rtl_alloc_buff(&btrtl_coex);
	spin_lock_init(&btrtl_coex.rxlock);
}

void rtk_btcoex_exit(void)
{
	RTKBT_DBG("%s: destroy workqueue", __func__);
#ifdef RTB_SOFTWARE_MAILBOX
#ifdef RTK_COEX_OVER_SYMBOL
	flush_workqueue(rtw_wq);
	destroy_workqueue(rtw_wq);
#else
	flush_workqueue(btrtl_coex.sock_wq);
	destroy_workqueue(btrtl_coex.sock_wq);
#endif
#endif
	flush_workqueue(btrtl_coex.fw_wq);
	destroy_workqueue(btrtl_coex.fw_wq);
	rtl_free_buff(&btrtl_coex);
}
