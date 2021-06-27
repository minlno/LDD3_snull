#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

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
