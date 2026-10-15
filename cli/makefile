# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: GPL-3.0-only

# Builds the neotrac command with g++ and Botan. libember's header is used from
# EMBER_INCLUDE when given, else from where its package installs it, when that's the
# version below. Without either, its header is downloaded once into ember/<version>/ and
# checked against its sha256.
#
#   make                  build ./neotrac
#   make install          to $(DESTDIR)$(PREFIX)/bin, /usr/local/bin by default
#   make header           write generated/neotrac.hpp again, from neotrac's .one files

CXX ?= g++
PREFIX ?= /usr/local
CXXFLAGS ?= -O2
TLS = $(shell pkg-config --cflags --libs botan-3)

EMBER_VERSION = 0.1.1
EMBER_SHA256 = b3232ede89253aa16f663a8dc098b6f6c4a22342fa3b83e8b70653a8996d1a9c
EMBER_INCLUDE ?= $(firstword $(foreach d,/usr/include /usr/local/include,$(if $(shell grep -ls '"$(EMBER_VERSION)"' $(d)/ember.hpp),$(d))) ember/$(EMBER_VERSION))

neotrac: neotrac.cpp terminal.hpp generated/neotrac.hpp $(EMBER_INCLUDE)/ember.hpp
	$(CXX) -std=c++23 $(CXXFLAGS) -Wall -Wextra -Igenerated -I$(EMBER_INCLUDE) neotrac.cpp -o neotrac $(TLS)

ember/$(EMBER_VERSION)/ember.hpp:
	mkdir -p ember/$(EMBER_VERSION)
	curl -fsSL -o $@.part https://raw.githubusercontent.com/da0x/libember/v$(EMBER_VERSION)/include/ember.hpp
	echo "$(EMBER_SHA256)  $@.part" | sha256sum -c --quiet
	mv $@.part $@

header:
	cd .. && one build . --for production && cp build/cpp/neotrac.hpp cli/generated/neotrac.hpp

install: neotrac
	install -Dm755 neotrac $(DESTDIR)$(PREFIX)/bin/neotrac
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/neotrac/LICENSE

clean:
	rm -f neotrac

.PHONY: header install clean
