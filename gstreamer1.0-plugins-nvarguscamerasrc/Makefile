# Copyright (c) 2020-2022, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

SO_NAME := libgstnvarguscamerasrc.so

CC := g++
exec_prefix ?= /usr
libdir ?= $(exec_prefix)/lib
includedir ?= $(exec_prefix)/include

GST_INSTALL_DIR ?= $(libdir)/gstreamer-1.0

LIBS:= -lnvbufsurface -lnvbufsurftransform -lnvdsbufferpool -lnvargus_socketclient -lpthread

SRCS := $(wildcard *.cpp)

PKGS := gstreamer-1.0 \
	gstreamer-base-1.0 \
	gstreamer-video-1.0 \
	gstreamer-allocators-1.0 \
	glib-2.0

OBJS := $(SRCS:.cpp=.o)

CXXFLAGS += -fPIC -I=$(includedir)/Argus/utils

CXXFLAGS += `pkg-config --cflags $(PKGS)`

LDFLAGS += -Wl,--no-undefined

LIBS += `pkg-config --libs $(PKGS)`

all: $(SO_NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SO_NAME): $(OBJS)
	$(CXX) -shared -o $(SO_NAME) $(OBJS) $(LIBS) $(LDFLAGS)

.PHONY: install
install: $(SO_NAME)
	install -d $(DESTDIR)$(GST_INSTALL_DIR)
	install -m 0644 $(SO_NAME) $(DESTDIR)$(GST_INSTALL_DIR)/

.PHONY: clean
clean:
	rm -rf $(OBJS) $(SO_NAME)
