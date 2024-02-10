# Kaleidoscope :telescope:
![C++](https://img.shields.io/badge/c++-%2300599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white)

This is a toy programming language and its compiler made with [LLVM](https://llvm.org/) and C++. The project isn't finished yet (still in progress). So far, I have implemented all of the frontend components the language needs:
- Lexing
- Abstract Syntax Tree (AST)
- Parsing

Along with the code the generation to generate the [LLVM IR (LLVM Intermediate Representation)](https://llvm.org/docs/LangRef.html). Also I added a [JIT](https://en.wikipedia.org/wiki/Just-in-time_compilation) Driver so I can see the IR generated, and the evaluated results. 
<br>


And Finally I Added additional [Optimizers](https://en.wikipedia.org/wiki/Optimizing_compiler#:~:text=Compiler%20optimization%20is%20generally%20implemented,fewer%20resources%20or%20executes%20faster.) to the compiler such as:
- [Constant Folding &  Constant Propagation](https://en.wikipedia.org/wiki/Constant_folding)
- [Common Subexpression Elimination](https://en.wikipedia.org/wiki/Common_subexpression_elimination)
- [Peephole Optimization](https://en.wikipedia.org/wiki/Peephole_optimization)
- [Dead-Code Elimination](https://en.wikipedia.org/wiki/Dead-code_elimination)

### Note
This is just something I made to teach myself more about compilers in general and the LLVM compiler infrastructure in particular, so I didn't follow software best practices and its not production ready.


