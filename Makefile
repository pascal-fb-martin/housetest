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
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house
        
INSTALL=/usr/bin/install

all: housetest housesimio

clean:
	rm -f *.o *.a housetest housesimio

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housetest: housetest.o
	gcc -g -O -o housetest $< -lechttp -lssl -lcrypto -lmagic -lrt

housesimio: housesimio.o
	gcc -g -O -o housesimio $< -lhouseportal -lechttp -lssl -lcrypto -lmagic -lrt

install-ui:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/simio
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/simio

install: install-ui
	$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 0755 -s housetest housesimio $(DESTDIR)$(prefix)/bin

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/housetest
	rm -f $(DESTDIR)$(prefix)/usr/local/bin/housesimio

purge: uninstall

