/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2015 OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright 2018 Joyent, Inc.
 */

#include <sys/vxlan.h>
#include "i40e_sw.h"

/*
 * ---------------------------------------------------------
 * Buffer and Memory Management, Receiving, and Transmitting
 * ---------------------------------------------------------
 *
 * Each physical function (PF), which is what we think of as an instance of the
 * device driver, has a series of associated transmit and receive queue pairs.
 * Effectively, what we think of in MAC as rings. Each of these has their own
 * ring of descriptors which is used as part of doing DMA activity.
 *
 * The transmit ring of descriptors are 16-byte entries which are used to send
 * packets, program filters, etc. The receive ring of descriptors are either
 * 16-byte or 32-bytes each. At the moment, we opt to use the larger descriptor
 * format so that we're in a better position if we ever want to leverage that
 * information later on.
 *
 * However, these rings are just for descriptors, they don't talk or deal with
 * how we actually store the memory that we need for DMA or the associated
 * information that we need for keeping track of message blocks. To correspond
 * to the hardware descriptor ring which is how we communicate with hardware, we
 * introduce a control block which keeps track of our required metadata like DMA
 * mappings.
 *
 * There are two main considerations that dictate how much memory and buffers
 * we end up allocating. Those are:
 *
 *   o The size of the ring (controlled through the driver.conf file)
 *
 *   o The maximum size frame we can receive.
 *
 * The size of the rings currently defaults to 1024 descriptors and is stored in
 * the i40e_t`i40e_rx_ring_size and i40e_t`i40e_tx_ring_size.
 *
 * While the size of the rings is controlled by the driver.conf, the maximum
 * size frame is informed primarily through the use of dladm and the setting of
 * the MTU property on the device. From the MTU, we then go and do some
 * machinations. The first thing we do is we then have to add in space for the
 * Ethernet header, potentially a VLAN header, and the FCS check. This value is
 * what's stored as i40e_t`i40e_frame_max and is derived any time
 * i40e_t`i40e_sdu changes.
 *
 * This size is then rounded up to the nearest 1k chunk, which represents the
 * actual amount of memory that we'll allocate for a single frame.
 *
 * Note, that for rx, we do something that might be unexpected. We always add
 * an extra two bytes to the frame size that we allocate. We then offset the DMA
 * address that we receive a packet into by two bytes. This ensures that the IP
 * header will always be 4 byte aligned because the MAC header is either 14 or
 * 18 bytes in length, depending on the use of 802.1Q tagging, which makes IP's
 * and MAC's lives easier.
 *
 * Both the rx and tx descriptor rings (which are what we use to communicate
 * with hardware) are allocated as a single region of DMA memory which is the
 * size of the descriptor (4 bytes and 2 bytes respectively) times the total
 * number of descriptors for an rx and tx ring.
 *
 * While the rx and tx descriptors are allocated using DMA-based memory, the
 * control blocks for each of them are allocated using normal kernel memory.
 * They aren't special from a DMA perspective. We'll go over the design of both
 * receiving and transmitting separately, as they have slightly different
 * control blocks and different ways that we manage the relationship between
 * control blocks and descriptors.
 *
 * ---------------------------------
 * RX Descriptors and Control Blocks
 * ---------------------------------
 *
 * For every descriptor in the ring that the driver has, we need some associated
 * memory, which means that we need to have the receive specific control block.
 * We have a couple different, but related goals:
 *
 *   o Once we've completed the mc_start GLDv3 endpoint (i40e_m_start), we do
 *     not want to do any additional memory allocations or DMA allocations if
 *     we don't have to.
 *
 *   o We'd like to try and do as much zero-copy as possible, while taking into
 *     account the cost of mapping in DMA resources.
 *
 *   o We'd like to have every receive descriptor available.
 *
 * Now, these rules are a bit in tension with one another. The act of mapping in
 * is an exercise of trying to find the break-even point between page table
 * updates and bcopy. We currently start by using the same metrics that ixgbe
 * used; however, it should be known that this value has effectively been
 * cargo-culted across to yet another driver, sorry.
 *
 * If we receive a packet which is larger than our copy threshold, we'll create
 * a message block out of the DMA memory via desballoc(9F) and send that up to
 * MAC that way. This will cause us to be notified when the message block is
 * then freed because it has been consumed, dropped, or otherwise. Otherwise, if
 * it's less than the threshold, we'll try to use allocb and bcopy it into the
 * block, thus allowing us to immediately reuse the DMA resource. Note, on debug
 * builds, we allow someone to whack the variable i40e_debug_rx_mode to override
 * the behavior and always do a bcopy or a DMA bind.
 *
 * To try and ensure that the device always has blocks that it can receive data
 * into, we maintain two lists of control blocks, a working list and a free
 * list. Each list is sized equal to the number of descriptors in the rx ring.
 * During the GLDv3 mc_start routine, we allocate a number of rx control blocks
 * equal to twice the number of descriptors in the ring and we assign them
 * equally to the free list and to the working list. Each control block also has
 * DMA memory allocated and associated with which it will be used to receive the
 * actual packet data. All of a received frame's data will end up in a single
 * DMA buffer.
 *
 * During operation, we always maintain the invariant that each rx descriptor
 * has an associated rx control block which lives in the working list. If we
 * feel that we should loan up DMA memory to MAC in the form of a message block,
 * we can only do so if we can maintain this invariant. To do that, we swap in
 * one of the buffers from the free list. If none are available, then we resort
 * to using allocb(9F) and bcopy(9F) on the packet instead, regardless of the
 * size.
 *
 * Loaned message blocks come back to use when freemsg(9F) or freeb(9F) is
 * called on the block, at which point we restore the rx control block to the
 * free list and are able to reuse the DMA memory again. While the scheme may
 * seem odd, it importantly keeps us out of trying to do any DMA allocations in
 * the normal path of operation, even though we may still have to allocate
 * message blocks and copy.
 *
 * The following state machine describes the life time of a rx control block. In
 * the diagram we abbrviate the rx ring descriptor entry as rxd and the rx
 * control block entry as rcb.
 *
 *             |                                   |
 *             * ... 1/2 of all initial rcb's  ... *
 *             |                                   |
 *             v                                   v
 *     +------------------+               +------------------+
 *     | rcb on free list |---*---------->| rcb on work list |
 *     +------------------+   .           +------------------+
 *             ^              . moved to          |
 *             |                replace rcb       * . . Frame received,
 *             |                loaned to         |     entry on free list
 *             |                MAC + co.         |     available. rcb's
 *             |                                  |     memory made into mblk_t
 *             * . freemsg(9F)                    |     and sent up to MAC.
 *             |   called on                      |
 *             |   loaned rcb                     |
 *             |   and it is                      v
 *             |   recycled.              +-------------------+
 *             +--------------------<-----| rcb loaned to MAC |
 *                                        +-------------------+
 *
 * Finally, note that every rx control block has a reference count on it. One
 * reference is added as long as the driver has had the GLDv3 mc_start endpoint
 * called. If the GLDv3 mc_stop entry point is called, IP has been unplumbed and
 * no other DLPI consumers remain, then we'll decrement the reference count by
 * one. Whenever we loan up the rx control block and associated buffer to MAC,
 * then we bump the reference count again. Even though the device is stopped,
 * there may still be loaned frames in upper levels that we'll want to account
 * for. Our callback from freemsg(9F)/freeb(9F) will take care of making sure
 * that it is cleaned up.
 *
 * --------------------
 * Managing the RX Ring
 * --------------------
 *
 * The receive ring descriptors are arranged in a circular buffer with a head
 * and tail pointer. There are both the conventional head and tail pointers
 * which are used to partition the ring into two portions, a portion that we,
 * the operating system, manage and a portion that is managed by hardware. When
 * hardware owns a descriptor in the ring, it means that it is waiting for data
 * to be filled in. However, when a portion of the ring is owned by the driver,
 * then that means that the descriptor has been consumed and we need to go take
 * a look at it.
 *
 * The initial head is configured to be zero by writing it as such in the
 * receive queue context in the FPM (function private memory from the host). The
 * initial tail is written to be the last descriptor. This is written to via the
 * PCIe register I40E_QRX_TAIL(). Technically, hardware owns everything between
 * the HEAD and TAIL, inclusive. Note that while we initially program the HEAD,
 * the only values we ever consult ourselves are the TAIL register and our own
 * state tracking. Effectively, we cache the HEAD register and then update it
 * ourselves based on our work.
 *
 * When we iterate over the rx descriptors and thus the received frames, we are
 * either in an interrupt context or we've been asked by MAC to poll on the
 * ring. If we've been asked to poll on the ring, we have a maximum number of
 * bytes of mblk_t's to return. If processing an rx descriptor would cause us to
 * exceed that count, then we do not process it. When in interrupt context, we
 * don't have a strict byte count. However, to ensure liveness, we limit the
 * amount of data based on a configuration value
 * (i40e_t`i40e_rx_limit_per_intr). The number that we've started with for this
 * is based on similar numbers that are used for ixgbe. After some additional
 * time in the field, we'll have a sense as to whether or not it should be
 * changed.
 *
 * When processing, we start at our own HEAD pointer
 * (i40e_rx_data_t`rxd_desc_next), which indicates the descriptor to start
 * processing. Every RX descriptor has what's described as the DD bit. This bit
 * (the LSB of the second 8-byte word), indicates whether or not the descriptor
 * is done.  When we give descriptors to the hardware, this value is always
 * zero. When the hardware has finished a descriptor, it will always be one.
 *
 * The first thing that we check is whether the DD bit indicates that the
 * current HEAD is ready. If it isn't, then we're done. That's the primary
 * invariant of processing a frame. If it's done, then there are a few other
 * things that we want to look at. In the same status word as the DD bit, there
 * are two other important bits:
 *
 *   o End of Packet (EOP)
 *   o Error bits
 *
 * The end of packet indicates that we have reached the last descriptor. Now,
 * you might ask when would there be more than one descriptor. The reason for
 * that might be due to large receive offload (lro) or header splitting
 * functionality, which presently isn't supported in the driver. The error bits
 * in the frame are only valid when EOP is set.
 *
 * If error bits are set on the frame, then we still consume it; however, we
 * will not generate an mblk_t to send up to MAC. If there are no error bits
 * set, then we'll consume the descriptor either using bcopy or DMA binding. See
 * the earlier section 'RX DESCRIPTORS AND CONTROL BLOCKS' for more information
 * on how that selection is made.
 *
 * Regardless of whether we construct an mblk_t or encounter an error, we end up
 * resetting the descriptor. This re-arms the descriptor for hardware and in the
 * process, we may end up assigning it a new receive control bock. After we do
 * this, we always update our HEAD pointer, no matter what.
 *
 * Finally, once we've consumed as much as we will in a given window, we go and
 * update the TAIL register to indicate all the frames we've consumed. We only
 * do a single bulk write for the ring.
 *
 * ---------------------------------
 * TX Descriptors and Control Blocks
 * ---------------------------------
 *
 * While the transmit path is similar in spirit to the receive path, it works
 * differently due to the fact that all data is originated by the operating
 * system and not by the device.
 *
 * Like rx, there is both a descriptor ring that we use to communicate to the
 * driver and which points to the memory used to transmit a frame. Similarly,
 * there is a corresponding transmit control block. Each transmit control block
 * has a region of DMA memory allocated to it; however, the way we use it
 * varies.
 *
 * The driver is asked to process a single frame at a time. That message block
 * may be made up of multiple fragments linked together by the mblk_t`b_cont
 * member. The device has a hard limit of up to 8 buffers being allowed for use
 * for a single logical frame. For each fragment, we'll try and use an entry
 * from the tx descriptor ring and then we'll allocate a corresponding tx
 * control block. Depending on the size of the fragment, we may copy it around
 * or we might instead try to do DMA binding of the fragment.
 *
 * If we exceed the number of blocks that fit, we'll try to pull up the block
 * and then we'll do a DMA bind and send it out.
 *
 * If we don't have enough space in the ring or tx control blocks available,
 * then we'll return the unprocessed message block to MAC. This will induce flow
 * control and once we recycle enough entries, we'll once again enable sending
 * on the ring.
 *
 * We size the working list as equal to the number of descriptors in the ring.
 * We size the free list as equal to 1.5 times the number of descriptors in the
 * ring. We'll allocate a number of tx control block entries equal to the number
 * of entries in the free list. By default, all entries are placed in the free
 * list. As we come along and try to send something, we'll allocate entries from
 * the free list and add them to the working list, where they'll stay until the
 * hardware indicates that all of the data has been written back to us. The
 * reason that we start with 1.5x is to help facilitate having more than one TX
 * buffer associated with the DMA activity.
 *
 * --------------------
 * Managing the TX Ring
 * --------------------
 *
 * The transmit descriptor ring is driven by us. We maintain our own notion of a
 * HEAD and TAIL register and we update the hardware with updates to the TAIL
 * register. When the hardware is done writing out data, it updates us by
 * writing back to a specific address, not by updating the individual
 * descriptors. That address is a 4-byte region after the main transmit
 * descriptor ring. This is why the descriptor ring has an extra descriptor's
 * worth allocated to it.
 *
 * We maintain our notion of the HEAD in the i40e_trqpair_t`itrq_desc_head and
 * the TAIL in the i40e_trqpair_t`itrq_desc_tail. When we write out frames,
 * we'll update the tail there and in the I40E_QTX_TAIL() register. At various
 * points in time, through both interrupts, and our own internal checks, we'll
 * sync the write-back head portion of the DMA space. Based on the index it
 * reports back, we'll free everything between our current HEAD and the
 * indicated index and update HEAD to the new index.
 *
 * When a frame comes in, we try to use a number of transmit control blocks and
 * we'll transition them from the free list to the work list. They'll get moved
 * to the entry on the work list that corresponds with the transmit descriptor
 * they correspond to. Once we are indicated that the corresponding descriptor
 * has been freed, we'll return it to the list.
 *
 * The transmit control block free list is managed by keeping track of the
 * number of entries in it, i40e_trqpair_t`itrq_tcb_free. We use it as a way to
 * index into the free list and add things to it. In effect, we always push and
 * pop from the tail and protect it with a single lock,
 * i40e_trqpair_t`itrq_tcb_lock. This scheme is somewhat simplistic and may not
 * stand up to further performance testing; however, it does allow us to get off
 * the ground with the device driver.
 *
 * The following image describes where a given transmit control block lives in
 * its lifetime:
 *
 *             |
 *             * ... Initial placement for all tcb's
 *             |
 *             v
 *    +------------------+                       +------------------+
 *    | tcb on free list |---*------------------>| tcb on work list |
 *    +------------------+   .                   +------------------+
 *             ^             . tcb allocated               |
 *             |               to send frame               v
 *             |               or fragment on              |
 *             |               wire, mblk from             |
 *             |               MAC associated.             |
 *             |                                           |
 *             +------*-------------------------------<----+
 *                    .
 *                    . Hardware indicates
 *                      entry transmitted.
 *                      tcb recycled, mblk
 *                      from MAC freed.
 *
 * ------------
 * Blocking MAC
 * ------------
 *
 * When performing transmit, we can run out of descriptors and ring entries.
 * When such a case happens, we return the mblk_t to MAC to indicate that we've
 * been blocked. At that point in time, MAC becomes blocked and will not
 * transmit anything out that specific ring until we notify MAC. To indicate
 * that we're in such a situation we set i40e_trqpair_t`itrq_tx_blocked member
 * to B_TRUE.
 *
 * When we recycle tx descriptors then we'll end up signaling MAC by calling
 * mac_tx_ring_update() if we were blocked, letting it know that it's safe to
 * start sending frames out to us again.
 */

/*
 * We set our DMA alignment requests based on the smallest supported page size
 * of the corresponding platform.
 */
#if	defined(__sparc)
#define	I40E_DMA_ALIGNMENT 0x2000ull
#elif defined(__x86)
#define	I40E_DMA_ALIGNMENT 0x1000ull
#else
#error	"unknown architecture for i40e"
#endif

/*
 * This structure is used to maintain information and flags related to
 * transmitting a frame.  These fields are ultimately used to construct the
 * tx data descriptor(s) and, if necessary, the tx context descriptor.
 */
typedef struct i40e_tx_context {
	enum i40e_tx_desc_cmd_bits	itc_data_cmdflags;
	uint32_t			itc_data_offsets;
	boolean_t			itc_ctx_tunneled;
	uint32_t			itc_ctx_tunnel_fld;
	enum i40e_tx_ctx_desc_cmd_bits	itc_ctx_cmdflags;
	uint32_t			itc_ctx_tsolen;
	uint32_t			itc_ctx_mss;
} i40e_tx_context_t;

/*
 * Toggles on debug builds which can be used to override our RX behaviour based
 * on thresholds.
 */
#ifdef	DEBUG
typedef enum {
	I40E_DEBUG_RX_DEFAULT	= 0,
	I40E_DEBUG_RX_BCOPY	= 1,
	I40E_DEBUG_RX_DMABIND	= 2
} i40e_debug_rx_t;

i40e_debug_rx_t i40e_debug_rx_mode = I40E_DEBUG_RX_DEFAULT;
#endif	/* DEBUG */

/*
 * Notes on the following pair of DMA attributes. The first attribute,
 * i40e_static_dma_attr, is designed to be used for both the descriptor rings
 * and the static buffers that we associate with control blocks. For this
 * reason, we force an SGL length of one. While technically the driver supports
 * a larger SGL (5 on rx and 8 on tx), we opt to only use one to simplify our
 * management here. In addition, when the Intel common code wants to allocate
 * memory via the i40e_allocate_virt_mem osdep function, we have it leverage
 * the static dma attr.
 *
 * The latter two sets of attributes, are what we use when we're binding a
 * bunch of mblk_t fragments to go out the door. Note that the main difference
 * here is that we're allowed a larger SGL length.  For non-LSO tx, we
 * restrict the SGL length to match the number of tx buffers available to the
 * PF (8).  For the LSO case we can go much larger, with the caveat that each
 * MSS-sized chunk (segment) must not span more than 8 data descriptors and
 * hence must not span more than 8 cookies.
 *
 * Note, we default to setting ourselves to be DMA capable here. However,
 * because we could have multiple instances which have different FMA error
 * checking capabilities, or end up on different buses, we make these static
 * and const and copy them into the i40e_t for the given device with the actual
 * values that reflect the actual capabilities.
 */
static const ddi_dma_attr_t i40e_g_static_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	0x00000000FFFFFFFFull,		/* dma counter max */
	I40E_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size */
	1,				/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

static const ddi_dma_attr_t i40e_g_txbind_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	I40E_MAX_TX_BUFSZ,		/* dma counter max */
	I40E_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	I40E_TX_MAX_COOKIE,		/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

static const ddi_dma_attr_t i40e_g_txbind_lso_dma_attr = {
	DMA_ATTR_V0,			/* version number */
	0x0000000000000000ull,		/* low address */
	0xFFFFFFFFFFFFFFFFull,		/* high address */
	I40E_MAX_TX_BUFSZ,		/* dma counter max */
	I40E_DMA_ALIGNMENT,		/* alignment */
	0x00000FFF,			/* burst sizes */
	0x00000001,			/* minimum transfer size */
	0x00000000FFFFFFFFull,		/* maximum transfer size */
	0xFFFFFFFFFFFFFFFFull,		/* maximum segment size	 */
	I40E_TX_LSO_MAX_COOKIE,		/* scatter/gather list length */
	0x00000001,			/* granularity */
	DDI_DMA_FLAGERR			/* DMA flags */
};

/*
 * Next, we have the attributes for these structures. The descriptor rings are
 * all strictly little endian, while the data buffers are just arrays of bytes
 * representing frames. Because of this, we purposefully simplify the driver
 * programming life by programming the descriptor ring as little endian, while
 * for the buffer data we keep it as unstructured.
 *
 * Note, that to keep the Intel common code operating in a reasonable way, when
 * we allocate DMA memory for it, we do not use byte swapping and thus use the
 * standard i40e_buf_acc_attr.
 */
static const ddi_device_acc_attr_t i40e_g_desc_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static const ddi_device_acc_attr_t i40e_g_buf_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * The next two functions are designed to be type-safe versions of macros that
 * are used to increment and decrement a descriptor index in the loop. Note,
 * these are marked inline to try and keep the data path hot and they were
 * effectively inlined in their previous life as macros.
 */
static inline int
i40e_next_desc(int base, int count, int size)
{
	int out;

	ASSERT(base >= 0);
	ASSERT(count > 0);
	ASSERT(size > 0);

	if (base + count < size) {
		out = base + count;
	} else {
		out = base + count - size;
	}

	ASSERT(out >= 0 && out < size);
	return (out);
}

static inline int
i40e_prev_desc(int base, int count, int size)
{
	int out;

	ASSERT(base >= 0);
	ASSERT(count > 0);
	ASSERT(size > 0);

	if (base >= count) {
		out = base - count;
	} else {
		out = base - count + size;
	}

	ASSERT(out >= 0 && out < size);
	return (out);
}

/*
 * Free DMA memory that is represented by a i40e_dma_buffer_t.
 */
static void
i40e_free_dma_buffer(i40e_dma_buffer_t *dmap)
{
	if (dmap->dmab_dma_address != NULL) {
		VERIFY(dmap->dmab_dma_handle != NULL);
		(void) ddi_dma_unbind_handle(dmap->dmab_dma_handle);
		dmap->dmab_dma_address = NULL;
		dmap->dmab_size = 0;
	}

	if (dmap->dmab_acc_handle != NULL) {
		ddi_dma_mem_free(&dmap->dmab_acc_handle);
		dmap->dmab_acc_handle = NULL;
		dmap->dmab_address = NULL;
	}

	if (dmap->dmab_dma_handle != NULL) {
		ddi_dma_free_handle(&dmap->dmab_dma_handle);
		dmap->dmab_dma_handle = NULL;
	}

	/*
	 * These should only be set if we have valid handles allocated and
	 * therefore should always be NULLed out due to the above code. This
	 * is here to catch us acting sloppy.
	 */
	ASSERT(dmap->dmab_dma_address == NULL);
	ASSERT(dmap->dmab_address == NULL);
	ASSERT(dmap->dmab_size == 0);
	dmap->dmab_len = 0;
}

/*
 * Allocate size bytes of DMA memory based on the passed in attributes. This
 * fills in the information in dmap and is designed for all of our single cookie
 * allocations.
 */
static boolean_t
i40e_alloc_dma_buffer(i40e_t *i40e, i40e_dma_buffer_t *dmap,
    ddi_dma_attr_t *attrsp, ddi_device_acc_attr_t *accp, boolean_t stream,
    boolean_t zero, size_t size)
{
	int ret;
	uint_t flags;
	size_t len;
	ddi_dma_cookie_t cookie;
	uint_t ncookies;

	if (stream == B_TRUE)
		flags = DDI_DMA_STREAMING;
	else
		flags = DDI_DMA_CONSISTENT;

	/*
	 * Step one: Allocate the DMA handle
	 */
	ret = ddi_dma_alloc_handle(i40e->i40e_dip, attrsp, DDI_DMA_DONTWAIT,
	    NULL, &dmap->dmab_dma_handle);
	if (ret != DDI_SUCCESS) {
		i40e_error(i40e, "failed to allocate dma handle for I/O "
		    "buffers: %d", ret);
		dmap->dmab_dma_handle = NULL;
		return (B_FALSE);
	}

	/*
	 * Step two: Allocate the DMA memory
	 */
	ret = ddi_dma_mem_alloc(dmap->dmab_dma_handle, size, accp, flags,
	    DDI_DMA_DONTWAIT, NULL, &dmap->dmab_address, &len,
	    &dmap->dmab_acc_handle);
	if (ret != DDI_SUCCESS) {
		i40e_error(i40e, "failed to allocate %ld bytes of DMA for I/O "
		    "buffers", size);
		dmap->dmab_address = NULL;
		dmap->dmab_acc_handle = NULL;
		i40e_free_dma_buffer(dmap);
		return (B_FALSE);
	}

	/*
	 * Step three: Optionally zero
	 */
	if (zero == B_TRUE)
		bzero(dmap->dmab_address, len);

	/*
	 * Step four: Bind the memory
	 */
	ret = ddi_dma_addr_bind_handle(dmap->dmab_dma_handle, NULL,
	    dmap->dmab_address, len, DDI_DMA_RDWR | flags, DDI_DMA_DONTWAIT,
	    NULL, &cookie, &ncookies);
	if (ret != DDI_DMA_MAPPED) {
		i40e_error(i40e, "failed to allocate %ld bytes of DMA for I/O "
		    "buffers: %d", size, ret);
		i40e_free_dma_buffer(dmap);
		return (B_FALSE);
	}

	VERIFY(ncookies == 1);
	dmap->dmab_dma_address = cookie.dmac_laddress;
	dmap->dmab_size = len;
	dmap->dmab_len = 0;
	return (B_TRUE);
}

/*
 * This function is called once the last pending rcb has been freed by the upper
 * levels of the system.
 */
static void
i40e_free_rx_data(i40e_rx_data_t *rxd)
{
	VERIFY(rxd->rxd_rcb_pending == 0);

	if (rxd->rxd_rcb_area != NULL) {
		kmem_free(rxd->rxd_rcb_area,
		    sizeof (i40e_rx_control_block_t) *
		    (rxd->rxd_free_list_size + rxd->rxd_ring_size));
		rxd->rxd_rcb_area = NULL;
	}

	if (rxd->rxd_free_list != NULL) {
		kmem_free(rxd->rxd_free_list,
		    sizeof (i40e_rx_control_block_t *) *
		    rxd->rxd_free_list_size);
		rxd->rxd_free_list = NULL;
	}

	if (rxd->rxd_work_list != NULL) {
		kmem_free(rxd->rxd_work_list,
		    sizeof (i40e_rx_control_block_t *) *
		    rxd->rxd_ring_size);
		rxd->rxd_work_list = NULL;
	}

	kmem_free(rxd, sizeof (i40e_rx_data_t));
}

static boolean_t
i40e_alloc_rx_data(i40e_t *i40e, i40e_trqpair_t *itrq)
{
	i40e_rx_data_t *rxd;

	rxd = kmem_zalloc(sizeof (i40e_rx_data_t), KM_NOSLEEP);
	if (rxd == NULL)
		return (B_FALSE);
	itrq->itrq_rxdata = rxd;
	rxd->rxd_i40e = i40e;

	rxd->rxd_ring_size = i40e->i40e_rx_ring_size;
	rxd->rxd_free_list_size = i40e->i40e_rx_ring_size;

	rxd->rxd_rcb_free = rxd->rxd_free_list_size;

	rxd->rxd_work_list = kmem_zalloc(sizeof (i40e_rx_control_block_t *) *
	    rxd->rxd_ring_size, KM_NOSLEEP);
	if (rxd->rxd_work_list == NULL) {
		i40e_error(i40e, "failed to allocate rx work list for a ring "
		    "of %d entries for ring %d", rxd->rxd_ring_size,
		    itrq->itrq_index);
		goto cleanup;
	}

	rxd->rxd_free_list = kmem_zalloc(sizeof (i40e_rx_control_block_t *) *
	    rxd->rxd_free_list_size, KM_NOSLEEP);
	if (rxd->rxd_free_list == NULL) {
		i40e_error(i40e, "failed to allocate a %d entry rx free list "
		    "for ring %d", rxd->rxd_free_list_size, itrq->itrq_index);
		goto cleanup;
	}

	rxd->rxd_rcb_area = kmem_zalloc(sizeof (i40e_rx_control_block_t) *
	    (rxd->rxd_free_list_size + rxd->rxd_ring_size), KM_NOSLEEP);
	if (rxd->rxd_rcb_area == NULL) {
		i40e_error(i40e, "failed to allocate a %d entry rcb area for "
		    "ring %d", rxd->rxd_ring_size + rxd->rxd_free_list_size,
		    itrq->itrq_index);
		goto cleanup;
	}

	return (B_TRUE);

cleanup:
	i40e_free_rx_data(rxd);
	itrq->itrq_rxdata = NULL;
	return (B_FALSE);
}

/*
 * Free all of the memory that we've allocated for DMA. Note that we may have
 * buffers that we've loaned up to the OS which are still outstanding. We'll
 * always free up the descriptor ring, because we no longer need that. For each
 * rcb, we'll iterate over it and if we send the reference count to zero, then
 * we'll free the message block and DMA related resources. However, if we don't
 * take the last one, then we'll go ahead and keep track that we'll have pending
 * data and clean it up when we get there.
 */
static void
i40e_free_rx_dma(i40e_rx_data_t *rxd, boolean_t failed_init)
{
	uint32_t i, count, ref;

	i40e_rx_control_block_t *rcb;
	i40e_t *i40e = rxd->rxd_i40e;

	i40e_free_dma_buffer(&rxd->rxd_desc_area);
	rxd->rxd_desc_ring = NULL;
	rxd->rxd_desc_next = 0;

	mutex_enter(&i40e->i40e_rx_pending_lock);

	rcb = rxd->rxd_rcb_area;
	count = rxd->rxd_ring_size + rxd->rxd_free_list_size;

	for (i = 0; i < count; i++, rcb++) {
		VERIFY(rcb != NULL);

		/*
		 * If we're cleaning up from a failed creation attempt, then an
		 * entry may never have been assembled which would mean that
		 * it's reference count is zero. If we find that, we leave it
		 * be, because nothing else should be modifying it at this
		 * point. We're not at the point that any more references can be
		 * added, just removed.
		 */
		if (failed_init == B_TRUE && rcb->rcb_ref == 0)
			continue;

		ref = atomic_dec_32_nv(&rcb->rcb_ref);
		if (ref == 0) {
			freemsg(rcb->rcb_mp);
			rcb->rcb_mp = NULL;
			i40e_free_dma_buffer(&rcb->rcb_dma);
		} else {
			atomic_inc_32(&rxd->rxd_rcb_pending);
			atomic_inc_32(&i40e->i40e_rx_pending);
		}
	}
	mutex_exit(&i40e->i40e_rx_pending_lock);
}

/*
 * Initialize the DMA memory for the descriptor ring and for each frame in the
 * control block list.
 */
static boolean_t
i40e_alloc_rx_dma(i40e_rx_data_t *rxd)
{
	int i, count;
	size_t dmasz;
	i40e_rx_control_block_t *rcb;
	i40e_t *i40e = rxd->rxd_i40e;

	/*
	 * First allocate the rx descriptor ring.
	 */
	dmasz = sizeof (i40e_rx_desc_t) * rxd->rxd_ring_size;
	VERIFY(dmasz > 0);
	if (i40e_alloc_dma_buffer(i40e, &rxd->rxd_desc_area,
	    &i40e->i40e_static_dma_attr, &i40e->i40e_desc_acc_attr, B_FALSE,
	    B_TRUE, dmasz) == B_FALSE) {
		i40e_error(i40e, "failed to allocate DMA resources "
		    "for rx descriptor ring");
		return (B_FALSE);
	}
	rxd->rxd_desc_ring =
	    (i40e_rx_desc_t *)(uintptr_t)rxd->rxd_desc_area.dmab_address;
	rxd->rxd_desc_next = 0;

	count = rxd->rxd_ring_size + rxd->rxd_free_list_size;
	rcb = rxd->rxd_rcb_area;

	dmasz = i40e->i40e_rx_buf_size;
	VERIFY(dmasz > 0);
	for (i = 0; i < count; i++, rcb++) {
		i40e_dma_buffer_t *dmap;
		VERIFY(rcb != NULL);

		if (i < rxd->rxd_ring_size) {
			rxd->rxd_work_list[i] = rcb;
		} else {
			rxd->rxd_free_list[i - rxd->rxd_ring_size] = rcb;
		}

		dmap = &rcb->rcb_dma;
		if (i40e_alloc_dma_buffer(i40e, dmap,
		    &i40e->i40e_static_dma_attr, &i40e->i40e_buf_acc_attr,
		    B_TRUE, B_FALSE, dmasz) == B_FALSE) {
			i40e_error(i40e, "failed to allocate rx dma buffer");
			return (B_FALSE);
		}

		/*
		 * Initialize the control block and offset the DMA address. See
		 * the note in the big theory statement that explains how this
		 * helps IP deal with alignment. Note, we don't worry about
		 * whether or not we successfully get an mblk_t from desballoc,
		 * it's a common case that we have to handle later on in the
		 * system.
		 */
		dmap->dmab_size -= I40E_BUF_IPHDR_ALIGNMENT;
		dmap->dmab_address += I40E_BUF_IPHDR_ALIGNMENT;
		dmap->dmab_dma_address += I40E_BUF_IPHDR_ALIGNMENT;

		rcb->rcb_ref = 1;
		rcb->rcb_rxd = rxd;
		rcb->rcb_free_rtn.free_func = i40e_rx_recycle;
		rcb->rcb_free_rtn.free_arg = (caddr_t)rcb;
		rcb->rcb_mp = desballoc((unsigned char *)dmap->dmab_address,
		    dmap->dmab_size, 0, &rcb->rcb_free_rtn);
	}

	return (B_TRUE);
}

static void
i40e_free_tx_dma(i40e_trqpair_t *itrq)
{
	size_t fsz;

	if (itrq->itrq_tcb_area != NULL) {
		uint32_t i;
		i40e_tx_control_block_t *tcb = itrq->itrq_tcb_area;

		for (i = 0; i < itrq->itrq_tx_free_list_size; i++, tcb++) {
			i40e_free_dma_buffer(&tcb->tcb_dma);
			if (tcb->tcb_dma_handle != NULL) {
				ddi_dma_free_handle(&tcb->tcb_dma_handle);
				tcb->tcb_dma_handle = NULL;
			}
		}

		fsz = sizeof (i40e_tx_control_block_t) *
		    itrq->itrq_tx_free_list_size;
		kmem_free(itrq->itrq_tcb_area, fsz);
		itrq->itrq_tcb_area = NULL;
	}

	if (itrq->itrq_tcb_free_list != NULL) {
		fsz = sizeof (i40e_tx_control_block_t *) *
		    itrq->itrq_tx_free_list_size;
		kmem_free(itrq->itrq_tcb_free_list, fsz);
		itrq->itrq_tcb_free_list = NULL;
	}

	if (itrq->itrq_tcb_work_list != NULL) {
		fsz = sizeof (i40e_tx_control_block_t *) *
		    itrq->itrq_tx_ring_size;
		kmem_free(itrq->itrq_tcb_work_list, fsz);
		itrq->itrq_tcb_work_list = NULL;
	}

	i40e_free_dma_buffer(&itrq->itrq_desc_area);
	itrq->itrq_desc_ring = NULL;

}

static boolean_t
i40e_alloc_tx_dma(i40e_trqpair_t *itrq)
{
	int i, ret;
	size_t dmasz;
	i40e_tx_control_block_t *tcb;
	i40e_t *i40e = itrq->itrq_i40e;

	itrq->itrq_tx_ring_size = i40e->i40e_tx_ring_size;
	itrq->itrq_tx_free_list_size = i40e->i40e_tx_ring_size +
	    (i40e->i40e_tx_ring_size >> 1);

	/*
	 * Allocate an additional tx descriptor for the writeback head.
	 */
	dmasz = sizeof (i40e_tx_desc_t) * itrq->itrq_tx_ring_size;
	dmasz += sizeof (i40e_tx_desc_t);

	VERIFY(dmasz > 0);
	if (i40e_alloc_dma_buffer(i40e, &itrq->itrq_desc_area,
	    &i40e->i40e_static_dma_attr, &i40e->i40e_desc_acc_attr,
	    B_FALSE, B_TRUE, dmasz) == B_FALSE) {
		i40e_error(i40e, "failed to allocate DMA resources for tx "
		    "descriptor ring");
		return (B_FALSE);
	}
	itrq->itrq_desc_ring =
	    (i40e_tx_desc_t *)(uintptr_t)itrq->itrq_desc_area.dmab_address;
	itrq->itrq_desc_wbhead = (uint32_t *)(itrq->itrq_desc_ring +
	    itrq->itrq_tx_ring_size);
	itrq->itrq_desc_head = 0;
	itrq->itrq_desc_tail = 0;
	itrq->itrq_desc_free = itrq->itrq_tx_ring_size;

	itrq->itrq_tcb_work_list = kmem_zalloc(itrq->itrq_tx_ring_size *
	    sizeof (i40e_tx_control_block_t *), KM_NOSLEEP);
	if (itrq->itrq_tcb_work_list == NULL) {
		i40e_error(i40e, "failed to allocate a %d entry tx work list "
		    "for ring %d", itrq->itrq_tx_ring_size, itrq->itrq_index);
		goto cleanup;
	}

	itrq->itrq_tcb_free_list = kmem_zalloc(itrq->itrq_tx_free_list_size *
	    sizeof (i40e_tx_control_block_t *), KM_SLEEP);
	if (itrq->itrq_tcb_free_list == NULL) {
		i40e_error(i40e, "failed to allocate a %d entry tx free list "
		    "for ring %d", itrq->itrq_tx_free_list_size,
		    itrq->itrq_index);
		goto cleanup;
	}

	/*
	 * We allocate enough tx control blocks to cover the free list.
	 */
	itrq->itrq_tcb_area = kmem_zalloc(sizeof (i40e_tx_control_block_t) *
	    itrq->itrq_tx_free_list_size, KM_NOSLEEP);
	if (itrq->itrq_tcb_area == NULL) {
		i40e_error(i40e, "failed to allocate a %d entry tcb area for "
		    "ring %d", itrq->itrq_tx_free_list_size, itrq->itrq_index);
		goto cleanup;
	}

	/*
	 * For each tcb, allocate DMA memory.
	 */
	dmasz = i40e->i40e_tx_buf_size;
	VERIFY(dmasz > 0);
	tcb = itrq->itrq_tcb_area;
	for (i = 0; i < itrq->itrq_tx_free_list_size; i++, tcb++) {
		VERIFY(tcb != NULL);

		/*
		 * Allocate both a DMA buffer which we'll use for when we copy
		 * packets for transmission and allocate a DMA handle which
		 * we'll use when we bind data.
		 */
		ret = ddi_dma_alloc_handle(i40e->i40e_dip,
		    &i40e->i40e_txbind_dma_attr, DDI_DMA_DONTWAIT, NULL,
		    &tcb->tcb_dma_handle);
		if (ret != DDI_SUCCESS) {
			i40e_error(i40e, "failed to allocate DMA handle for tx "
			    "data binding on ring %d: %d", itrq->itrq_index,
			    ret);
			tcb->tcb_dma_handle = NULL;
			goto cleanup;
		}

		ret = ddi_dma_alloc_handle(i40e->i40e_dip,
		    &i40e->i40e_txbind_lso_dma_attr, DDI_DMA_DONTWAIT, NULL,
		    &tcb->tcb_lso_dma_handle);
		if (ret != DDI_SUCCESS) {
			i40e_error(i40e, "failed to allocate DMA handle for tx "
			    "LSO data binding on ring %d: %d", itrq->itrq_index,
			    ret);
			tcb->tcb_lso_dma_handle = NULL;
			goto cleanup;
		}

		if (i40e_alloc_dma_buffer(i40e, &tcb->tcb_dma,
		    &i40e->i40e_static_dma_attr, &i40e->i40e_buf_acc_attr,
		    B_TRUE, B_FALSE, dmasz) == B_FALSE) {
			i40e_error(i40e, "failed to allocate %ld bytes of "
			    "DMA for tx data binding on ring %d", dmasz,
			    itrq->itrq_index);
			goto cleanup;
		}

		itrq->itrq_tcb_free_list[i] = tcb;
	}

	itrq->itrq_tcb_free = itrq->itrq_tx_free_list_size;

	return (B_TRUE);

cleanup:
	i40e_free_tx_dma(itrq);
	return (B_FALSE);
}

/*
 * Free all memory associated with all of the rings on this i40e instance. Note,
 * this is done as part of the GLDv3 stop routine.
 */
void
i40e_free_ring_mem(i40e_t *i40e, boolean_t failed_init)
{
	int i;

	for (i = 0; i < i40e->i40e_num_trqpairs; i++) {
		i40e_rx_data_t *rxd = i40e->i40e_trqpairs[i].itrq_rxdata;

		/*
		 * Clean up our rx data. We have to free DMA resources first and
		 * then if we have no more pending RCB's, then we'll go ahead
		 * and clean things up. Note, we can't set the stopped flag on
		 * the rx data until after we've done the first pass of the
		 * pending resources. Otherwise we might race with
		 * i40e_rx_recycle on determining who should free the
		 * i40e_rx_data_t above.
		 */
		i40e_free_rx_dma(rxd, failed_init);

		mutex_enter(&i40e->i40e_rx_pending_lock);
		rxd->rxd_shutdown = B_TRUE;
		if (rxd->rxd_rcb_pending == 0) {
			i40e_free_rx_data(rxd);
			i40e->i40e_trqpairs[i].itrq_rxdata = NULL;
		}
		mutex_exit(&i40e->i40e_rx_pending_lock);

		i40e_free_tx_dma(&i40e->i40e_trqpairs[i]);
	}
}

/*
 * Allocate all of the resources associated with all of the rings on this i40e
 * instance. Note this is done as part of the GLDv3 start routine and thus we
 * should not use blocking allocations. This takes care of both DMA and non-DMA
 * related resources.
 */
boolean_t
i40e_alloc_ring_mem(i40e_t *i40e)
{
	int i;

	for (i = 0; i < i40e->i40e_num_trqpairs; i++) {
		if (i40e_alloc_rx_data(i40e, &i40e->i40e_trqpairs[i]) ==
		    B_FALSE)
			goto unwind;

		if (i40e_alloc_rx_dma(i40e->i40e_trqpairs[i].itrq_rxdata) ==
		    B_FALSE)
			goto unwind;

		if (i40e_alloc_tx_dma(&i40e->i40e_trqpairs[i]) == B_FALSE)
			goto unwind;
	}

	return (B_TRUE);

unwind:
	i40e_free_ring_mem(i40e, B_TRUE);
	return (B_FALSE);
}


/*
 * Because every instance of i40e may have different support for FMA
 * capabilities, we copy the DMA attributes into the i40e_t and set them that
 * way and use them for determining attributes.
 */
void
i40e_init_dma_attrs(i40e_t *i40e, boolean_t fma)
{
	bcopy(&i40e_g_static_dma_attr, &i40e->i40e_static_dma_attr,
	    sizeof (ddi_dma_attr_t));
	bcopy(&i40e_g_txbind_dma_attr, &i40e->i40e_txbind_dma_attr,
	    sizeof (ddi_dma_attr_t));
	bcopy(&i40e_g_txbind_lso_dma_attr, &i40e->i40e_txbind_lso_dma_attr,
	    sizeof (ddi_dma_attr_t));
	bcopy(&i40e_g_desc_acc_attr, &i40e->i40e_desc_acc_attr,
	    sizeof (ddi_device_acc_attr_t));
	bcopy(&i40e_g_buf_acc_attr, &i40e->i40e_buf_acc_attr,
	    sizeof (ddi_device_acc_attr_t));

	if (fma == B_TRUE) {
		i40e->i40e_static_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;
		i40e->i40e_txbind_dma_attr.dma_attr_flags |= DDI_DMA_FLAGERR;
		i40e->i40e_txbind_lso_dma_attr.dma_attr_flags |=
		    DDI_DMA_FLAGERR;
	} else {
		i40e->i40e_static_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
		i40e->i40e_txbind_dma_attr.dma_attr_flags &= ~DDI_DMA_FLAGERR;
		i40e->i40e_txbind_lso_dma_attr.dma_attr_flags &=
		    ~DDI_DMA_FLAGERR;
	}
}

static void
i40e_rcb_free(i40e_rx_data_t *rxd, i40e_rx_control_block_t *rcb)
{
	mutex_enter(&rxd->rxd_free_lock);
	ASSERT(rxd->rxd_rcb_free < rxd->rxd_free_list_size);
	ASSERT(rxd->rxd_free_list[rxd->rxd_rcb_free] == NULL);
	rxd->rxd_free_list[rxd->rxd_rcb_free] = rcb;
	rxd->rxd_rcb_free++;
	mutex_exit(&rxd->rxd_free_lock);
}

static i40e_rx_control_block_t *
i40e_rcb_alloc(i40e_rx_data_t *rxd)
{
	i40e_rx_control_block_t *rcb;

	mutex_enter(&rxd->rxd_free_lock);
	if (rxd->rxd_rcb_free == 0) {
		mutex_exit(&rxd->rxd_free_lock);
		return (NULL);
	}
	rxd->rxd_rcb_free--;
	rcb = rxd->rxd_free_list[rxd->rxd_rcb_free];
	VERIFY(rcb != NULL);
	rxd->rxd_free_list[rxd->rxd_rcb_free] = NULL;
	mutex_exit(&rxd->rxd_free_lock);

	return (rcb);
}

/*
 * This is the callback that we get from the OS when freemsg(9F) has been called
 * on a loaned descriptor. In addition, if we take the last reference count
 * here, then we have to tear down all of the rx data.
 */
void
i40e_rx_recycle(caddr_t arg)
{
	uint32_t ref;
	i40e_rx_control_block_t *rcb;
	i40e_rx_data_t *rxd;
	i40e_t *i40e;

	/* LINTED: E_BAD_PTR_CAST_ALIGN */
	rcb = (i40e_rx_control_block_t *)arg;
	rxd = rcb->rcb_rxd;
	i40e = rxd->rxd_i40e;

	/*
	 * It's possible for this to be called with a reference count of zero.
	 * That will happen when we're doing the freemsg after taking the last
	 * reference because we're tearing down everything and this rcb is not
	 * outstanding.
	 */
	if (rcb->rcb_ref == 0)
		return;

	/*
	 * Don't worry about failure of desballoc here. It'll only become fatal
	 * if we're trying to use it and we can't in i40e_rx_bind().
	 */
	rcb->rcb_mp = desballoc((unsigned char *)rcb->rcb_dma.dmab_address,
	    rcb->rcb_dma.dmab_size, 0, &rcb->rcb_free_rtn);
	i40e_rcb_free(rxd, rcb);

	/*
	 * It's possible that the rcb was being used while we are shutting down
	 * the device. In that case, we'll take the final reference from the
	 * device here.
	 */
	ref = atomic_dec_32_nv(&rcb->rcb_ref);
	if (ref == 0) {
		freemsg(rcb->rcb_mp);
		rcb->rcb_mp = NULL;
		i40e_free_dma_buffer(&rcb->rcb_dma);

		mutex_enter(&i40e->i40e_rx_pending_lock);
		atomic_dec_32(&rxd->rxd_rcb_pending);
		atomic_dec_32(&i40e->i40e_rx_pending);

		/*
		 * If this was the last block and it's been indicated that we've
		 * passed the shutdown point, we should clean up.
		 */
		if (rxd->rxd_shutdown == B_TRUE && rxd->rxd_rcb_pending == 0) {
			i40e_free_rx_data(rxd);
			cv_broadcast(&i40e->i40e_rx_pending_cv);
		}

		mutex_exit(&i40e->i40e_rx_pending_lock);
	}
}

static mblk_t *
i40e_rx_bind(i40e_trqpair_t *itrq, i40e_rx_data_t *rxd, uint32_t index,
    uint32_t plen)
{
	mblk_t *mp;
	i40e_t *i40e = rxd->rxd_i40e;
	i40e_rx_control_block_t *rcb, *rep_rcb;

	ASSERT(MUTEX_HELD(&itrq->itrq_rx_lock));

	if ((rep_rcb = i40e_rcb_alloc(rxd)) == NULL) {
		itrq->itrq_rxstat.irxs_rx_bind_norcb.value.ui64++;
		return (NULL);
	}

	rcb = rxd->rxd_work_list[index];

	/*
	 * Check to make sure we have a mblk_t. If we don't, this is our last
	 * chance to try and get one.
	 */
	if (rcb->rcb_mp == NULL) {
		rcb->rcb_mp =
		    desballoc((unsigned char *)rcb->rcb_dma.dmab_address,
		    rcb->rcb_dma.dmab_size, 0, &rcb->rcb_free_rtn);
		if (rcb->rcb_mp == NULL) {
			itrq->itrq_rxstat.irxs_rx_bind_nomp.value.ui64++;
			i40e_rcb_free(rxd, rcb);
			return (NULL);
		}
	}

	I40E_DMA_SYNC(&rcb->rcb_dma, DDI_DMA_SYNC_FORKERNEL);

	if (i40e_check_dma_handle(rcb->rcb_dma.dmab_dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
		i40e_rcb_free(rxd, rcb);
		return (NULL);
	}

	/*
	 * Note, we've already accounted for the I40E_BUF_IPHDR_ALIGNMENT.
	 */
	mp = rcb->rcb_mp;
	atomic_inc_32(&rcb->rcb_ref);
	mp->b_wptr = mp->b_rptr + plen;
	mp->b_next = mp->b_cont = NULL;

	rxd->rxd_work_list[index] = rep_rcb;
	return (mp);
}

/*
 * We're going to allocate a new message block for this frame and attempt to
 * receive it. See the big theory statement for more information on when we copy
 * versus bind.
 */
static mblk_t *
i40e_rx_copy(i40e_trqpair_t *itrq, i40e_rx_data_t *rxd, uint32_t index,
    uint32_t plen)
{
	i40e_t *i40e = rxd->rxd_i40e;
	i40e_rx_control_block_t *rcb;
	mblk_t *mp;

	ASSERT(index < rxd->rxd_ring_size);
	rcb = rxd->rxd_work_list[index];

	I40E_DMA_SYNC(&rcb->rcb_dma, DDI_DMA_SYNC_FORKERNEL);

	if (i40e_check_dma_handle(rcb->rcb_dma.dmab_dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
		return (NULL);
	}

	mp = allocb(plen + I40E_BUF_IPHDR_ALIGNMENT, 0);
	if (mp == NULL) {
		itrq->itrq_rxstat.irxs_rx_copy_nomem.value.ui64++;
		return (NULL);
	}

	mp->b_rptr += I40E_BUF_IPHDR_ALIGNMENT;
	bcopy(rcb->rcb_dma.dmab_address, mp->b_rptr, plen);
	mp->b_wptr = mp->b_rptr + plen;

	return (mp);
}

/*
 * Determine if this pinfo is valid for L4 outer checksum offload for a
 * non-tunneled packet. This is the case for an IP packe
 */
static inline int
i40e_rx_ptype_nontunnel_ol4(struct i40e_rx_ptype_decoded *pinfo)
{
	return (pinfo->outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    pinfo->tunnel_type == I40E_RX_PTYPE_TUNNEL_NONE &&
	    (pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_UDP ||
	    pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_TCP ||
	    pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_SCTP));
}

static inline int
i40e_rx_ptype_tunnel_il4(struct i40e_rx_ptype_decoded *pinfo)
{
	return (pinfo->outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    (pinfo->tunnel_type == I40E_RX_PTYPE_TUNNEL_IP_GRENAT_MAC ||
	    pinfo->tunnel_type == I40E_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN) &&
	    pinfo->tunnel_end_frag == I40E_RX_PTYPE_NOT_FRAG &&
	    pinfo->tunnel_end_prot != I40E_RX_PTYPE_TUNNEL_END_NONE &&
	    (pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_UDP ||
	    pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_TCP ||
	    pinfo->inner_prot == I40E_RX_PTYPE_INNER_PROT_SCTP));
}

/*
 * Determine if the device has enabled any checksum flags for us. The level of
 * checksum computed will depend on the type packet that we have, which is
 * contained in ptype. For example, the checksum logic it does will vary
 * depending on whether or not the packet is considered tunneled, whether it
 * recognizes the L4 type, etc. Section 8.3.4.3 summarizes which checksums are
 * valid.
 *
 * While there are additional checksums that we could recognize here, we'll need
 * to get some additional GLDv3 enhancements to be able to properly describe
 * them.
 */
static void
i40e_rx_hcksum(i40e_trqpair_t *itrq, mblk_t *mp, uint64_t status, uint32_t err,
    uint32_t ptype)
{
	uint32_t cksum;
	struct i40e_rx_ptype_decoded pinfo;

	ASSERT(ptype <= 255);
	pinfo = decode_rx_desc_ptype(ptype);

	cksum = 0;

	/*
	 * If the ptype isn't something that we know in the driver, then we
	 * shouldn't even consider moving forward.
	 */
	if (pinfo.known == 0) {
		itrq->itrq_rxstat.irxs_hck_unknown.value.ui64++;
		return;
	}

	/*
	 * If hardware didn't set the L3L4P bit on the frame, then there is no
	 * checksum offload to consider.
	 */
	if ((status & (1 << I40E_RX_DESC_STATUS_L3L4P_SHIFT)) == 0) {
		itrq->itrq_rxstat.irxs_hck_nol3l4p.value.ui64++;
		return;
	}

	/*
	 * The device tells us that IPv6 checksums where a Destination Options
	 * Header or a Routing header shouldn't be trusted. Discard all
	 * checksums in this case.
	 */
	if (pinfo.outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    pinfo.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV6 &&
	    (status & (1 << I40E_RX_DESC_STATUS_IPV6EXADD_SHIFT))) {
		itrq->itrq_rxstat.irxs_hck_v6skip.value.ui64++;
		return;
	}

	/*
	 * The hardware denotes three kinds of possible errors. Two are used
	 * for inner and outer IP checksum errors (IPE and EIPE) and the third
	 * is for L4 checksum errors (L4E). If there is only one IP header, then
	 * the only thing that we care about is IPE. However, if this is a
	 * tunnel packet, then we care about EPIE. Note, none of this controls
	 * whether or not we have an inner IPv4 checksum.
	 */
	if (pinfo.outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    pinfo.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV4) {
		if (pinfo.tunnel_type == I40E_RX_PTYPE_TUNNEL_NONE) {
			if ((err & (1 << I40E_RX_DESC_ERROR_IPE_SHIFT)) != 0) {
				itrq->itrq_rxstat.irxs_hck_iperr.value.ui64++;
			} else {
				itrq->itrq_rxstat.irxs_hck_v4hdrok.value.ui64++;
				cksum |= HCK_IPV4_HDRCKSUM_OK;
			}
		} else {
			if ((err & (1 << I40E_RX_DESC_ERROR_EIPE_SHIFT)) != 0) {
				itrq->itrq_rxstat.irxs_hck_eiperr.value.ui64++;
			} else {
				itrq->itrq_rxstat.irxs_hck_v4hdrok.value.ui64++;
				cksum |= HCK_IPV4_HDRCKSUM_OK;
			}
		}
	}

	/*
	 * If we have a fragmented packet in any form, we're done.
	 */
	if (pinfo.outer_frag != I40E_RX_PTYPE_NOT_FRAG)
		goto done;

	/*
	 * If we have a tunneled packet and the inner IP header is IPv4, check
	 * IPE to see if we have a valid L4 checksum.
	 */
	if (pinfo.tunnel_type != I40E_RX_PTYPE_TUNNEL_NONE &&
	    pinfo.tunnel_end_prot == I40E_RX_PTYPE_TUNNEL_END_IPV4) {
		if ((err & (1 << I40E_RX_DESC_ERROR_IPE_SHIFT)) != 0) {
			itrq->itrq_rxstat.irxs_hck_iperr.value.ui64++;
		} else {
			itrq->itrq_rxstat.irxs_hck_v4hdrok.value.ui64++;
			cksum |= HCK_INNER_IPV4_HDRCKSUM_OK;
		}
	}

	/*
	 * Determine if we have a valid outer L4 checksum. The only supported L4
	 * checksums are TCP, SCTP, and UDP. If this is a UDP tunneled packet,
	 * then there is no support for the outer L4 unless we are on the X722
	 * MAC. However, we do not support that at this time.
	 */
	if (i40e_rx_ptype_nontunnel_ol4(&pinfo)) {
		if ((err & (1 << I40E_RX_DESC_ERROR_L4E_SHIFT)) != 0) {
			itrq->itrq_rxstat.irxs_hck_l4err.value.ui64++;
		} else {
			itrq->itrq_rxstat.irxs_hck_l4hdrok.value.ui64++;
			cksum |= HCK_FULLCKSUM_OK;
		}
	}

	if (i40e_rx_ptype_tunnel_il4(&pinfo)) {
		if ((err & (1 << I40E_RX_DESC_ERROR_L4E_SHIFT)) != 0) {
			itrq->itrq_rxstat.irxs_hck_l4err.value.ui64++;
		} else {
			itrq->itrq_rxstat.irxs_hck_l4hdrok.value.ui64++;
			cksum |= HCK_INNER_FULLCKSUM_OK;
		}
	}

done:
	if (cksum != 0) {
		itrq->itrq_rxstat.irxs_hck_set.value.ui64++;
		mac_hcksum_set(mp, 0, 0, 0, 0, cksum);
	} else {
		itrq->itrq_rxstat.irxs_hck_miss.value.ui64++;
	}
}

mblk_t *
i40e_ring_rx(i40e_trqpair_t *itrq, int poll_bytes)
{
	i40e_t *i40e;
	i40e_hw_t *hw;
	i40e_rx_data_t *rxd;
	uint32_t cur_head;
	i40e_rx_desc_t *cur_desc;
	i40e_rx_control_block_t *rcb;
	uint64_t rx_bytes, rx_frames;
	uint64_t stword;
	mblk_t *mp, *mp_head, **mp_tail;

	ASSERT(MUTEX_HELD(&itrq->itrq_rx_lock));
	rxd = itrq->itrq_rxdata;
	i40e = itrq->itrq_i40e;
	hw = &i40e->i40e_hw_space;

	if (!(i40e->i40e_state & I40E_STARTED) ||
	    (i40e->i40e_state & I40E_OVERTEMP) ||
	    (i40e->i40e_state & I40E_SUSPENDED) ||
	    (i40e->i40e_state & I40E_ERROR))
		return (NULL);

	/*
	 * Before we do anything else, we have to make sure that all of the DMA
	 * buffers are synced up and then check to make sure that they're
	 * actually good from an FM perspective.
	 */
	I40E_DMA_SYNC(&rxd->rxd_desc_area, DDI_DMA_SYNC_FORKERNEL);
	if (i40e_check_dma_handle(rxd->rxd_desc_area.dmab_dma_handle) !=
	    DDI_FM_OK) {
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
		return (NULL);
	}

	/*
	 * Prepare our stats. We do a limited amount of processing in both
	 * polling and interrupt context. The limit in interrupt context is
	 * based on frames, in polling context based on bytes.
	 */
	rx_bytes = rx_frames = 0;
	mp_head = NULL;
	mp_tail = &mp_head;

	/*
	 * At this point, the descriptor ring is available to check. We'll try
	 * and process until we either run out of poll_bytes or descriptors.
	 */
	cur_head = rxd->rxd_desc_next;
	cur_desc = &rxd->rxd_desc_ring[cur_head];
	stword = LE64_TO_CPU(cur_desc->wb.qword1.status_error_len);

	/*
	 * Note, the primary invariant of this loop should be that cur_head,
	 * cur_desc, and stword always point to the currently processed
	 * descriptor. When we leave the loop, it should point to a descriptor
	 * that HAS NOT been processed. Meaning, that if we haven't consumed the
	 * frame, the descriptor should not be advanced.
	 */
	while ((stword & (1 << I40E_RX_DESC_STATUS_DD_SHIFT)) != 0) {
		uint32_t error, eop, plen, ptype;

		/*
		 * The DD, PLEN, and EOP bits are the only ones that are valid
		 * in every frame. The error information is only valid when EOP
		 * is set in the same frame.
		 *
		 * At this time, because we don't do any LRO or header
		 * splitting. We expect that every frame should have EOP set in
		 * it. When later functionality comes in, we'll want to
		 * re-evaluate this.
		 */
		eop = stword & (1 << I40E_RX_DESC_STATUS_EOF_SHIFT);
		VERIFY(eop != 0);

		error = (stword & I40E_RXD_QW1_ERROR_MASK) >>
		    I40E_RXD_QW1_ERROR_SHIFT;
		if (error & I40E_RX_ERR_BITS) {
			itrq->itrq_rxstat.irxs_rx_desc_error.value.ui64++;
			goto discard;
		}

		plen = (stword & I40E_RXD_QW1_LENGTH_PBUF_MASK) >>
		    I40E_RXD_QW1_LENGTH_PBUF_SHIFT;

		ptype = (stword & I40E_RXD_QW1_PTYPE_MASK) >>
		    I40E_RXD_QW1_PTYPE_SHIFT;

		/*
		 * This packet contains valid data. We should check to see if
		 * we're actually going to consume it based on its length (to
		 * ensure that we don't overshoot our quota). We determine
		 * whether to bcopy or bind the DMA resources based on the size
		 * of the frame. However, if on debug, we allow it to be
		 * overridden for testing purposes.
		 *
		 * We should be smarter about this and do DMA binding for
		 * larger frames, but for now, it's really more important that
		 * we actually just get something simple working.
		 */

		/*
		 * Ensure we don't exceed our polling quota by reading this
		 * frame. Note we only bump bytes now, we bump frames later.
		 */
		if ((poll_bytes != I40E_POLL_NULL) &&
		    (rx_bytes + plen) > poll_bytes)
			break;
		rx_bytes += plen;

		mp = NULL;
		if (plen >= i40e->i40e_rx_dma_min)
			mp = i40e_rx_bind(itrq, rxd, cur_head, plen);
		if (mp == NULL)
			mp = i40e_rx_copy(itrq, rxd, cur_head, plen);

		if (mp != NULL) {
			if (i40e->i40e_rx_hcksum_enable)
				i40e_rx_hcksum(itrq, mp, stword, error, ptype);
			*mp_tail = mp;
			mp_tail = &mp->b_next;
		}

		/*
		 * Now we need to prepare this frame for use again. See the
		 * discussion in the big theory statements.
		 *
		 * However, right now we're doing the simple version of this.
		 * Normally what we'd do would depend on whether or not we were
		 * doing DMA binding or bcopying. But because we're always doing
		 * bcopying, we can just always use the current index as a key
		 * for what to do and reassign the buffer based on the ring.
		 */
discard:
		rcb = rxd->rxd_work_list[cur_head];
		cur_desc->read.pkt_addr =
		    CPU_TO_LE64((uintptr_t)rcb->rcb_dma.dmab_dma_address);
		cur_desc->read.hdr_addr = 0;

		/*
		 * Finally, update our loop invariants.
		 */
		cur_head = i40e_next_desc(cur_head, 1, rxd->rxd_ring_size);
		cur_desc = &rxd->rxd_desc_ring[cur_head];
		stword = LE64_TO_CPU(cur_desc->wb.qword1.status_error_len);

		/*
		 * To help provide liveness, we limit the amount of data that
		 * we'll end up counting. Note that in these cases, an interrupt
		 * is not dissimilar from a polling request.
		 */
		rx_frames++;
		if (rx_frames > i40e->i40e_rx_limit_per_intr) {
			itrq->itrq_rxstat.irxs_rx_intr_limit.value.ui64++;
			break;
		}
	}

	/*
	 * As we've modified the ring, we need to make sure that we sync the
	 * descriptor ring for the device. Next, we update the hardware and
	 * update our notion of where the head for us to read from hardware is
	 * next.
	 */
	I40E_DMA_SYNC(&rxd->rxd_desc_area, DDI_DMA_SYNC_FORDEV);
	if (i40e_check_dma_handle(rxd->rxd_desc_area.dmab_dma_handle) !=
	    DDI_FM_OK) {
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
	}

	if (rx_frames != 0) {
		uint32_t tail;
		ddi_acc_handle_t rh = i40e->i40e_osdep_space.ios_reg_handle;
		rxd->rxd_desc_next = cur_head;
		tail = i40e_prev_desc(cur_head, 1, rxd->rxd_ring_size);

		I40E_WRITE_REG(hw, I40E_QRX_TAIL(itrq->itrq_index), tail);
		if (i40e_check_acc_handle(rh) != DDI_FM_OK) {
			ddi_fm_service_impact(i40e->i40e_dip,
			    DDI_SERVICE_DEGRADED);
			atomic_or_32(&i40e->i40e_state, I40E_ERROR);
		}

		itrq->itrq_rxstat.irxs_bytes.value.ui64 += rx_bytes;
		itrq->itrq_rxstat.irxs_packets.value.ui64 += rx_frames;
	}

#ifdef DEBUG
	if (rx_frames == 0) {
		ASSERT(rx_bytes == 0);
	}
#endif

	return (mp_head);
}

/*
 * This function is called by the GLDv3 when it wants to poll on a ring. The
 * only primary difference from when we call this during an interrupt is that we
 * have a limit on the number of bytes that we should consume.
 */
mblk_t *
i40e_ring_rx_poll(void *arg, int poll_bytes)
{
	i40e_trqpair_t *itrq = arg;
	mblk_t *mp;

	ASSERT(poll_bytes > 0);
	if (poll_bytes == 0)
		return (NULL);

	mutex_enter(&itrq->itrq_rx_lock);
	mp = i40e_ring_rx(itrq, poll_bytes);
	mutex_exit(&itrq->itrq_rx_lock);

	return (mp);
}

/*
 * This is a structure I wish someone would fill out for me for dorking with the
 * checksums. When we get some more experience with this, we should go ahead and
 * consider adding this to MAC.
 */
typedef enum mac_ether_offload_flags {
	MEOI_L2INFO_SET		= 0x01,
	MEOI_VLAN_TAGGED	= 0x02,
	MEOI_L3INFO_SET		= 0x04,
	MEOI_L3CKSUM_SET	= 0x08,
	MEOI_L4INFO_SET		= 0x10,
	MEOI_L4CKSUM_SET	= 0x20,
	MEOI_TUNNEL_INFO_SET	= 0x40,
} mac_ether_offload_flags_t;

#define	MEOI_L2_L3_L4	(MEOI_L2INFO_SET | MEOI_L3INFO_SET | MEOI_L4INFO_SET)

typedef struct mac_ether_offload_info {
	mac_ether_offload_flags_t	meoi_flags;
	uint8_t		meoi_l2hlen;	/* How long is the Ethernet header? */
	uint16_t	meoi_l3proto;	/* What's the Ethertype */
	uint8_t		meoi_l3hlen;	/* How long is the header? */
	uint8_t		meoi_l4proto;	/* What is the payload type? */
	uint8_t		meoi_l4hlen;	/* How long is the L4 header */
	/*
	 * The following members are used when tunneling (e.g. vxlan)
	 */
	uint8_t		meoi_tun_protlen; /* Length of the tunnel protocol */
	uint8_t		meoi_tun_l2hlen;  /* How long is the Ethernet header? */
	uint16_t	meoi_tun_l3proto; /* What's the Ethertype */
	uint8_t		meoi_tun_l3hlen;  /* How long is the header? */
	uint8_t		meoi_tun_l4proto; /* What is the payload type? */
	uint8_t		meoi_tun_l4hlen;  /* How long is the L4 header */
	/*
	 * The following members are currently not used
	 */
	mblk_t		*meoi_l3ckmp;	/* Which mblk has the l3 checksum */
	off_t		meoi_l3ckoff;	/* What's the offset to it */
	mblk_t		*meoi_l4ckmp;	/* Which mblk has the L4 checksum */
	off_t		meoi_l4ckoff;	/* What is the offset to it? */
} mac_ether_offload_info_t;

/*
 * This is something that we'd like to make a general MAC function. Before we do
 * that, we should add support for TSO.
 *
 * We should really keep track of our offset and not walk everything every
 * time. I can't imagine that this will be kind to us at high packet rates;
 * however, for the moment, let's leave that.
 *
 * This walks a message block chain without pulling up to fill in the context
 * information. Note that the data we care about could be hidden across more
 * than one mblk_t.
 */
static int
i40e_meoi_get_uint8(mblk_t *mp, off_t off, uint8_t *out)
{
	size_t mpsize;
	uint8_t *bp;

	mpsize = msgsize(mp);
	/* Check for overflow */
	if (off + sizeof (uint16_t) > mpsize)
		return (-1);

	mpsize = MBLKL(mp);
	while (off >= mpsize) {
		mp = mp->b_cont;
		off -= mpsize;
		mpsize = MBLKL(mp);
	}

	bp = mp->b_rptr + off;
	*out = *bp;
	return (0);

}

static int
i40e_meoi_get_uint16(mblk_t *mp, off_t off, uint16_t *out)
{
	size_t mpsize;
	uint8_t *bp;

	mpsize = msgsize(mp);
	/* Check for overflow */
	if (off + sizeof (uint16_t) > mpsize)
		return (-1);

	mpsize = MBLKL(mp);
	while (off >= mpsize) {
		mp = mp->b_cont;
		off -= mpsize;
		mpsize = MBLKL(mp);
	}

	/*
	 * Data is in network order. Note the second byte of data might be in
	 * the next mp.
	 */
	bp = mp->b_rptr + off;
	*out = *bp << 8;
	if (off + 1 == mpsize) {
		mp = mp->b_cont;
		bp = mp->b_rptr;
	} else {
		bp++;
	}

	*out |= *bp;
	return (0);

}

static int
mac_ether_offload_info(mblk_t *mp, mac_ether_offload_info_t *meoi,
    uint32_t ttype, size_t starting_off)
{
	size_t off;
	uint16_t ether;
	uint8_t ipproto, iplen, l4len, maclen;

	bzero(meoi, sizeof (mac_ether_offload_info_t));

	if (ttype != TTYPE_NONE && ttype != TTYPE_VXLAN)
		return (-1);

	off = offsetof(struct ether_header, ether_type) + starting_off;
	if (i40e_meoi_get_uint16(mp, off, &ether) != 0)
		return (-1);

	if (ether == ETHERTYPE_VLAN) {
		off = offsetof(struct ether_vlan_header, ether_type) +
		    starting_off;
		if (i40e_meoi_get_uint16(mp, off, &ether) != 0)
			return (-1);
		meoi->meoi_flags |= MEOI_VLAN_TAGGED;
		maclen = sizeof (struct ether_vlan_header);
	} else {
		maclen = sizeof (struct ether_header);
	}
	meoi->meoi_flags |= MEOI_L2INFO_SET;
	meoi->meoi_l2hlen = maclen;
	meoi->meoi_l3proto = ether;

	switch (ether) {
	case ETHERTYPE_IP:
		/*
		 * For IPv4 we need to get the length of the header, as it can
		 * be variable.
		 */
		off = offsetof(ipha_t, ipha_version_and_hdr_length) + maclen +
		    starting_off;
		if (i40e_meoi_get_uint8(mp, off, &iplen) != 0)
			return (-1);
		iplen &= 0x0f;
		if (iplen < 5 || iplen > 0x0f)
			return (-1);
		iplen *= 4;
		off = offsetof(ipha_t, ipha_protocol) + maclen + starting_off;
		if (i40e_meoi_get_uint8(mp, off, &ipproto) == -1)
			return (-1);
		break;
	case ETHERTYPE_IPV6:
		iplen = 40;
		off = offsetof(ip6_t, ip6_nxt) + maclen + starting_off;
		if (i40e_meoi_get_uint8(mp, off, &ipproto) == -1)
			return (-1);
		break;
	default:
		return (0);
	}
	meoi->meoi_l3hlen = iplen;
	meoi->meoi_l4proto = ipproto;
	meoi->meoi_flags |= MEOI_L3INFO_SET;

	switch (ipproto) {
	case IPPROTO_TCP:
		off = offsetof(tcph_t, th_offset_and_rsrvd) + maclen + iplen +
		    starting_off;
		if (i40e_meoi_get_uint8(mp, off, &l4len) == -1)
			return (-1);
		l4len = (l4len & 0xf0) >> 4;
		if (l4len < 5 || l4len > 0xf)
			return (-1);
		l4len *= 4;
		break;
	case IPPROTO_UDP:
		l4len = sizeof (struct udphdr);
		break;
	case IPPROTO_SCTP:
		l4len = sizeof (sctp_hdr_t);
		break;
	default:
		return (0);
	}

	meoi->meoi_l4hlen = l4len;
	meoi->meoi_flags |= MEOI_L4INFO_SET;

	if (ttype == TTYPE_VXLAN) {
		/*
		 * Recursively call ourselves to obtain the tunneled L2/L3/L4
		 * data, using the proper starting offset to the tunneled
		 * packet.
		 */
		int ret;
		mac_ether_offload_info_t meo;

		/* We currently only support tunneling within UDP */
		if (ipproto != IPPROTO_UDP)
			return (-1);

		ASSERT(starting_off == 0);
		off = maclen + iplen + l4len + VXLAN_HDR_LEN;
		ret = mac_ether_offload_info(mp, &meo, TTYPE_NONE, off);
		if (ret != 0)
			return (ret);

		if ((meo.meoi_flags & MEOI_L2_L3_L4) != MEOI_L2_L3_L4)
			return (-1);

		meoi->meoi_tun_protlen = VXLAN_HDR_LEN;
		meoi->meoi_tun_l2hlen = meo.meoi_l2hlen;
		meoi->meoi_tun_l3proto = meo.meoi_l3proto;
		meoi->meoi_tun_l3hlen = meo.meoi_l3hlen;
		meoi->meoi_tun_l4proto = meo.meoi_l4proto;
		meoi->meoi_tun_l4hlen = meo.meoi_l4hlen;

		meoi->meoi_flags |= MEOI_TUNNEL_INFO_SET;
	}

	return (0);
}

/*
 * Attempt to put togther the information we'll need to feed into a descriptor
 * to properly program the hardware for checksum offload as well as the
 * generally required flags.
 *
 * The i40e_tx_context_t`itc_data_cmdflags contains the set of flags we need to
 * 'or' into the descriptor based on the checksum flags for this mblk_t and the
 * actual information we care about.
 *
 * If we're using LSO or need to perform tunneling-based checksums, then we'll
 * fill in information that will be used for the Transmit Context Descriptor.
 */
static int
i40e_tx_context(i40e_t *i40e, i40e_trqpair_t *itrq, mblk_t *mp,
    i40e_tx_context_t *tctx)
{
	int ret;
	uint32_t chkflags, start, mss, lsoflags, ttype;
	mac_ether_offload_info_t meo;
	i40e_txq_stat_t *txs = &itrq->itrq_txstat;
	boolean_t tunneled;

	bzero(tctx, sizeof (i40e_tx_context_t));

	if (i40e->i40e_tx_hcksum_enable != B_TRUE)
		return (0);

	mac_hcksum_get(mp, &start, NULL, NULL, NULL, &chkflags);
	mac_lso_get(mp, &mss, &lsoflags);
	mac_tunnel_type_get(mp, &ttype);

	if (chkflags == 0 && lsoflags == 0)
		return (0);

	/*
	 * Have we been asked to perform an inner checksum? This implies a
	 * tunneled packet.
	 */
	tunneled = (chkflags &
	    (HCK_INNER_IPV4_HDRCKSUM_NEEDED | HCK_INNER_PSEUDO_NEEDED)) != 0;
	if (tunneled && ttype != TTYPE_VXLAN) {
		/* XXX kstat */
		return (-1);
	}

	tctx->itc_ctx_tunneled = tunneled;

	if ((ret = mac_ether_offload_info(mp, &meo, ttype, 0)) != 0) {
		txs->itxs_hck_meoifail.value.ui64++;
		return (ret);
	}

	/*
	 * Tunneling:
	 * See Table 8-21
	 *	EIPT = 11b	calc. outer IP checksum
	 *	IIPT = 11b	calc. inner IP checksum
	 *	L4TUNT = 01b	UDP/GRE tunneling
	 *	EIPLEN		set to outer IP header len.
	 *	L4TUNLEN	set to L4 tunnel len. see table 8-21 and the
	 *			"IP -> GRE / UDP -> MAC (with/without VLAN)
	 *			-> IP -> L4" entry. This is the UDP & VXLAN
	 * 			header len. up to the inner IP header.
	 *
	 * See Figure 8-9
	 * Packet Headers and descriptor length fields:
	 * Outer MAC | External IP | UDP | Inner MAC | Inner IP | L4	  | data
	 * <-MACLEN-> <---EIPLEN--> <----L4TUNLEN---> <-IPLEN--> <-L4LEN->
	 */
	if (tunneled) {
		/*
		 * Setup to calculate the 3 possible requested HW checksum(s)
		 * for the tunnel:
		 *    Inner IPv4 checksum if IIPT = 11b
		 *    Outer IPv4 checksum if EIPT = 11b
		 *    L4 checksum if L4LEN is meaningful
		 */
		uint8_t eipt;
		uint_t l4tunlen;

		/*
		 * The MAC ether offload logic should have verified that we have
		 * the right information for calculating the checksums here.
		 * Make sure that this is the case.
		 */
		if ((meo.meoi_flags & MEOI_L2INFO_SET) == 0) {
			txs->itxs_hck_nol2info.value.ui64++;
			return (-1);
		}
		if ((meo.meoi_flags & MEOI_L3INFO_SET) == 0) {
			txs->itxs_hck_nol3info.value.ui64++;
			return (-1);
		}

		if ((meo.meoi_flags & MEOI_L4INFO_SET) == 0 ||
		    meo.meoi_l4proto != IPPROTO_UDP) {
			txs->itxs_hck_badl4.value.ui64++;
			return (-1);
		}

		if ((meo.meoi_flags & MEOI_TUNNEL_INFO_SET) == 0) {
			/* XXX Missing kstat */
			return (-1);
		}

		if ((chkflags & HCK_PARTIALCKSUM) != 0) {
			/*
			 * There is no HW support for outer checksum other than
			 * the (outer) HCK_IPV4_HDRCKSUM.
			 * XXX missing kstat
			 */
			return (-1);
		}

		/*
		 * First fill in the descriptors for the tunneling extensions.
		 */
		l4tunlen = meo.meoi_l4hlen + meo.meoi_tun_l2hlen +
		    meo.meoi_tun_protlen;;

		/* outer IP */
		if (chkflags & HCK_IPV4_HDRCKSUM) {
			if (meo.meoi_l3proto == ETHERTYPE_IP) {
				eipt = I40E_TX_DESC_TNL_EIPT_IPV4_CSUM;
			} else {
				txs->itxs_hck_badl3.value.ui64++;
				return (-1);
			}
		} else {
			if (meo.meoi_l3proto == ETHERTYPE_IP) {
				eipt = I40E_TX_DESC_TNL_EIPT_IPV4;
			} else if (meo.meoi_l3proto == ETHERTYPE_IPV6) {
				eipt = I40E_TX_DESC_TNL_EIPT_IPV6;
			} else {
				txs->itxs_hck_badl3.value.ui64++;
				return (-1);
			}
		}

		tctx->itc_ctx_tunnel_fld =
		    I40E_TXD_TNL_SET_EIPT(eipt) |
		    I40E_TXD_TNL_SET_EIPLEN(meo.meoi_l3hlen >> 2) |
		    I40E_TXD_TNL_SET_L4TUNT(I40E_TX_DESC_TNL_L4TUNT_UDP) |
		    I40E_TXD_TNL_SET_L4TUNLEN(l4tunlen >> 1) |
		    I40E_TXD_TNL_SET_DECTTL(0);

		/* The MAC len is for the outer, irregardless of tunneling */
		tctx->itc_data_offsets |= (meo.meoi_l2hlen >> 1) <<
		    I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

		/* inner IP */
		if (chkflags & HCK_INNER_IPV4_HDRCKSUM_NEEDED) {
			/* When tunneled, IIPT applies to the inner IP (L3) */
			if (meo.meoi_tun_l3proto != ETHERTYPE_IP) {
				txs->itxs_hck_badl3.value.ui64++;
				return (-1);
			}
			tctx->itc_data_cmdflags |=
			    I40E_TX_DESC_CMD_IIPT_IPV4_CSUM;
		} else {
			if (meo.meoi_l3proto == ETHERTYPE_IP) {
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_IIPT_IPV4;
			} else if (meo.meoi_l3proto == ETHERTYPE_IPV6) {
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_IIPT_IPV6;
			} else {
				txs->itxs_hck_badl3.value.ui64++;
				return (-1);
			}
		}

		/* set the inner IP header length */
		tctx->itc_data_offsets |= (meo.meoi_tun_l3hlen >> 2) <<
		    I40E_TX_DESC_LENGTH_IPLEN_SHIFT;

		if (chkflags & HCK_INNER_PSEUDO_NEEDED) {
			/* L4T */
			switch (meo.meoi_tun_l4proto) {
			case IPPROTO_TCP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_TCP;
				break;
			case IPPROTO_UDP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_UDP;
				break;
			case IPPROTO_SCTP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_SCTP;
				break;
			default:
				txs->itxs_hck_badl4.value.ui64++;
				return (-1);
			}

			/* setting L4LEN initiates inner L4 HW checksum */
			tctx->itc_data_offsets |= (meo.meoi_tun_l4hlen >> 2) <<
			    I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		}

	} else {
		/* Not tunneled */

		/*
		 * Have we been asked to checksum an IPv4 header. If so, verify
		 * that we have sufficient information and then set the proper
		 * fields in the command structure.
		 */
		if (chkflags & HCK_IPV4_HDRCKSUM) {
			if ((meo.meoi_flags & MEOI_L2INFO_SET) == 0) {
				txs->itxs_hck_nol2info.value.ui64++;
				return (-1);
			}
			if ((meo.meoi_flags & MEOI_L3INFO_SET) == 0) {
				txs->itxs_hck_nol3info.value.ui64++;
				return (-1);
			}
			if (meo.meoi_l3proto != ETHERTYPE_IP) {
				txs->itxs_hck_badl3.value.ui64++;
				return (-1);
			}
			tctx->itc_data_cmdflags |=
			    I40E_TX_DESC_CMD_IIPT_IPV4_CSUM;

			tctx->itc_data_offsets |= (meo.meoi_l2hlen >> 1) <<
			    I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

			tctx->itc_data_offsets |=
			    (meo.meoi_l3hlen >> 2) <<
			    I40E_TX_DESC_LENGTH_IPLEN_SHIFT;
		}

		/*
		 * Have we been asked to provide an L4 header. If so, first,
		 * set up the IP information in the descriptor if we haven't
		 * already, before moving on to seeing if we have enough
		 * information for the L4 checksum offload.
		 */
		if (chkflags & HCK_PARTIALCKSUM) {
			if ((meo.meoi_flags & MEOI_L4INFO_SET) == 0) {
				txs->itxs_hck_nol4info.value.ui64++;
				return (-1);
			}

			if (!(chkflags & HCK_IPV4_HDRCKSUM)) {
				if ((meo.meoi_flags & MEOI_L2INFO_SET) == 0) {
					txs->itxs_hck_nol2info.value.ui64++;
					return (-1);
				}
				if ((meo.meoi_flags & MEOI_L3INFO_SET) == 0) {
					txs->itxs_hck_nol3info.value.ui64++;
					return (-1);
				}

				if (meo.meoi_l3proto == ETHERTYPE_IP) {
					tctx->itc_data_cmdflags |=
					    I40E_TX_DESC_CMD_IIPT_IPV4;
				} else if (meo.meoi_l3proto == ETHERTYPE_IPV6) {
					tctx->itc_data_cmdflags |=
					    I40E_TX_DESC_CMD_IIPT_IPV6;
				} else {
					txs->itxs_hck_badl3.value.ui64++;
					return (-1);
				}
				tctx->itc_data_offsets |=
				    (meo.meoi_l2hlen >> 1) <<
				    I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

				tctx->itc_data_offsets |=
				    (meo.meoi_l3hlen >> 2) <<
				    I40E_TX_DESC_LENGTH_IPLEN_SHIFT;
			}

			switch (meo.meoi_l4proto) {
			case IPPROTO_TCP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_TCP;
				break;
			case IPPROTO_UDP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_UDP;
				break;
			case IPPROTO_SCTP:
				tctx->itc_data_cmdflags |=
				    I40E_TX_DESC_CMD_L4T_EOFT_SCTP;
				break;
			default:
				txs->itxs_hck_badl4.value.ui64++;
				return (-1);
			}

			tctx->itc_data_offsets |= (meo.meoi_l4hlen >> 2) <<
			    I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		}
	}

	if (lsoflags & HW_LSO) {
		/*
		 * LSO requires that checksum offloads are enabled.  If for
		 * some reason they're not we bail out with an error.
		 */
		if (!((chkflags & HCK_IPV4_HDRCKSUM) &&
		    (chkflags & HCK_PARTIALCKSUM))) {
			return (-1);
		}
		tctx->itc_ctx_cmdflags |= I40E_TX_CTX_DESC_TSO;
		tctx->itc_ctx_mss = mss;
		tctx->itc_ctx_tsolen = msgsize(mp) -
		    (meo.meoi_l2hlen + meo.meoi_l3hlen + meo.meoi_l4hlen);
	}

	return (0);
}

static void
i40e_tcb_free(i40e_trqpair_t *itrq, i40e_tx_control_block_t *tcb)
{
	ASSERT(tcb != NULL);

	mutex_enter(&itrq->itrq_tcb_lock);
	ASSERT(itrq->itrq_tcb_free < itrq->itrq_tx_free_list_size);
	itrq->itrq_tcb_free_list[itrq->itrq_tcb_free] = tcb;
	itrq->itrq_tcb_free++;
	mutex_exit(&itrq->itrq_tcb_lock);
}

static i40e_tx_control_block_t *
i40e_tcb_alloc(i40e_trqpair_t *itrq)
{
	i40e_tx_control_block_t *ret;

	mutex_enter(&itrq->itrq_tcb_lock);
	if (itrq->itrq_tcb_free == 0) {
		mutex_exit(&itrq->itrq_tcb_lock);
		return (NULL);
	}

	itrq->itrq_tcb_free--;
	ret = itrq->itrq_tcb_free_list[itrq->itrq_tcb_free];
	itrq->itrq_tcb_free_list[itrq->itrq_tcb_free] = NULL;
	mutex_exit(&itrq->itrq_tcb_lock);

	ASSERT(ret != NULL);
	return (ret);
}

/*
 * This should be used to free any DMA resources, associated mblk_t's, etc. It's
 * used as part of recycling the message blocks when we have either an interrupt
 * or other activity that indicates that we need to take a look.
 */
static void
i40e_tcb_reset(i40e_tx_control_block_t *tcb)
{
	int i;

	switch (tcb->tcb_type) {
	case I40E_TX_COPY:
		tcb->tcb_dma.dmab_len = 0;
		break;
	case I40E_TX_DMA:
		if (tcb->tcb_used_lso == B_TRUE)
			(void) ddi_dma_unbind_handle(tcb->tcb_lso_dma_handle);
		else
			(void) ddi_dma_unbind_handle(tcb->tcb_dma_handle);
		if (tcb->tcb_bind_info != NULL) {
			for (i = 0; i < tcb->tcb_bind_ncookies; i++) {
				kmem_free(tcb->tcb_bind_info[i],
				    sizeof (struct i40e_dma_bind_info));
			}
			kmem_free(tcb->tcb_bind_info,
			    tcb->tcb_bind_ncookies *
			    sizeof (struct i40e_dma_bind_info *));
		}
		tcb->tcb_bind_info = NULL;
		tcb->tcb_bind_ncookies = 0;
		tcb->tcb_used_lso = B_FALSE;
		break;
	case I40E_TX_DESC:
		break;
	case I40E_TX_NONE:
		/* Cast to pacify lint */
		panic("trying to free tcb %p with bad type none", (void *)tcb);
	default:
		panic("unknown i40e tcb type: %d", tcb->tcb_type);
	}

	tcb->tcb_type = I40E_TX_NONE;
	if (tcb->tcb_mp != NULL) {
		freemsg(tcb->tcb_mp);
		tcb->tcb_mp = NULL;
	}
	tcb->tcb_next = NULL;
}

/*
 * This is called as part of shutting down to clean up all outstanding
 * descriptors. Similar to recycle, except we don't re-arm anything and instead
 * just return control blocks to the free list.
 */
void
i40e_tx_cleanup_ring(i40e_trqpair_t *itrq)
{
	uint32_t index;

	ASSERT(MUTEX_HELD(&itrq->itrq_tx_lock));
	ASSERT(itrq->itrq_desc_free <= itrq->itrq_tx_ring_size);

	/*
	 * Because we should have shut down the chip at this point, it should be
	 * safe to just clean up all the entries between our head and tail.
	 */
#ifdef	DEBUG
	index = I40E_READ_REG(&itrq->itrq_i40e->i40e_hw_space,
	    I40E_QTX_ENA(itrq->itrq_index));
	VERIFY0(index & (I40E_QTX_ENA_QENA_REQ_MASK |
	    I40E_QTX_ENA_QENA_STAT_MASK));
#endif

	index = itrq->itrq_desc_head;
	while (itrq->itrq_desc_free < itrq->itrq_tx_ring_size) {
		i40e_tx_control_block_t *tcb;

		tcb = itrq->itrq_tcb_work_list[index];
		VERIFY(tcb != NULL);
		itrq->itrq_tcb_work_list[index] = NULL;
		i40e_tcb_reset(tcb);
		i40e_tcb_free(itrq, tcb);

		bzero(&itrq->itrq_desc_ring[index], sizeof (i40e_tx_desc_t));
		index = i40e_next_desc(index, 1, itrq->itrq_tx_ring_size);
		itrq->itrq_desc_free++;
	}

	ASSERT(index == itrq->itrq_desc_tail);
	itrq->itrq_desc_head = index;
}

/*
 * We're here either by hook or by crook. We need to see if there are transmit
 * descriptors available for us to go and clean up and return to the hardware.
 * We may also be blocked, and if so, we should make sure that we let it know
 * we're good to go.
 */
void
i40e_tx_recycle_ring(i40e_trqpair_t *itrq)
{
	uint32_t wbhead, toclean, count;
	i40e_tx_control_block_t *tcbhead;
	i40e_t *i40e = itrq->itrq_i40e;
	int desc_per_tcb, i;

	mutex_enter(&itrq->itrq_tx_lock);

	ASSERT(itrq->itrq_desc_free <= itrq->itrq_tx_ring_size);
	if (itrq->itrq_desc_free == itrq->itrq_tx_ring_size) {
		if (itrq->itrq_tx_blocked == B_TRUE) {
			itrq->itrq_tx_blocked = B_FALSE;
			mac_tx_ring_update(i40e->i40e_mac_hdl,
			    itrq->itrq_mactxring);
			itrq->itrq_txstat.itxs_num_unblocked.value.ui64++;
		}
		mutex_exit(&itrq->itrq_tx_lock);
		return;
	}

	/*
	 * Now we need to try and see if there's anything available. The driver
	 * will write to the head location and it guarantees that it does not
	 * use relaxed ordering.
	 */
	VERIFY0(ddi_dma_sync(itrq->itrq_desc_area.dmab_dma_handle,
	    (uintptr_t)itrq->itrq_desc_wbhead,
	    sizeof (uint32_t), DDI_DMA_SYNC_FORKERNEL));

	if (i40e_check_dma_handle(itrq->itrq_desc_area.dmab_dma_handle) !=
	    DDI_FM_OK) {
		mutex_exit(&itrq->itrq_tx_lock);
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
		return;
	}

	wbhead = *itrq->itrq_desc_wbhead;
	toclean = itrq->itrq_desc_head;
	count = 0;
	tcbhead = NULL;

	while (toclean != wbhead) {
		i40e_tx_control_block_t *tcb;

		tcb = itrq->itrq_tcb_work_list[toclean];
		itrq->itrq_tcb_work_list[toclean] = NULL;
		ASSERT(tcb != NULL);
		tcb->tcb_next = tcbhead;
		tcbhead = tcb;

		/*
		 * In the DMA bind case, there may not necessarily be a 1:1
		 * mapping between tcb's and descriptors.  If the tcb type
		 * indicates a DMA binding then check the number of DMA
		 * cookies to determine how many entries to clean in the
		 * descriptor ring.
		 */
		if (tcb->tcb_type == I40E_TX_DMA)
			desc_per_tcb = tcb->tcb_bind_ncookies;
		else
			desc_per_tcb = 1;

		for (i = 0; i < desc_per_tcb; i++) {
			/*
			 * We zero this out for sanity purposes.
			 */
			bzero(&itrq->itrq_desc_ring[toclean],
			    sizeof (i40e_tx_desc_t));
			toclean = i40e_next_desc(toclean, 1,
			    itrq->itrq_tx_ring_size);
			count++;
		}
	}

	itrq->itrq_desc_head = wbhead;
	itrq->itrq_desc_free += count;
	itrq->itrq_txstat.itxs_recycled.value.ui64 += count;
	ASSERT(itrq->itrq_desc_free <= itrq->itrq_tx_ring_size);

	if (itrq->itrq_tx_blocked == B_TRUE &&
	    itrq->itrq_desc_free > i40e->i40e_tx_block_thresh) {
		itrq->itrq_tx_blocked = B_FALSE;

		mac_tx_ring_update(i40e->i40e_mac_hdl, itrq->itrq_mactxring);
		itrq->itrq_txstat.itxs_num_unblocked.value.ui64++;
	}

	mutex_exit(&itrq->itrq_tx_lock);

	/*
	 * Now clean up the tcb.
	 */
	while (tcbhead != NULL) {
		i40e_tx_control_block_t *tcb = tcbhead;

		tcbhead = tcb->tcb_next;
		i40e_tcb_reset(tcb);
		i40e_tcb_free(itrq, tcb);
	}

	DTRACE_PROBE2(i40e__recycle, i40e_trqpair_t *, itrq, uint32_t, count);
}

static i40e_tx_control_block_t *
i40e_tx_bind_fragment(i40e_trqpair_t *itrq, const mblk_t *mp,
    boolean_t use_lso)
{
	ddi_dma_handle_t dma_handle;
	ddi_dma_cookie_t dma_cookie;
	uint_t ncookies = 0, dmaflags;
	i40e_tx_control_block_t *tcb;
	i40e_txq_stat_t *txs = &itrq->itrq_txstat;
	int i = 0;
	struct i40e_dma_bind_info *dbi;

	if ((tcb = i40e_tcb_alloc(itrq)) == NULL) {
		txs->itxs_err_notcb.value.ui64++;
		return (NULL);
	}
	tcb->tcb_type = I40E_TX_DMA;

	if (use_lso == B_TRUE)
		dma_handle = tcb->tcb_lso_dma_handle;
	else
		dma_handle = tcb->tcb_dma_handle;

	dmaflags = DDI_DMA_RDWR | DDI_DMA_STREAMING;
	if (ddi_dma_addr_bind_handle(dma_handle, NULL,
	    (caddr_t)mp->b_rptr, MBLKL(mp), dmaflags, DDI_DMA_DONTWAIT, NULL,
	    &dma_cookie, &ncookies) != DDI_DMA_MAPPED) {
		goto bffail;
	}

	tcb->tcb_bind_info =
	    kmem_zalloc(ncookies * sizeof (struct i40e_dma_bind_info *),
	    KM_NOSLEEP);
	if (tcb->tcb_bind_info == NULL)
		goto bffail;

	while (i < ncookies) {
		if (i > 0)
			ddi_dma_nextcookie(dma_handle, &dma_cookie);
		dbi = kmem_zalloc(sizeof (struct i40e_dma_bind_info),
		    KM_NOSLEEP);
		if (dbi == NULL)
			goto bffail;

		dbi->dbi_paddr = (caddr_t)dma_cookie.dmac_laddress;
		dbi->dbi_len = dma_cookie.dmac_size;
		tcb->tcb_bind_info[i++] = dbi;
	}
	tcb->tcb_bind_ncookies = ncookies;
	tcb->tcb_used_lso = use_lso;

	return (tcb);

bffail:
	i40e_tcb_reset(tcb);
	i40e_tcb_free(itrq, tcb);
	if (ncookies != 0)
		(void) ddi_dma_unbind_handle(dma_handle);
	if (tcb->tcb_bind_info != NULL)
		kmem_free(tcb->tcb_bind_info,
		    tcb->tcb_bind_ncookies *
		    sizeof (struct i40e_dma_bind_info *));
	tcb->tcb_bind_info = NULL;
	tcb->tcb_bind_ncookies = 0;

	return (NULL);
}

static void
i40e_tx_set_data_desc(i40e_trqpair_t *itrq, i40e_tx_context_t *tctx,
    struct i40e_dma_bind_info *dbi, boolean_t last_desc)
{
	i40e_tx_desc_t *txdesc;
	int type, cmd;

	ASSERT(MUTEX_HELD(&itrq->itrq_tx_lock));
	itrq->itrq_desc_free--;
	txdesc = &itrq->itrq_desc_ring[itrq->itrq_desc_tail];
	itrq->itrq_desc_tail = i40e_next_desc(itrq->itrq_desc_tail, 1,
	    itrq->itrq_tx_ring_size);

	type = I40E_TX_DESC_DTYPE_DATA;
	cmd = I40E_TX_DESC_CMD_ICRC | tctx->itc_data_cmdflags;

	/*
	 * The last data descriptor needs the EOP and RS bits set, so that the
	 * HW knows that we're ready to send.
	 */
	if (last_desc == B_TRUE) {
		cmd |= I40E_TX_DESC_CMD_EOP;
		cmd |= I40E_TX_DESC_CMD_RS;
	}

	txdesc->buffer_addr = CPU_TO_LE64((uintptr_t)dbi->dbi_paddr);
	txdesc->cmd_type_offset_bsz =
	    CPU_TO_LE64(((uint64_t)type |
	    ((uint64_t)tctx->itc_data_offsets << I40E_TXD_QW1_OFFSET_SHIFT) |
	    ((uint64_t)cmd << I40E_TXD_QW1_CMD_SHIFT) |
	    ((uint64_t)dbi->dbi_len << I40E_TXD_QW1_TX_BUF_SZ_SHIFT)));
}

/*
 * We've been asked to send a message block on the wire. We'll only have a
 * single chain. There will not be any b_next pointers; however, there may be
 * multiple b_cont blocks.
 *
 * We may do one of three things with any given mblk_t chain:
 *
 *   1) Drop it
 *   2) Transmit it
 *   3) Return it
 *
 * If we return it to MAC, then MAC will flow control on our behalf. In other
 * words, it won't send us anything until we tell it that it's okay to send us
 * something.
 */
mblk_t *
i40e_ring_tx(void *arg, mblk_t *mp)
{
	const mblk_t *nmp;
	size_t mpsize, blksz;
	i40e_tx_control_block_t *tcb_ctx = NULL, *tcb_data = NULL,
	    **tcb_dma = NULL;
	i40e_tx_desc_t *txdesc;
	i40e_tx_context_desc_t *ctxdesc;
	i40e_tx_context_t tctx;
	int cmd, type, i, c;
	uint_t needed_desc = 0, tail, nbufs = 0;
	boolean_t do_ctx_desc = B_FALSE, do_dma_bind = B_FALSE, last_desc,
	    use_lso = B_FALSE;

	i40e_trqpair_t *itrq = arg;
	i40e_t *i40e = itrq->itrq_i40e;
	i40e_hw_t *hw = &i40e->i40e_hw_space;
	i40e_txq_stat_t *txs = &itrq->itrq_txstat;

	ASSERT(mp->b_next == NULL);

	if (!(i40e->i40e_state & I40E_STARTED) ||
	    (i40e->i40e_state & I40E_OVERTEMP) ||
	    (i40e->i40e_state & I40E_SUSPENDED) ||
	    (i40e->i40e_state & I40E_ERROR) ||
	    (i40e->i40e_link_state != LINK_STATE_UP)) {
		freemsg(mp);
		return (NULL);
	}

	/*
	 * Figure out the relevant context about this frame that we might need
	 * for enabling checksum, lso, etc. This also fills in information that
	 * we might set around the packet type, etc.
	 */
	if (i40e_tx_context(i40e, itrq, mp, &tctx) < 0) {
		freemsg(mp);
		itrq->itrq_txstat.itxs_err_context.value.ui64++;
		return (NULL);
	}
	if (tctx.itc_ctx_cmdflags & I40E_TX_CTX_DESC_TSO)
		use_lso = B_TRUE;

	if ((tctx.itc_ctx_cmdflags & I40E_TX_CTX_DESC_TSO) ||
	    tctx.itc_ctx_tunneled)
		do_ctx_desc = B_TRUE;

	/*
	 * For the primordial driver we can punt on doing any recycling right
	 * now; however, longer term we need to probably do some more pro-active
	 * recycling to cut back on stalls in the tx path.
	 */

	/*
	 * Iterate through the mblks to calculate both the total size and the
	 * number of message blocks.  This is used to determine whether we're
	 * doing DMA binding and, if so, how many control blocks we'll need.
	 */
	mpsize = 0;
	for (nmp = mp; nmp != NULL; nmp = nmp->b_cont) {
		blksz = MBLKL(nmp);
		if (blksz > 0) {
			mpsize += blksz;
			nbufs++;
		}
	}

	if (do_ctx_desc == B_TRUE) {
		/*
		 * If we're doing tunneling or LSO, then we'll need a tx
		 * context descriptor in addition to one or more tx data
		 * descriptors.  Since there's no data DMA block associated
		 * with the context descriptor we create a special control
		 * block that behaves effectively like a NOP.
		 */
		if ((tcb_ctx = i40e_tcb_alloc(itrq)) == NULL) {
			txs->itxs_err_notcb.value.ui64++;
			goto txfail;
		}
		tcb_ctx->tcb_type = I40E_TX_DESC;
		needed_desc++;
	}

	/*
	 * For the non-LSO tx case, we alter our DMA strategy based on a
	 * threshold tied to the frame size.  This threshold is configurable
	 * via tx_dma_threshold property.
	 *
	 * If the frame size is above the threshold, we do DMA binding of the
	 * fragments, building a control block and data descriptor for each
	 * piece.
	 *
	 * If it's below or at the threshold then we just use a single control
	 * block and data descriptor and simply bcopy all of the fragments into
	 * the pre-allocated DMA buffer in the control block.
	 *
	 * For the LSO tx case we always to DMA binding.
	 */
	if (use_lso == B_TRUE || mpsize > i40e->i40e_tx_dma_min) {
		do_dma_bind = B_TRUE;
		tcb_dma =
		    kmem_zalloc(nbufs * sizeof (i40e_tx_control_block_t *),
		    KM_NOSLEEP);
		if (tcb_dma == NULL) {
			i40e_error(i40e, "failed to allocate tcb_dma list");
			goto txfail;
		}
		/*
		 * For each b_cont: bind the control block's DMA handle to the
		 * b_rptr, and record the cookies so that we can iterate
		 * through them and build tx data descriptors.
		 */
		for (nmp = mp, i = 0; nmp != NULL; nmp = nmp->b_cont) {
			if (MBLKL(nmp) == 0)
				continue;
			tcb_dma[i] = i40e_tx_bind_fragment(itrq, nmp, use_lso);
			if (tcb_dma[i] == NULL) {
				i40e_error(i40e, "dma bind failed!");
				goto txfail;
			}
			if (i == 0)
				tcb_dma[i]->tcb_mp = mp;
			needed_desc += tcb_dma[i++]->tcb_bind_ncookies;
		}
	} else {
		/*
		 * Just use a single control block and bcopy all of the
		 * fragments into its pre-allocated DMA buffer.
		 */
		if ((tcb_data = i40e_tcb_alloc(itrq)) == NULL) {
			txs->itxs_err_notcb.value.ui64++;
			goto txfail;
		}
		tcb_data->tcb_type = I40E_TX_COPY;

		ASSERT(tcb_data->tcb_dma.dmab_len == 0);
		ASSERT(tcb_data->tcb_dma.dmab_size >= mpsize);

		for (nmp = mp; nmp != NULL; nmp = nmp->b_cont) {
			size_t clen = MBLKL(nmp);
			void *coff = tcb_data->tcb_dma.dmab_address +
			    tcb_data->tcb_dma.dmab_len;

			bcopy(nmp->b_rptr, coff, clen);
			tcb_data->tcb_dma.dmab_len += clen;
		}
		ASSERT(tcb_data->tcb_dma.dmab_len == mpsize);
		I40E_DMA_SYNC(&tcb_data->tcb_dma, DDI_DMA_SYNC_FORDEV);

		tcb_data->tcb_mp = mp;
		needed_desc++;
	}

	mutex_enter(&itrq->itrq_tx_lock);
	if (itrq->itrq_desc_free < i40e->i40e_tx_block_thresh) {
		txs->itxs_err_nodescs.value.ui64++;
		mutex_exit(&itrq->itrq_tx_lock);
		goto txfail;
	}

	ASSERT(itrq->itrq_desc_free >= needed_desc);

	if (do_ctx_desc == B_TRUE) {
		/*
		 * If we're enabling any offloads for this frame, then we'll
		 * need to build up a transmit context descriptor, first.  The
		 * context descriptor needs to be placed in the tx ring before
		 * the data descriptor(s).  See section 8.4.2, table 8-16
		 */
		itrq->itrq_desc_free--;
		tail = itrq->itrq_desc_tail;
		ctxdesc = (i40e_tx_context_desc_t *)&itrq->itrq_desc_ring[tail];
		itrq->itrq_tcb_work_list[tail] = tcb_ctx;
		itrq->itrq_desc_tail = i40e_next_desc(tail, 1,
		    itrq->itrq_tx_ring_size);

		type = I40E_TX_DESC_DTYPE_CONTEXT;
		/* QW0 */
		if (tctx.itc_ctx_tunneled) {
			ctxdesc->tunneling_params = tctx.itc_ctx_tunnel_fld;
		} else {
			ctxdesc->tunneling_params = 0;
		}
		ctxdesc->l2tag2 = 0;

		/* QW1 */
		ctxdesc->type_cmd_tso_mss = CPU_TO_LE64((uint64_t)type);
		if (tctx.itc_ctx_cmdflags & I40E_TX_CTX_DESC_TSO) {
			ctxdesc->type_cmd_tso_mss |= CPU_TO_LE64((uint64_t)
			    ((uint64_t)tctx.itc_ctx_cmdflags <<
			    I40E_TXD_CTX_QW1_CMD_SHIFT) |
			    ((uint64_t)tctx.itc_ctx_tsolen <<
			    I40E_TXD_CTX_QW1_TSO_LEN_SHIFT) |
			    ((uint64_t)tctx.itc_ctx_mss <<
			    I40E_TXD_CTX_QW1_MSS_SHIFT));
		}
	}

	if (do_dma_bind == B_TRUE) {
		/*
		 * Next build up a transmit data descriptor for each buffer.
		 */
		last_desc = B_FALSE;
		for (i = 0; i < nbufs; i++) {
			itrq->itrq_tcb_work_list[itrq->itrq_desc_tail] =
			    tcb_dma[i];

			for (c = 0; c < tcb_dma[i]->tcb_bind_ncookies; c++) {
				if (i == (nbufs - 1) &&
				    c == (tcb_dma[i]->tcb_bind_ncookies - 1)) {
					last_desc = B_TRUE;
				}
				i40e_tx_set_data_desc(itrq, &tctx,
				    tcb_dma[i]->tcb_bind_info[c], last_desc);
			}
		}
		kmem_free(tcb_dma, nbufs * sizeof (i40e_tx_control_block_t *));
	} else {
		/*
		 * Build up the single transmit data descriptor needed for the
		 * non-DMA-bind case.
		 */
		itrq->itrq_desc_free--;
		txdesc = &itrq->itrq_desc_ring[itrq->itrq_desc_tail];
		itrq->itrq_tcb_work_list[itrq->itrq_desc_tail] = tcb_data;
		itrq->itrq_desc_tail = i40e_next_desc(itrq->itrq_desc_tail, 1,
		    itrq->itrq_tx_ring_size);

		type = I40E_TX_DESC_DTYPE_DATA;
		cmd = I40E_TX_DESC_CMD_EOP |
		    I40E_TX_DESC_CMD_RS |
		    I40E_TX_DESC_CMD_ICRC |
		    tctx.itc_data_cmdflags;
		txdesc->buffer_addr =
		    CPU_TO_LE64((uintptr_t)tcb_data->tcb_dma.dmab_dma_address);
		txdesc->cmd_type_offset_bsz = CPU_TO_LE64(((uint64_t)type |
		    ((uint64_t)tctx.itc_data_offsets <<
		    I40E_TXD_QW1_OFFSET_SHIFT) |
		    ((uint64_t)cmd << I40E_TXD_QW1_CMD_SHIFT) |
		    ((uint64_t)tcb_data->tcb_dma.dmab_len <<
		    I40E_TXD_QW1_TX_BUF_SZ_SHIFT)));
	}

	I40E_DMA_SYNC(&itrq->itrq_desc_area, DDI_DMA_SYNC_FORDEV);
	I40E_WRITE_REG(hw, I40E_QTX_TAIL(itrq->itrq_index),
	    itrq->itrq_desc_tail);

	if (i40e_check_acc_handle(i40e->i40e_osdep_space.ios_reg_handle) !=
	    DDI_FM_OK) {
		/*
		 * Note, we can't really go through and clean this up very well,
		 * because the memory has been given to the device, so just
		 * indicate it's been transmitted.
		 */
		ddi_fm_service_impact(i40e->i40e_dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&i40e->i40e_state, I40E_ERROR);
	}

	txs->itxs_bytes.value.ui64 += mpsize;
	txs->itxs_packets.value.ui64++;
	txs->itxs_descriptors.value.ui64 += needed_desc;

	mutex_exit(&itrq->itrq_tx_lock);

	return (NULL);

txfail:
	/*
	 * We ran out of resources. Return it to MAC and indicate that we'll
	 * need to signal MAC. If there are allocated tcb's, return them now.
	 * Make sure to reset their message block's, since we'll return them
	 * back to MAC.
	 */
	if (tcb_ctx != NULL) {
		tcb_ctx->tcb_mp = NULL;
		i40e_tcb_reset(tcb_ctx);
		i40e_tcb_free(itrq, tcb_ctx);
	}
	if (tcb_data != NULL) {
		tcb_data->tcb_mp = NULL;
		i40e_tcb_reset(tcb_data);
		i40e_tcb_free(itrq, tcb_data);
	}
	if (tcb_dma != NULL)
		kmem_free(tcb_dma, nbufs * sizeof (i40e_tx_control_block_t *));

	mutex_enter(&itrq->itrq_tx_lock);
	itrq->itrq_tx_blocked = B_TRUE;
	mutex_exit(&itrq->itrq_tx_lock);

	return (mp);
}
