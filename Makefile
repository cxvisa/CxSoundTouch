CXX      := g++
CXXFLAGS := -std=c++26 -Wall -Wextra -Werror -pedantic-errors -O2
DEPFLAGS := -MMD -MP
LDFLAGS  := -lcurl -lwebsockets -lpugixml

TARGET   := cxstcc
SOURCES  := main.cpp SoundTouchClient.cpp WebSocketListener.cpp StreamConfig.cpp DeviceDiscovery.cpp IcyDemuxer.cpp IcyReader.cpp StreamProxy.cpp HttpUtil.cpp WebJson.cpp WebServer.cpp SpeakerConfig.cpp
OBJECTS  := $(SOURCES:.cpp=.o)

# Each object's header dependencies, written by the compiler as it builds.
DEPENDS  := $(OBJECTS:.o=.d)

.PHONY: all clean container install install-service deps-fedora test e2e

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TARGET) $(TEST_TARGET)

# A small, dependency-free unit test for the web server's pure logic (HTTP request parsing, JSON
# serialisation, the stream list's rules and file, and the live-change signal). No gtest: it compiles
# only the units under test, which pull in no sockets, curl or pugixml. Run: make test
TEST_TARGET := webtest
TEST_SRC    := test/WebTest.cpp HttpUtil.cpp WebJson.cpp StreamConfig.cpp SpeakerConfig.cpp

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRC) HttpUtil.h WebJson.h LiveSignal.h StreamConfig.h SpeakerConfig.h DeviceDiscovery.h SoundTouchClient.h
	$(CXX) $(CXXFLAGS) -I. -o $@ $(TEST_SRC)

# End to end, with no real speaker: runs the built program against test/fake_speaker.py (a stand-in
# SoundTouch on 127.0.0.2) and checks the dashboard, live, and each of its controls. Needs python3,
# nothing else.
e2e: $(TARGET)
	python3 test/web_live_check.py

# The Fedora packages the build needs: sudo make deps-fedora. dnf asks before installing anything,
# and skips what is already there; DNF="dnf -y" to not be asked. Docker, for make container, is
# left out: Fedora's moby-engine and Docker's own docker-ce conflict, so install whichever you use.
FEDORA_DEPS := gcc-c++ make libcurl-devel libwebsockets-devel pugixml-devel json-devel
DNF         := dnf

deps-fedora:
	$(DNF) install $(FEDORA_DEPS)

# Installing as a system service: see systemd/README.md. Neither target enables or starts it.
PREFIX    := /usr/local
DATA_DIR  := /var/lib/private/soundtouch
SEED_FROM :=

# The binary, as already built: run make first, so that nothing is built as root.
install:
	@test -x $(TARGET) || { echo "Build it first: make"; exit 1; }
	install -D -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

# The unit and its settings, and a seeded data directory. Settings and data already there are left
# alone. SEED_FROM=<dir> also copies devices.json and state.json from where control ran before.
install-service:
	install -D -m 0644 systemd/soundtouch.service $(DESTDIR)/etc/systemd/system/soundtouch.service
	test -e $(DESTDIR)/etc/soundtouch/soundtouch.env || install -D -m 0644 systemd/soundtouch.env $(DESTDIR)/etc/soundtouch/soundtouch.env
	install -d -m 0700 $(DESTDIR)/var/lib/private
	install -d -m 0755 $(DESTDIR)$(DATA_DIR)
	test -e $(DESTDIR)$(DATA_DIR)/streams.json || install -m 0644 streams.json $(DESTDIR)$(DATA_DIR)/streams.json
	for f in devices.json state.json; do \
	    if [ -n "$(SEED_FROM)" ] && [ -e "$(SEED_FROM)/$$f" ] && [ ! -e $(DESTDIR)$(DATA_DIR)/$$f ]; then \
	        install -m 0644 "$(SEED_FROM)/$$f" $(DESTDIR)$(DATA_DIR)/$$f; \
	    fi; \
	done
	if [ -z "$(DESTDIR)" ]; then systemctl daemon-reload; fi

# A self-contained image: one fully static binary on scratch, ~2.2 MB (see container/Dockerfile).
IMAGE    := soundtouch:static

container:
	docker build -f container/Dockerfile -t $(IMAGE) .

run-control: $(TARGET)
	./$(TARGET) control

run-play: $(TARGET)
	./$(TARGET) play

run-stop: $(TARGET)
	./$(TARGET) stop

run-status: $(TARGET)
	./$(TARGET) status

-include $(DEPENDS)
