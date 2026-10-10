# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: GPL-3.0-only

# Builds the neotrac command with g++ and Botan. libember's header is found where its
# package installs it, /usr/include, or in EMBER_INCLUDE.
#
#   make                  build ./neotrac
#   make header           write generated/neotrac.hpp again, from neotrac's .one files
#   make install          to $(DESTDIR)$(PREFIX)/bin

CXX ?= g++
PREFIX ?= /usr/local
EMBER_INCLUDE ?= /usr/include
CXXFLAGS ?= -O2
TLS = $(shell pkg-config --cflags --libs botan-3)

neotrac: neotrac.cpp generated/neotrac.hpp
	$(CXX) -std=c++23 $(CXXFLAGS) -Wall -Wextra -Igenerated -I$(EMBER_INCLUDE) neotrac.cpp -o neotrac $(TLS)

header:
	cd .. && one build . --for production && cp build/cpp/neotrac.hpp cli/generated/neotrac.hpp

install: neotrac
	install -Dm755 neotrac $(DESTDIR)$(PREFIX)/bin/neotrac

clean:
	rm -f neotrac

.PHONY: header install clean
