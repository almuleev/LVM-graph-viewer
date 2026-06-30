#include "formula_engine.hpp"

#include <array>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string>
#include <vector>

namespace {

int formula_precedence(FormulaOp op) {
    switch (op) {
        case FormulaOp::Neg: return 3;
        case FormulaOp::Mul:
        case FormulaOp::Div: return 2;
        case FormulaOp::Add:
        case FormulaOp::Sub: return 1;
        default: return 0;
    }
}

bool formula_is_right_associative(FormulaOp op) {
    return op == FormulaOp::Neg;
}

bool formula_is_function(FormulaOp op) {
    return op == FormulaOp::FuncAbs || op == FormulaOp::FuncSqrt || op == FormulaOp::FuncSin ||
           op == FormulaOp::FuncCos || op == FormulaOp::FuncTan || op == FormulaOp::FuncLog ||
           op == FormulaOp::FuncExp;
}

bool formula_is_operator(FormulaOp op) {
    return op == FormulaOp::Add || op == FormulaOp::Sub || op == FormulaOp::Mul ||
           op == FormulaOp::Div || op == FormulaOp::Neg;
}

bool formula_function_from_name(const std::wstring& name, FormulaOp& out) {
    if (name == L"abs") { out = FormulaOp::FuncAbs; return true; }
    if (name == L"sqrt") { out = FormulaOp::FuncSqrt; return true; }
    if (name == L"sin") { out = FormulaOp::FuncSin; return true; }
    if (name == L"cos") { out = FormulaOp::FuncCos; return true; }
    if (name == L"tan") { out = FormulaOp::FuncTan; return true; }
    if (name == L"log") { out = FormulaOp::FuncLog; return true; }
    if (name == L"exp") { out = FormulaOp::FuncExp; return true; }
    return false;
}

bool push_error(std::wstring& error, bool english, const wchar_t* en, const wchar_t* ru) {
    error = english ? en : ru;
    return false;
}

}  // namespace

std::wstring default_channel_formula_text() {
    return L"x";
}

std::wstring normalize_formula_text(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L',') out.push_back(L'.');
        else out.push_back(ch);
    }
    std::size_t first = 0;
    while (first < out.size() && iswspace(out[first])) ++first;
    std::size_t last = out.size();
    while (last > first && iswspace(out[last - 1])) --last;
    out = out.substr(first, last - first);
    if (out.empty()) out = default_channel_formula_text();
    return out;
}

bool compile_formula_rpn(const std::wstring& raw_text,
                         std::vector<FormulaToken>& out,
                         std::wstring& error,
                         bool english) {
    const std::wstring text = normalize_formula_text(raw_text);
    struct StackEntry {
        FormulaOp op = FormulaOp::Add;
        bool is_lparen = false;
    };
    out.clear();
    std::vector<StackEntry> ops;
    bool expect_value = true;

    auto push_operator = [&](FormulaOp op) {
        while (!ops.empty() && !ops.back().is_lparen && formula_is_operator(ops.back().op)) {
            const int top_prec = formula_precedence(ops.back().op);
            const int cur_prec = formula_precedence(op);
            if (top_prec > cur_prec || (top_prec == cur_prec && !formula_is_right_associative(op))) {
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            } else {
                break;
            }
        }
        ops.push_back({op, false});
    };

    std::size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = text[i];
        if (iswspace(ch)) { ++i; continue; }

        if ((ch >= L'0' && ch <= L'9') || ch == L'.') {
            const wchar_t* begin = text.c_str() + i;
            wchar_t* end = nullptr;
            double value = wcstod(begin, &end);
            if (begin == end) {
                return push_error(error, english, L"Invalid number in coefficient.", L"Некорректное число в коэффициенте.");
            }
            i = static_cast<std::size_t>(end - text.c_str());
            out.push_back({FormulaOp::Number, value});
            expect_value = false;
            continue;
        }

        if (iswalpha(ch)) {
            std::size_t j = i;
            while (j < text.size() && (iswalpha(text[j]) || iswdigit(text[j]) || text[j] == L'_')) ++j;
            std::wstring name = text.substr(i, j - i);
            for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
            if (name == L"x") {
                out.push_back({FormulaOp::Variable, 0.0});
                expect_value = false;
            } else {
                FormulaOp fn = FormulaOp::FuncAbs;
                if (!formula_function_from_name(name, fn)) {
                    error = (english ? L"Unknown function: " : L"Неизвестная функция: ") + name;
                    return false;
                }
                ops.push_back({fn, false});
                expect_value = true;
            }
            i = j;
            continue;
        }

        if (ch == L'(') {
            ops.push_back({FormulaOp::Add, true});
            ++i;
            expect_value = true;
            continue;
        }
        if (ch == L')') {
            bool found_lparen = false;
            while (!ops.empty()) {
                if (ops.back().is_lparen) {
                    found_lparen = true;
                    ops.pop_back();
                    break;
                }
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            }
            if (!found_lparen) {
                return push_error(error, english, L"Mismatched parentheses.", L"Несогласованные скобки.");
            }
            if (!ops.empty() && formula_is_function(ops.back().op)) {
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            }
            ++i;
            expect_value = false;
            continue;
        }

        FormulaOp op = FormulaOp::Add;
        if (ch == L'+') op = FormulaOp::Add;
        else if (ch == L'-') op = expect_value ? FormulaOp::Neg : FormulaOp::Sub;
        else if (ch == L'*') op = FormulaOp::Mul;
        else if (ch == L'/') op = FormulaOp::Div;
        else {
            error = (english ? L"Unsupported character in coefficient: " : L"Недопустимый символ в коэффициенте: ") + std::wstring(1, ch);
            return false;
        }
        if (expect_value && op != FormulaOp::Neg) {
            return push_error(error, english, L"Operator position is invalid.", L"Некорректное положение оператора.");
        }
        push_operator(op);
        ++i;
        expect_value = true;
    }

    if (expect_value) {
        return push_error(error, english, L"Incomplete coefficient.", L"Коэффициент не завершён.");
    }

    while (!ops.empty()) {
        if (ops.back().is_lparen) {
            return push_error(error, english, L"Mismatched parentheses.", L"Несогласованные скобки.");
        }
        out.push_back({ops.back().op, 0.0});
        ops.pop_back();
    }
    return !out.empty();
}

bool formula_rpn_is_identity(const std::vector<FormulaToken>& rpn) {
    return rpn.size() == 1 && rpn[0].op == FormulaOp::Variable;
}

AffineFormulaInfo analyze_formula_rpn_affine(const std::vector<FormulaToken>& rpn) {
    if (rpn.empty()) return {};

    std::vector<AffineFormulaInfo> stack;
    stack.reserve(rpn.size());
    auto is_constant = [](const AffineFormulaInfo& value) {
        return value.valid && value.mul == 0.0;
    };
    auto push = [&](AffineFormulaInfo value) -> bool {
        if (!value.valid) return false;
        stack.push_back(value);
        return true;
    };
    auto pop1 = [&]() -> AffineFormulaInfo {
        if (stack.empty()) return {};
        AffineFormulaInfo value = stack.back();
        stack.pop_back();
        return value;
    };
    auto pop2 = [&](AffineFormulaInfo& a, AffineFormulaInfo& b) -> bool {
        if (stack.size() < 2) return false;
        b = stack.back();
        stack.pop_back();
        a = stack.back();
        stack.pop_back();
        return true;
    };
    auto push_constant_fn = [&](const AffineFormulaInfo& operand, double (*fn)(double)) -> bool {
        return is_constant(operand) && push({true, 0.0, fn(operand.add)});
    };

    for (const auto& token : rpn) {
        switch (token.op) {
            case FormulaOp::Number:
                if (!push({true, 0.0, token.value})) return {};
                break;
            case FormulaOp::Variable:
                if (!push({true, 1.0, 0.0})) return {};
                break;
            case FormulaOp::Add: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !push({a.valid && b.valid, a.mul + b.mul, a.add + b.add})) return {};
                break;
            }
            case FormulaOp::Sub: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !push({a.valid && b.valid, a.mul - b.mul, a.add - b.add})) return {};
                break;
            }
            case FormulaOp::Neg: {
                AffineFormulaInfo v = pop1();
                if (!push({v.valid, -v.mul, -v.add})) return {};
                break;
            }
            case FormulaOp::Mul: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b)) return {};
                if (is_constant(a) && b.valid) {
                    if (!push({true, a.add * b.mul, a.add * b.add})) return {};
                } else if (is_constant(b) && a.valid) {
                    if (!push({true, b.add * a.mul, b.add * a.add})) return {};
                } else {
                    return {};
                }
                break;
            }
            case FormulaOp::Div: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !a.valid || !is_constant(b)) return {};
                const double denom = b.add;
                const double inv = (denom == 0.0) ? std::numeric_limits<double>::quiet_NaN() : (1.0 / denom);
                if (!push({true, a.mul * inv, a.add * inv})) return {};
                break;
            }
            case FormulaOp::FuncAbs: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::fabs(x); })) return {};
                break;
            }
            case FormulaOp::FuncSqrt: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) {
                    return x < 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(x);
                })) return {};
                break;
            }
            case FormulaOp::FuncSin: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::sin(x); })) return {};
                break;
            }
            case FormulaOp::FuncCos: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::cos(x); })) return {};
                break;
            }
            case FormulaOp::FuncTan: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::tan(x); })) return {};
                break;
            }
            case FormulaOp::FuncLog: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) {
                    return x <= 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::log(x);
                })) return {};
                break;
            }
            case FormulaOp::FuncExp: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::exp(x); })) return {};
                break;
            }
        }
    }

    return stack.size() == 1 ? stack.back() : AffineFormulaInfo{};
}

double eval_formula_rpn(const std::vector<FormulaToken>& rpn, double x) {
    if (rpn.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (formula_rpn_is_identity(rpn)) return x;

    constexpr std::size_t kInlineStackCap = 32;
    std::array<double, kInlineStackCap> inline_stack{};
    std::vector<double> heap_stack;
    double* stack = inline_stack.data();
    std::size_t capacity = kInlineStackCap;
    if (rpn.size() > kInlineStackCap) {
        heap_stack.resize(rpn.size());
        stack = heap_stack.data();
        capacity = heap_stack.size();
    }

    std::size_t sp = 0;
    auto push = [&](double v) -> bool {
        if (sp >= capacity) return false;
        stack[sp++] = v;
        return true;
    };
    auto pop1 = [&]() -> double {
        if (sp == 0) return std::numeric_limits<double>::quiet_NaN();
        return stack[--sp];
    };
    auto pop2 = [&](double& a, double& b) -> bool {
        if (sp < 2) return false;
        b = stack[--sp];
        a = stack[--sp];
        return true;
    };

    for (const auto& token : rpn) {
        switch (token.op) {
            case FormulaOp::Number: if (!push(token.value)) return std::numeric_limits<double>::quiet_NaN(); break;
            case FormulaOp::Variable: if (!push(x)) return std::numeric_limits<double>::quiet_NaN(); break;
            case FormulaOp::Add: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a + b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Sub: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a - b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Mul: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a * b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Div: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(b == 0.0 ? std::numeric_limits<double>::quiet_NaN() : (a / b))) {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                break;
            }
            case FormulaOp::Neg: {
                double v = pop1();
                if (!std::isfinite(v) && !std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(-v)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::FuncAbs: {
                double v = pop1(); if (!push(std::fabs(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncSqrt: {
                double v = pop1(); if (!push(v < 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncSin: { double v = pop1(); if (!push(std::sin(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncCos: { double v = pop1(); if (!push(std::cos(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncTan: { double v = pop1(); if (!push(std::tan(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncLog: {
                double v = pop1(); if (!push(v <= 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::log(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncExp: { double v = pop1(); if (!push(std::exp(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
        }
    }
    return sp == 1 ? stack[0] : std::numeric_limits<double>::quiet_NaN();
}
