# housetest - Test tools for the suite of House applications.
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HROOT=/usr/local
SHARE=$(HROOT)/share/house

all: housetest housesimio

clean:
	rm -f *.o *.a housetest housesimio

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housetest: housetest.o
	gcc -g -O -o housetest $< -lechttp -lssl -lcrypto -lrt

housesimio: housesimio.o
	gcc -g -O -o housesimio $< -lhouseportal -lechttp -lssl -lcrypto -lrt

install: uninstall
	mkdir -p $(HROOT)/bin
	cp housetest housesimio $(HROOT)/bin
	chown root:root $(HROOT)/bin/housetest $(HROOT)/bin/housesimio
	chmod 755 $(HROOT)/bin/housetest $(HROOT)/bin/housesimio
	mkdir -p $(SHARE)/public/simio
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/simio
	cp public/* $(SHARE)/public/simio
	chown root:root $(SHARE)/public/simio/*.html
	chmod 644 $(SHARE)/public/simio/*.html

uninstall:
	rm -f /usr/local/bin/housetest /usr/local/bin/housesimio

purge: uninstall

