# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: GPL-3.0-only

# Builds the neotrac command with g++ and Botan. libember's header is used from
# EMBER_INCLUDE when given, else from where its package installs it. Without either,
# libember 0.1.0's header is downloaded once into ember/ and checked against its sha256.
#
#   make                  build ./neotrac
#   make install          to $(DESTDIR)$(PREFIX)/bin, /usr/local/bin by default
#   make header           write generated/neotrac.hpp again, from neotrac's .one files

CXX ?= g++
PREFIX ?= /usr/local
CXXFLAGS ?= -O2
TLS = $(shell pkg-config --cflags --libs botan-3)

EMBER_VERSION = 0.1.0
EMBER_SHA256 = bcdc31ac4e0ec87831f6b827870d43763731c5c9499c9d098121985806259ffd
EMBER_INCLUDE ?= $(firstword $(foreach d,/usr/include /usr/local/include,$(if $(wildcard $(d)/ember.hpp),$(d))) ember)

neotrac: neotrac.cpp generated/neotrac.hpp $(EMBER_INCLUDE)/ember.hpp
	$(CXX) -std=c++23 $(CXXFLAGS) -Wall -Wextra -Igenerated -I$(EMBER_INCLUDE) neotrac.cpp -o neotrac $(TLS)

ember/ember.hpp:
	mkdir -p ember
	curl -fsSL -o ember/ember.hpp.part https://raw.githubusercontent.com/da0x/libember/v$(EMBER_VERSION)/include/ember.hpp
	echo "$(EMBER_SHA256)  ember/ember.hpp.part" | sha256sum -c --quiet
	mv ember/ember.hpp.part ember/ember.hpp

header:
	cd .. && one build . --for production && cp build/cpp/neotrac.hpp cli/generated/neotrac.hpp

install: neotrac
	install -Dm755 neotrac $(DESTDIR)$(PREFIX)/bin/neotrac

clean:
	rm -f neotrac

.PHONY: header install clean
