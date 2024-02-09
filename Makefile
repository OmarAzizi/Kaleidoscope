CLANG = clang++
CLANG_FLAGS = -g -O3
LLVM = `llvm-config --cxxflags --ldflags --system-libs --libs core` 

kaleidoscope:
	$(CLANG) $(CLANG_FLAGS) kaleidoscope.cpp $(LLVM) -o kaleidoscope 
