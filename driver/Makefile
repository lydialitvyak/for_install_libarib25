TARGET:= pt1_drv.ko
VERBOSITY = 0

all: ${TARGET}

pt1_drv.ko: pt1_pci.c pt1_i2c.c pt1_tuner.c pt1_tuner_data.c
	make -C /lib/modules/`uname -r`/build M=`pwd` V=$(VERBOSITY) modules

clean:
	make -C /lib/modules/`uname -r`/build M=`pwd` V=$(VERBOSITY) clean

obj-m := pt1_drv.o

pt1_drv-objs := pt1_pci.o pt1_i2c.o pt1_tuner.o pt1_tuner_data.o

clean-files := *.o *.ko *.mod.[co] *~

install: $(TARGET)
	install -m 644 $(TARGET) /lib/modules/`uname -r`/kernel/drivers/video
	if [ -d /etc/udev/rules.d -a ! -f /etc/udev/rules.d/99-pt1.rules ] ; then \
		install -m 644 etc/99-pt1.rules /etc/udev/rules.d ; \
	fi
	depmod -a
