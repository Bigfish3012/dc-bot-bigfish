CXX      = g++
CPPFLAGS = -I../include
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
DEPFLAGS = -MMD -MP
LDLIBS   = -ldpp -lcurl
TARGET   = bot
SRCS     = main.cpp helper.cpp pdf_extract.cpp media.cpp
OBJS     = $(SRCS:.cpp=.o)
DEPS     = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

.PHONY: all clean
