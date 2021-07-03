#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

static int lockup = 0;
module_param(lockup, int, 0);

static int timeout = SNULL_TIMEOUT;
module_param(timeout, int, 0);

/* 
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */

struct snull_priv {
	struct net_device_stats stats;
	int status;
	struct snull_packet *ppool;
	struct snull_packet *rx_queue; /* List of incoming packets */
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct sk_buff *skb;
	spinlock_t lock;
	struct net_device *dev;
	struct napi_struct napi;
};

struct net_device *snull_devs[2];

/*
 * Receive a packet: retrieve, encapsulate and pass over to upper levels
 */
void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	struct sk_buff *skb;
	struct snull_priv *priv = netdev_priv(dev);

	/*
	 * The packet has been retrieved from the transmission
	 * medium. Build an skb around it, so upper laters can handle it
	 */
	skb = dev_alloc_skb(pkt->datalen + 2);
	if (!skb) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "snull rx: low on mem - packet dropped\n");
		priv->stats.rx_dropped++;
		goto out;
	}
	skb_reserve(skb, 2); /* align IP on 16B boundary */
	memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += pkt->datalen;
	netif_rx(skb);
out:
	return;
}

/*
 * The typical interrupt entry point
 */
static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;
	/*
	 * As usual, check the "device" pointer to be sure it is
	 * really interrupting,
	 * Then assign "struct device *dev"
	 */
	struct net_device *dev = (struct net_device *)dev_id;
	/* ... and check with hw if it's really ours */

	/* paranoid */
	if (!dev)
		return;

	/* Lock the device */
	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	/* retrieve statusword: real netdevices use I/O instructions */
	statusword = priv->status;
	priv->status = 0;
	if (statusword & SNULL_RX_INTR) {
		/* send it to snull_rx for handling */
		pkt = priv->rx_queue;
		if (pkt) {
			priv->rx_queue = pkt->next;
			snull_rx(dev, pkt);
		}
	}
	if (statusword & SNULL_TX_INTR) {
		/* a transmission is over: free the skb */
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);
	if (pkt) snull_release_buffer(pkt); /* Do this outside the lock! */
	return;
}

/*
 * Transmit a packet (called by the kernel)
 */
int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	char *data, shortpkt[ETH_ZLEN];
	struct snull_priv *priv = netdev_priv(dev);

	data = skb->data;
	len = skb->len;
	if (len < ETH_ZLEN) {
		memset(shortpkt, 0, ETH_ZLEN);
		memcpy(shortpkt, skb->data, skb->len);
		len = ETH_ZLEN;
		data = shortpkt;
	}
	netif_trans_update(dev);

	/* Remember the skb, so we can free it at interrupt time */
	priv->skb = skb;

	snull_hw_tx(data, len, dev);

	return 0;
}

/*
 * Deal with a transmit timeout
 */
void snull_tx_timeout(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct netdev_queue *txq = netdev_get_tx_queue(dev, 0);

	PDEBUG("Transmit timeout at %ld, latency %ld\n", jiffies,
					jiffies - txq->trans_start);
	/* Simulate a transmission interrupt to get things moving */
	priv->status |= SNULL_TX_INTR;
	snull_interrupt(0, dev, NULL);
	priv->stats.tx_errors++;

	/* Reset packet pool */
	spin_lock(&priv->lock);
	snull_teardown_pool(dev);
	snull_setup_pool(dev);
	spin_unlock(&priv->lock);

	netif_wake_queue(dev);
	return;
}

/*
 * Open and Close
 */

int snull_open(struct net_device *dev)
{
	memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
	if (dev == snull_devs[1])
		dev->dev_addr[ETH_ALEN-1]++;
	if (use_napi) {
		struct snull_priv *priv = netdev_priv(dev);
		napi_enable(&priv->napi);
	}
	netif_start_queue(dev);
	return 0;
}

int snull_release(struct net_device *dev)
{
	netif_stop_queue(dev);
	if (use_napi) {
		struct snull_priv *priv = netdev_priv(dev);
		napi_disable(&priv->napi);
	}
	return 0;
}

static const struct header_ops snull_header_ops = {
	.create			= snull_header,
};

static const struct net_device_ops snull_netdev_ops = {
	.ndo_open		= snull_open,
	.ndo_stop		= snull_release,
	.ndo_start_xmit		= snull_tx,
	.ndo_do_ioctl		= snull_ioctl,
	.ndo_set_config		= snull_config,
	.ndo_get_stats64	= snull_stats,
	.ndo_change_mtu		= snull_change_mtu,
	.ndo_tx_timeout		= snull_tx_timeout,
};

void snull_init(struct net_device *dev)
{
	struct snull_priv *priv;

	/*
	 * Assign other field in dev, using ether_setup() and some
	 * hand assignments
	 */
	ether_setup(dev);
	dev->watchdog_timeo = timeout;
	dev->netdev_ops = &snull_netdev_ops;
	dev->header_ops = &snull_header_ops;
	dev->flags		|= IFF_NOARP;
	dev->features	|= NETIF_F_HW_CSUM;

	/*
	 * Then, initialize the priv feild. This encloses the statistics
	 * and a few private fields.
	 */
	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct snull_priv));
	if (use_napi) {
		netif_napi_add(dev, &priv->napi, snull_poll, 2);
	}
	spin_lock_init(&priv->lock);
	priv->dev = dev;

	snull_rx_ints(dev, 1);
	snull_setup_pool(dev);
}

static int __init snull_init_module(void)
{
	int i, result, ret = -ENOMEM;

	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
					NET_NAME_UNKNOWN, snull_init);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
					NET_NAME_UNKNOWN, snull_init);
	if (snull_devs[0] == NULL || snull_devs[1] == NULL)
		goto out;

	ret = -ENODEV;
	for (i = 0; i < 2; i++) {
		if ((result = register_netdev(snull_devs[i])))
			printk("snull: error %i registering device \"%s\"\n",
					result, snull_devs[i]->name);
		else
			ret = 0;
	}

out:
	if (ret)
		snull_cleanup();
	return ret;
}
module_init(snull_init_module);

static void __exit snull_cleanup(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]);
			snull_teardown_pool(snull_devs[i]);
			free_netdev(snull_devs[i]);
		}
	}

	return;
}
module_exit(snull_cleanup);

MODULE_AUTHOR("Minho Kim");
MODULE_DESCRIPTION("LDD3 Snull");
MODULE_LICENSE("GPL v2");
