#undef PDEBUG
#ifdef SNULL_DEBUG
#	ifdef __KERNEL__
#		define PDEBUG(fmt, args...) printk(KERN_DEBUG "snull: " fmt, ## args)
#	else
#		define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#	endif
#else
#	define PDEBUG(fmt, args...)
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...)

#define SNULL_RX_INTR 0x0001
#define SNULL_TX_INTR 0x0002

#define SNULL_TIMEOUT 5 /* In jiffies */

extern struct net_device *snull_devs[];
