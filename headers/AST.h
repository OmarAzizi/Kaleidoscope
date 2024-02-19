#ifndef __AST_H__
#define __AST_H__

#include "common.h"
/*
    ==============================================
    ========= AST (Abstract Syntax Tree) ========= 
    ==============================================
*/

/*
    The AST for a program captures its behavior in such a way that it is easy for later stages 
    of the compiler (e.g. code generation) to interpret. 

    We basically want one object for each construct in the language. In Kaleidoscope, 
    we have expressions, a prototype, and a function object.
*/

// base class for all expression nodes in the AST 
// Note: This is an abstract class
namespace {
    class ExprAST {
    public:
        virtual ~ExprAST() = default;
        
        /*
            The codegen() method says to emit IR for that AST node along with all the 
            things it depends on, and they all return an LLVM Value object. 
            
            “Value” is the class used to represent a 
            “Static Single Assignment (SSA) register” 
            or “SSA value” in LLVM. 
        */
        virtual Value* codegen() = 0;
    };

    // number node
    class NumberExprAST : public ExprAST {
        double Val;
        
    public:
        NumberExprAST(double Val) : Val(Val) {}
        Value* codegen() override;
    };

    // variable/identifier node
    class VariableExprAST : public ExprAST {
        std::string Name;

    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
        Value* codegen() override;
    };

    // binary expression node
    class BinaryExprAST : public ExprAST {
        char Op; // operator of binary expr (e.g. '+', '-', '*', '/')
        std::unique_ptr<ExprAST> LHS, RHS; // left & right hand sides of the expression (e.g. operands)

    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS) 
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        Value* codegen() override;
    };

    class UnaryExprAST : public ExprAST {
        char Opcode;
        std::unique_ptr<ExprAST> Operand;

    public:
        UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
            : Opcode(Opcode), Operand(std::move(Operand)) {}

        Value* codegen() override;
    };

    class CallExprAST : public ExprAST {
        std::string Callee;
        std::vector<std::unique_ptr<ExprAST>> Args;

    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args) 
            : Callee(Callee), Args(std::move(Args)) {}
        Value* codegen() override;
    };

    class IfExprAST : public ExprAST {
        std::unique_ptr<ExprAST> Cond, Then, Else;
    public:
        IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,std::unique_ptr<ExprAST> Else) :
            Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

        Value* codegen() override;
    };

    /* 
        PrototypeAST - This class represents the "prototype" for a function,
        which captures its name, and its argument names (thus implicitly the number
        of arguments the function takes).
    */

    class ForExprAST : public ExprAST {
        std::string VarName;
        std::unique_ptr<ExprAST> Start, End, Step, Body;
    public:
        ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
            std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step, 
            std::unique_ptr<ExprAST> Body) : VarName(VarName), Start(std::move(Start)), 
            End(std::move(End)), Step(std::move(Step)), Body(std::move(Body)) {}
    
        Value* codegen() override;
    };

    class PrototypeAST {
        std::string Name;
        std::vector<std::string> Args;

        bool isOperator;
        unsigned Precedence; // Precedence if binay operator

    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args, bool isOperator = false, unsigned Prec = 0)
            : Name(Name), Args(std::move(Args)), isOperator(isOperator), Precedence(Prec) {}
        
        const std::string &getName() const { return Name; }
        Function* codegen();

        bool isUnaryOp() const { return isOperator && Args.size() == 1;}
        bool isBinaryOp() const { return isOperator && Args.size() == 2; }

        char getOperatorName() const {
            assert(isUnaryOp() || isBinaryOp());
            return Name[Name.size() - 1];
        }

        unsigned getBinaryPrecedence() const { return Precedence; }
    };

    // FunctionAST - This class represents a function definition itself.
    class FunctionAST {
        std::unique_ptr<PrototypeAST> Proto;
        std::unique_ptr<ExprAST> Body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
            : Proto(std::move(Proto)), Body(std::move(Body)) {}
        Function* codegen();
    };
}

#endif