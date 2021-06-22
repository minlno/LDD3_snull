#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>

struct net_device *snull_devs[2];

static int __init snull_init_module(void)
{
	int i, result;

	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
			snull_init);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d",
			snull_init);
	if (snull_devs[0] == NULL || snull_devs[1] == NULL)
		goto out;

	for (i = 0; i < 2; i++) {
		if ((result = register_netdev(snull_devs[i])))
			printk("snull: error %i registering device \"%s\"\n",
					result, snull_devs[i]->name);
	}

	return 0;

out:
}
module_init(snull_init_mudule);

static void __exit snull_exit_module(void)
{

}
module_exit(snull_exit_module);

MODULE_AUTHOR("Minho Kim");
MODULE_DESCRIPTION("LDD3 Snull");
MODULE_LICENSE("GPL v2");
