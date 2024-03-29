#ifndef KALEIDOSCOPE_NODE_HPP
#define KALEIDOSCOPE_NODE_HPP

#include <map>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <utility>

#include "KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

namespace kaleidoscope
{
class PrototypeAST;

inline llvm::LLVMContext TheContext;
inline llvm::IRBuilder<> Builder(TheContext);
inline std::unique_ptr<llvm::Module> TheModule;
inline std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
inline std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
inline std::map<std::string, llvm::AllocaInst*> NamedValues;
inline std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

inline bool Interpret;

inline std::unique_ptr<class ExprAST> log_error(const char *str)
{
    fprintf(stderr, "LogError: %s\n", str);
    return nullptr;
}

inline std::unique_ptr<class PrototypeAST> log_error_p(const char *str)
{
    log_error(str);
    return nullptr;
}

inline llvm::Value *log_error_v(const char *str)
{
    log_error(str);
    return nullptr;
}

inline llvm::Function *log_error_f(const char *str)
{
    log_error(str);
    return nullptr;
}

inline void initialize_module_and_pass_manager()
{
    TheModule = llvm::make_unique<llvm::Module>("My cool jit", TheContext);
    if (Interpret)
    {
        TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());
    }

    TheFPM = llvm::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

    TheFPM->add(llvm::createInstructionCombiningPass());
    TheFPM->add(llvm::createReassociatePass());
    TheFPM->add(llvm::createGVNPass());
    TheFPM->add(llvm::createCFGSimplificationPass());
    TheFPM->add(llvm::createPromoteMemoryToRegisterPass());
    TheFPM->add(llvm::createInstructionCombiningPass());
    TheFPM->add(llvm::createReassociatePass());

    TheFPM->doInitialization();
}

inline llvm::AllocaInst *create_entry_block_alloca(llvm::Function *the_function,
    const std::string &var_name)
{
    llvm::IRBuilder<> tmp_b(&the_function->getEntryBlock(),
        the_function->getEntryBlock().begin());
    return tmp_b.CreateAlloca(llvm::Type::getDoubleTy(TheContext), 0, var_name);
}

// ExprAST - Base class for all expression nodes
class ExprAST
{
  public:
    virtual ~ExprAST() {}
    virtual llvm::Value *codegen() = 0;
};

// NumberExprAST - Expression class for numeric literals like "1.0"
class NumberExprAST : public ExprAST
{
    double value_;

  public:
    NumberExprAST(double value) : value_(value) {}
    llvm::Value *codegen() override;
};

// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST
{
    std::string name_;

  public:
    VariableExprAST(const std::string &name) : name_(name) {}
    const std::string &get_name() { return name_; }
    llvm::Value *codegen() override;
};

class UnaryExprAST : public ExprAST
{
    char op_;
    std::unique_ptr<ExprAST> operand_;

  public:
    UnaryExprAST(char op, std::unique_ptr<ExprAST> operand)
      : op_(op), operand_(std::move(operand)) {}

    llvm::Value *codegen() override;
};

// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST
{
    char op_;
    std::unique_ptr<ExprAST> lhs_, rhs_;

  public:
    BinaryExprAST(char op,
        std::unique_ptr<ExprAST> lhs,
        std::unique_ptr<ExprAST> rhs)
      : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}
    llvm::Value *codegen() override;
};

// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST
{
    std::string callee_;
    std::vector<std::unique_ptr<ExprAST>> args_;

  public:
    CallExprAST(const std::string &callee,
        std::vector<std::unique_ptr<ExprAST>> args)
      : callee_(callee), args_(std::move(args)) {}
    llvm::Value *codegen() override;
};

class IfExprAST : public ExprAST
{
    std::unique_ptr<ExprAST> cond_, then_, else_;

  public:
    IfExprAST(std::unique_ptr<ExprAST> cond,
        std::unique_ptr<ExprAST> then,
        std::unique_ptr<ExprAST> els)
      : cond_(std::move(cond)), then_(std::move(then)), else_(std::move(els)) {}

    llvm::Value *codegen() override;
};

class ForExprAST : public ExprAST
{
    std::string var_name_;
    std::unique_ptr<ExprAST> start_, end_, step_, body_;

  public:
    ForExprAST(const std::string &var_name,
        std::unique_ptr<ExprAST> start,
        std::unique_ptr<ExprAST> end,
        std::unique_ptr<ExprAST> step,
        std::unique_ptr<ExprAST> body)
      : var_name_(var_name), start_(move(start)), end_(move(end)),
        step_(move(step)), body_(move(body)) {}

    llvm::Value *codegen() override;
};

class VarExprAST : public ExprAST
{
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> var_names_;
    std::unique_ptr<ExprAST> body_;

  public:
    VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> var_names,
        std::unique_ptr<ExprAST> body)
      : var_names_(std::move(var_names)), body_(std::move(body)) {}

    llvm::Value *codegen() override;
};

// PrototypeAST - This class represents the "prototype" for a function,
// which captures its name, and its argument names (thus implicitly the number
// of arguments the function takes).
class PrototypeAST
{
    std::string name_;
    std::vector<std::string> args_;
    bool is_operator_;
    size_t precedence_;

  public:
    PrototypeAST(const std::string &name,
        std::vector<std::string> args,
        bool is_operator, size_t precedence = 30)
      : name_(name), args_(std::move(args)),
        is_operator_(is_operator), precedence_(precedence) {}

    const std::string &get_name() const { return name_; }
    bool is_unary_op() const { return is_operator_ && args_.size() == 1; }
    bool is_binary_op() const { return is_operator_ && args_.size() == 2; }
    char get_operator_name() const
    {
        assert(is_unary_op() || is_binary_op());
        return name_.back();
    }
    size_t get_binary_precedence() const { return precedence_; }
    llvm::Function *codegen();
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST
{
    std::unique_ptr<PrototypeAST> proto_;
    std::unique_ptr<ExprAST> body_;

  public:
    FunctionAST(std::unique_ptr<PrototypeAST> proto,
        std::unique_ptr<ExprAST> body)
      : proto_(std::move(proto)), body_(std::move(body)) {}
    llvm::Function *codegen();
};
} // namespace kaleidoscope

#endif // KALEIDOSCOPE_NODE_HPP
