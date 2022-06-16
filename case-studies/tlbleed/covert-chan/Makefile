CFLAGS=-Wall -O2 -D_GNU_SOURCE -Werror -pie -fpic -g -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable -mrtm -Wno-unused-function -Wno-unused-variable
LDFLAGS=-lpthread

all: covert-naive covert-ninja

covert-naive: covert-channel-tlb.c common.h profile.h
	$(CC) -o $@ $< $(LDFLAGS) $(CFLAGS)

covert-ninja: covert-channel-tlb.c common.h profile.h
	$(CC) -o $@ $< -DUSE_NINJA $(LDFLAGS) $(CFLAGS)

clean:
	rm -f *.o covert-naive covert-ninja
