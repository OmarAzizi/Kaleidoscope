CLANG = clang++
LLVM = `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native`

kaleidoscope:
	$(CLANG) -g kaleidoscope.cpp $(LLVM) -O3 -o kaleidoscope 
