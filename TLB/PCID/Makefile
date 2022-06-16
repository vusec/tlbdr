all: trigger kmod

ready: trigger kmod unload
	sudo insmod mmuctl/mmuctl.ko

unload:
	sudo rmmod mmuctl || true

test: trigger ready
	./trigger


kmod:
	cd mmuctl; $(MAKE) clean; $(MAKE)

trigger: trigger.c
	$(CC) -Wall -O2 -o $@ $<


.PHONY: ready unload test kmod clean

clean:
	rm -f trigger
	$(MAKE) -C mmuctl clean || true
