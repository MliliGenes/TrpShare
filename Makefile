NAME := trpshare
CXX ?= c++
CXXFLAGS ?= -Wall -Wextra -Wpedantic -std=c++17
CPPFLAGS ?= -Iinclude -D_FILE_OFFSET_BITS=64
LDLIBS ?= -lncurses

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
DEP := $(OBJ:.o=.d)

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(OBJ) $(LDLIBS) -o $@

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP)

fclean: clean
	rm -f $(NAME)

re: fclean all

-include $(DEP)

test: $(NAME)
	python3 tests/test_uploads.py
	node tests/test_browser_upload.cjs

test-large: $(NAME)
	python3 tests/test_uploads.py --large

.PHONY: all clean fclean re test test-large
