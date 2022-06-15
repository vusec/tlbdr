BUILD ?= obj
PREFIX ?= /usr
LIBDIR ?= lib

includedir = $(PREFIX)/include
libdir = $(PREFIX)/$(LIBDIR)

all: mmuctl.ko libmmuctl.so

obj-m += mmuctl.o
mmuctl-objs += source/kmod.o
ccflags-y += -I$(PWD)/include

lib-obj-y += source/mmuctl.o
lib-objs += $(addprefix ${BUILD}/, ${lib-obj-y})

deps += $(hjb-obj:.o=.d)
deps += $(attack-obj:.o=.d)

-include ${deps}

mmuctl.ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

libmmuctl.so: CFLAGS += -fPIC -Iinclude
libmmuctl.so: LDFLAGS += -shared
libmmuctl.so: $(lib-objs)
	@echo "LD $(notdir $@)"
	@mkdir -p $(dir $@)
	@${CC} ${lib-objs} -o $@ ${LDFLAGS} ${CFLAGS} ${LIBS}

$(BUILD)/%.o: %.c
	@echo "CC $<"
	@mkdir -p "$(dir $@)"
	@${CC} -c $< -o $@ ${CFLAGS} -MT $@ -MMD -MP -MF $(@:.o=.d)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	@rm -rf ${BUILD}
	@rm libmmuctl.so

install: libmmuctl.so
	@install -m 755 -d "$(DESTDIR)$(libdir)"
	@install -m 644 include/mmuctl.h "$(DESTDIR)$(includedir)/"
	@install -m 644 libmmuctl.so "$(DESTDIR)$(libdir)/libmmuctl.so"
	@install -m 644 mmuctl.ko "/lib/modules/$(shell uname -r)/kernel/drivers/misc/mmuctl.ko"
	@depmod

uninstall:
	@rm "/lib/modules/$(uname -r)/kernel/drivers/misc/mmuctl.ko"
	@rm "$(DESTDIR)$(libdir)/libmmuctl.so"
	@rm "$(DESTDIR)$(includedir)/mmuctl.h"
	@depmod
