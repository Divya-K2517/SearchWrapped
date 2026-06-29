CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I./include
LDFLAGS  = -lpthread -lws2_32
TARGET   = search_wrapped_server
SRC      = src/main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) include/httplib.h include/json.hpp
	@echo "🔨 Compiling C++ Search Wrapped backend..."
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "✅ Built: ./$(TARGET)"

run: $(TARGET)
	@echo "🚀 Starting server at http://localhost:8080"
	./$(TARGET)

clean:
	rm -f $(TARGET)
