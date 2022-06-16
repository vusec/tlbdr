all: trigger kmod

ready: trigger kmod unload
	sudo insmod mmuctl/mmuctl.ko

restart: unload
	 sudo insmod mmuctl/mmuctl.ko
	 sudo ./trigger

insert: 	 
	 sudo insmod mmuctl/mmuctl.ko

restart_trigger: 
	 sudo ./trigger

unload:
	sudo rmmod mmuctl || true

test: trigger ready
	sudo ./trigger


kmod:
	cd mmuctl; $(MAKE) clean; $(MAKE)

trigger: trigger.c
	$(CC) -Wall -O2 -o $@ $< -lrt


.PHONY: ready unload test kmod clean

clean:
	rm -f trigger
	$(MAKE) -C mmuctl clean || true
