CFLAGS := -O2

all: mmuctl/mmuctl.ko mmuctl/libmmuctl.so ptham

mmuctl/mmuctl.ko mmuctl/libmmuctl.so &: mmuctl/
	cd $< && $(MAKE)

ptham: ptham.c xbs.h
	$(CC) -o $@ -Immuctl/include -O2 -W -Lmmuctl -lmmuctl $<

.PHONY: clean run

clean:
	-cd mmuctl && $(MAKE) clean
	rm -f ptham

run: all
	./run.sh
