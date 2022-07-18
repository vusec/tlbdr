all: mmuctl/mmuctl.ko mmuctl/libmmuctl.so ptham

mmuctl/mmuctl.ko mmuctl/libmmuctl.so &: mmuctl/
	cd $< && $(MAKE)

ptham: ptham.c xbs.h
	$(CC) -o $@ -Immuctl/include -g -march=skylake -O2 -W ${CFLAGS} $< -Lmmuctl -lmmuctl

.PHONY: clean run

clean:
	-cd mmuctl && $(MAKE) clean
	rm -f ptham

run: all
	./run.sh
