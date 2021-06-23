obj-m += snull.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f Module.symvers modules.order
	rm -f snull.o snull.mod snull.mod.c snull.mod.o

fclean: clean
	rm -f snull.ko

re: fclean all
