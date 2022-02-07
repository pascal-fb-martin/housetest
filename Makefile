
SHARE=/usr/local/share/house

all: housetest

clean:
	rm -f *.o *.a housetest

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housetest: housetest.o
	gcc -g -O -o housetest $< -lechttp -lssl -lcrypto -lrt

install:
	mkdir -p /usr/local/bin
	rm -f /usr/local/bin/housetest
	cp housetest /usr/local/bin
	chown root:root /usr/local/bin/housetest
	chmod 755 /usr/local/bin/housetest

uninstall:
	rm -f /usr/local/bin/housetest

purge: uninstall

