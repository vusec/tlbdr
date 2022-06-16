all: madtlb
run: clear-max.txt clear-probe.txt inuse-max.txt inuse-probe.txt

clear-max.txt clear-probe.txt inuse-max.txt inuse-probe.txt &: madtlb measure.sh
	./measure.sh

madtlb: madtlb.c
	$(CC) -O2 -Wall -o $@ $<

.PHONY: clean cleanall

clean:
	rm -f madtlb

cleanall: clean
	rm -f clear-max.txt clear-probe.txt inuse-max.txt inuse-probe.txt
