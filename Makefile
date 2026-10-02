CXX      := g++
CXXFLAGS := -std=c++26 -Wall -Wextra -Werror -pedantic-errors -O2
DEPFLAGS := -MMD -MP
LDFLAGS  := -lcurl -lwebsockets -lpugixml

TARGET   := soundtouch
SOURCES  := main.cpp SoundTouchClient.cpp WebSocketListener.cpp StreamConfig.cpp DeviceDiscovery.cpp IcyDemuxer.cpp IcyReader.cpp StreamProxy.cpp
OBJECTS  := $(SOURCES:.cpp=.o)

# Each object's header dependencies, written by the compiler as it builds.
DEPENDS  := $(OBJECTS:.o=.d)

.PHONY: all clean container

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TARGET)

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
