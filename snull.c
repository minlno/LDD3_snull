#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

struct net_device *snull_devs[2];

static const struct net_device_ops snull_netdev_ops = {
	.ndo_open		= snull_open,
	.ndo_stop		= snull_release,
	.ndo_start_xmit		= snull_tx,
	.ndo_do_ioctl		= snull_ioctl,
	.ndo_set_config		= snull_config,
	.ndo_get_stats64	= snull_stats,
	.ndo_change_mtu		= snull_change_mtu,
	.ndo_tx_timeout		= snull_tx_timeout;
};

void snull_init(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &snull_netdev_ops;
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

}
module_exit(snull_cleanup);

MODULE_AUTHOR("Minho Kim");
MODULE_DESCRIPTION("LDD3 Snull");
MODULE_LICENSE("GPL v2");
