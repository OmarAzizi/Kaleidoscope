# Kaleidoscope :telescope:
![C++](https://img.shields.io/badge/c++-%2300599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white)

This is a toy programming language and its compiler made with [LLVM](https://llvm.org/) and C++. The project isn't finished yet (still in progress). So far, I have implemented all of the frontend components the language needs:
- Lexing
- Abstract Syntax Tree (AST)
- Parsing

I also did the code the generation part to generate the [LLVM IR (LLVM Intermediate Representation)](https://llvm.org/docs/LangRef.html). Finally I added a [JIT](https://en.wikipedia.org/wiki/Just-in-time_compilation) Driver so I can see the IR generated.

#### Note
This is just something I made to teach myself more about compilers in general and the LLVM compiler infrastructure in particular, so I didn't follow software best practices.


