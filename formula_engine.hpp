#pragma once

#include <string>
#include <vector>

enum class FormulaOp {
    Number,
    Variable,
    Add,
    Sub,
    Mul,
    Div,
    Neg,
    FuncAbs,
    FuncSqrt,
    FuncSin,
    FuncCos,
    FuncTan,
    FuncLog,
    FuncExp
};

struct FormulaToken {
    FormulaOp op = FormulaOp::Number;
    double value = 0.0;
};

enum class TransformRuntimeKind : unsigned char {
    Identity = 0,
    Affine = 1,
    CachedFormula = 2
};

struct AffineFormulaInfo {
    bool valid = false;
    double mul = 0.0;
    double add = 0.0;
};

std::wstring default_channel_formula_text();
std::wstring normalize_formula_text(const std::wstring& text);
bool compile_formula_rpn(const std::wstring& raw_text,
                         std::vector<FormulaToken>& out,
                         std::wstring& error,
                         bool english);
bool formula_rpn_is_identity(const std::vector<FormulaToken>& rpn);
AffineFormulaInfo analyze_formula_rpn_affine(const std::vector<FormulaToken>& rpn);
double eval_formula_rpn(const std::vector<FormulaToken>& rpn, double x);
