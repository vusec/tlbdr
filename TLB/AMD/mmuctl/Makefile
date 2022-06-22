BUILD ?= obj

all: mmuctl.ko

obj-m += mmuctl.o
mmuctl-objs += source/address_generation.o
mmuctl-objs += source/experiments.o
mmuctl-objs += source/helpers.o
mmuctl-objs += source/kmod.o
mmuctl-objs += source/pgtable.o
ccflags-y += -I$(PWD)/include

deps += $(hjb-obj:.o=.d)
deps += $(attack-obj:.o=.d)

-include ${deps}

mmuctl.ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

$(BUILD)/%.o: %.c
	@echo "CC $<"
	@mkdir -p "$(dir $@)"
	@${CC} -c $< -o $@ ${CFLAGS} -MT $@ -MMD -MP -MF $(@:.o=.d)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	@rm -rf ${BUILD}

install:
	@install -m 644 mmuctl.ko "/lib/modules/$(shell uname -r)/kernel/drivers/misc/mmuctl.ko"
	@depmod

uninstall:
	@rm "/lib/modules/$(uname -r)/kernel/drivers/misc/mmuctl.ko"
	@depmod