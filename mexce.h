/**
 * @file mexce.h
 * @brief Mini Expression Compiler/Evaluator.
 * @author Ioannis Makris
 *
 * mexce is a single-header, dependency-free runtime compiler for scalar
 * mathematical expressions.  It can compile and evaluate expressions at
 * runtime, emitting compact machine code that primarily uses the x87 FPU.
 * Subsequent calls to `evaluate()` jump directly to the generated code, so
 * evaluating the same expression repeatedly is fast.
 *
 * The library targets x86 (32-bit and 64-bit) Windows and Linux.  It requests a
 * writable/executable buffer with the appropriate platform APIs and relies on
 * the x87 floating-point stack, which makes it well suited for compact
 * numerical kernels and environments where adding a full parser is excessive.
 *
 * ### Basic usage
 *
 * ```cpp
 * float   x  = 0.0f;
 * double  y  = 0.1;
 * int     z  = 200;
 *
 * mexce::evaluator eval;
 *
 * // Associate runtime variables with their aliases in the expression.
 * eval.bind(x, "x", y, "y", z, "z");
 *
 * eval.set_expression("0.3+(-sin(2.33+x-logb((.3*pi+(88/y)/e),3.2+z)))/988.472e-02");
 *
 * std::cout << std::endl << "Evaluation results:" << std::endl;
 * for (int i = 0; i < 10; ++i, x -= 0.1f, y += 0.212, z += 2) {
 *     std::cout << "  " << eval.evaluate() << std::endl;
 * }
 * ```
 *
 * Output:
 *
 * ```
 * Evaluation results:
 *   0.200122
 *   0.210523
 *   0.224581
 *   0.240747
 *   0.258237
 *   0.276433
 *   0.294792
 *   0.312816
 *   0.330053
 *   0.346095
 * ```
 *
 * Bound variables are referenced directly — they must outlive the evaluator and
 * may be rebound or removed with `bind()`, `unbind()` or `unbind_all()`.  The
 * convenience overload `evaluate(expression)` can be used for single-shot
 * evaluations without permanently replacing the current expression.
 *
 * ### Expression syntax
 *
 * * Numeric literals may be written with decimal points or scientific
 *   notation.
 * * Unary `+` and `-` as well as the infix operators `+`, `-`, `*`, `/`, `^`
 *   (power) and the comparison operators `<` and `>` are supported.  Both yield
 *   `1` when the comparison is true and `0` otherwise.
 * * Parentheses and comma-separated argument lists follow familiar C-like
 *   rules.
 *
 * ### Built-in identifiers
 *
 * *Constants*: `pi` and `e` are always available.
 *
 * *Functions*: arguments are listed in call order.
 *   - `abs(x)` — absolute value.
 *   - `add(a, b)`, `sub(a, b)`, `mul(a, b)`, `div(a, b)` and `neg(x)` — basic
 *     arithmetic (the parser also maps `+`, `-`, `*`, `/` and unary `-` to
 *     these implementations).
 *   - `bias(x, a)` and `gain(x, a)` — common tone-mapping curves defined for
 *     inputs in the `[0, 1]` range.
 *   - `bnd(x, period)` — periodic wrap similar to `fmod`, returning `x`
 *     reduced to the `[0, period)` interval.
 *   - `ceil(x)`, `floor(x)`, `round(x)` and `int(x)` — rounding helpers.
 *   - `cos(x)`, `sin(x)`, `tan(x)` — trigonometric functions (optionally using
 *     polynomial refinements when `MEXCE_ACCURACY` is defined).
 *   - `exp(x)` and `pow(a, b)` — base-e exponent and exponentiation.
 *   - `expn(x)` — exponent part of `x`, and `sfc(x)` — significand/fractional
 *     component of `x` in the range `[0.5, 1)`.
 *   - `log(x)`/`ln(x)`, `log2(x)`, `log10(x)`, `logb(base, value)` and
 *     `ylog2(y, x)` (`y * log2(x)`).
 *   - `max(a, b)`, `min(a, b)` and `mod(a, b)`.
 *   - `sign(x)` — returns `1` for positive values and `-1` otherwise.
 *   - `signp(x)` — returns `1` for positive values and `0` when `x <= 0`.
 *   - `sqrt(x)` — square root.
 *
 * The parser rejects attempts to bind variables that collide with reserved
 * names.  Additional constants produced during simplification are stored inside
 * the evaluator and re-used across compilations of the same instance.
 *
 * ### API surface
 *
 * * `bind()`/`unbind()`/`unbind_all()` associate C++ lvalues of type
 *   `double`, `float`, `int16_t`, `int32_t` or `int64_t` with symbolic names.
 * * `set_expression()` compiles a new expression; syntax errors throw
 *   `mexce_parsing_exception` with the offending position while semantic issues
 *   raise `std::logic_error`.
 * * `evaluate()` executes the previously compiled expression.  Constant
 *   expressions are folded during compilation and return immediately.
 */


#ifndef MEXCE_INCLUDED
#define MEXCE_INCLUDED

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <iomanip>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


#if defined(_M_X64) || defined(__x86_64__)
    #define MEXCE_64
#elif defined(_M_IX86) || defined(__i386__)
    #define MEXCE_32
#else
    #error Unknown CPU architecture
#endif

#ifdef _WIN32
    #include <Windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <sys/mman.h>
#endif



namespace mexce {


class evaluator;


namespace impl {

    struct Element;
    struct Constant;
    struct Variable;
    struct Function;
    struct mexce_charstream;

    using std::abs;
    using std::deque;
    using std::exception;
    using std::get;
    using std::list;
    using std::logic_error;
    using std::make_pair;
    using std::make_shared;
    using std::make_tuple;
    using std::map;
    using std::next;
    using std::pair;
    using std::shared_ptr;
    using std::string;
    using std::stringstream;
    using std::vector;

    using constant_map_t    = map<string, shared_ptr<Constant> >;
    using variable_map_t    = map<string, shared_ptr<Variable> >;
    using elist_t           = list<Element>;
    using elist_it_t        = elist_t::iterator;
    using elist_const_it_t  = elist_t::const_iterator;

    shared_ptr<Constant> make_intermediate_constant(evaluator* ev, double v);
    uint8_t* push_intermediate_code(evaluator* ev, const std::string& s);
    shared_ptr<Function> make_function(evaluator* ev, const string& name);
    void asmd_optimizer(elist_it_t it, evaluator* ev, elist_t* elist);
    void pow_optimizer(elist_it_t it, evaluator* ev, elist_t* elist);
    pair<elist_it_t, elist_it_t> get_dependent_chunk(elist_it_t it);
}



class evaluator
{
public:

    evaluator();
    ~evaluator();

    // Prevent accidental double-free of executable memory buffer (Rule of Three)
    evaluator(const evaluator&) = delete;
    evaluator& operator=(const evaluator&) = delete;

    template <typename T, typename ...Args>
    void bind(T& referenced_variable, const std::string& variable_name, Args&... args);

    template <typename ...Args>
    void unbind(const std::string& variable_name, Args&... args);

    void unbind_all();

    void set_expression(std::string);

    double evaluate();

    double evaluate(const std::string& expression);

private:

    bool                    is_constant_expression      = false;
    double                  constant_expression_value   = 0.0;
    size_t                  m_buffer_size               = 0;
    std::string             m_expression;
    impl::elist_t           m_elist;
    std::list<std::string>  m_intermediate_code;
    impl::constant_map_t    m_intermediate_constants;  // produced during expression simplification
    impl::variable_map_t    m_variables;
    impl::constant_map_t    m_constants;
    uint64_t                m_next_element_id           = 0;

    // Map FunctionID -> { Coefficient, Kernel }
    std::map<uint64_t, std::pair<double, impl::elist_t>> m_linear_terms;

    // Map FunctionID -> { Kernel, Exponent }
    // Used to optimize (a^b)^c -> a^(b*c)
    std::map<uint64_t, std::pair<impl::elist_t, double>> m_power_terms;

    double                (*evaluate_fptr)()            = nullptr;

    void compile_and_finalize_elist(impl::elist_const_it_t first, impl::elist_const_it_t last);

    // Grant friend access to helpers that need private state.
    friend std::shared_ptr<impl::Constant> impl::make_intermediate_constant(evaluator* ev, double v);
    friend std::shared_ptr<impl::Function> impl::make_function(evaluator* ev, const std::string& name);
    friend void impl::asmd_optimizer(impl::elist_it_t it, evaluator* ev, impl::elist_t* elist);
    friend void impl::pow_optimizer(impl::elist_it_t it, evaluator* ev, impl::elist_t* elist);
    friend uint8_t* impl::push_intermediate_code(evaluator* ev, const std::string& s);


    template <typename = void> void bind() {}
    template <typename = void> void unbind() {}
};



class mexce_parsing_exception: public std::exception
{
public:
    explicit mexce_parsing_exception(const std::string& message, size_t position):
        m_message(message),
        m_position(position)
    {}

    virtual ~mexce_parsing_exception()  { }
    virtual const char* what() const throw() { return m_message.c_str(); }

protected:
    std::string             m_message;
    size_t                  m_position;
};


inline
double evaluator::evaluate(const std::string& expression)
{
    evaluator ev;
    ev.m_variables = m_variables;
    ev.set_expression(expression);
    return ev.evaluate();
}



namespace impl {


#ifdef _WIN32
inline
size_t get_page_size()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}
#endif


inline
uint8_t* get_executable_buffer(size_t sz)
{
#ifdef _WIN32
    (void)sz; // prevent warning
    return (uint8_t*)VirtualAlloc(0, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__linux__)
    return (uint8_t*)mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}


inline
double (*lock_executable_buffer(uint8_t* buffer, size_t sz))()
{
    if (sz == 0) {
        throw std::runtime_error("lock_executable_buffer requires a non-zero size");
    }
#ifdef _WIN32
    DWORD old_protect = 0;
    if (!VirtualProtect(buffer, sz, PAGE_EXECUTE_READ, &old_protect)) {
        VirtualFree((void*)buffer, 0, MEM_RELEASE);
        throw std::runtime_error("VirtualProtect(PAGE_EXECUTE_READ) failed");
    }
    FlushInstructionCache(GetCurrentProcess(), buffer, sz);
#elif defined(__linux__)
    if (mprotect((void*)buffer, sz, PROT_READ | PROT_EXEC) != 0) {
        munmap((void*)buffer, sz);
        throw std::runtime_error("mprotect(PROT_READ|PROT_EXEC) failed");
    }
#endif
    return reinterpret_cast<double (*)()>(buffer);
}


inline
void free_executable_buffer(double (*buffer)(), size_t sz)
{
    if (!buffer) {
        return;
    }
#ifdef _WIN32
    (void)sz; // prevent warning
    VirtualFree( (void*) buffer, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap((void *) buffer, sz);
#endif
}


enum Numeric_data_type
{
    M16INT,
    M32INT,
    M64INT,
    M32FP,
    M64FP,
};


enum class Element_type
{
    CCONST,
    CVAR,
    CFUNC
};


enum Token_type
{
    UNDEFINED_TOKEN_TYPE,
    NUMERIC_LITERAL,
    CONSTANT_NAME,
    VARIABLE_NAME,
    FUNCTION_NAME,
    INFIX_1,            // infix operator, with priority 1 ( '^', i.e. power )
    INFIX_2,            // infix operator, with priority 2 ( '*' and '/' )
    INFIX_3,            // infix operator, with priority 3 ( '+' and '-' )
    INFIX_4,            // infix operator, with priority 4 ( '<' and '>' )
    RIGHT_PARENTHESIS,
    LEFT_PARENTHESIS,
    COMMA,
    FUNCTION_RIGHT_PARENTHESIS,
    FUNCTION_LEFT_PARENTHESIS,
    UNARY
};



inline
string double_to_hex( double v )
{
    uint64_t u64;
    static_assert(sizeof(u64) == sizeof(v), "double and uint64_t size mismatch");
    memcpy(&u64, &v, sizeof(v));

    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016" PRIx64, u64);
    return string(buf);
}


struct Constant
{
    uint64_t          id;
    string            name;
    double            value;
    // Members to provide a common interface with Variable for codegen helpers
    volatile void*    address;
    Numeric_data_type numeric_data_type;

    Constant(uint64_t i, string num, string n):
        id(i), name(n), value(atof(num.data())),
        address((void*)&this->value), numeric_data_type(M64FP)
    {}

    Constant(uint64_t i, double v):
        id(i), name(double_to_hex(v)), value(v),
        address((void*)&this->value), numeric_data_type(M64FP)
    {}
};


struct Variable
{
    uint64_t          id;
    string            name;
    bool              referenced;
    // Members to provide a common interface with Constant for codegen helpers
    volatile void*    address;
    Numeric_data_type numeric_data_type;

    Variable(uint64_t i, volatile void* addr, string n, Numeric_data_type ndt):
        id(i), name(n), referenced(false), address(addr), numeric_data_type(ndt)
    {}
};


struct Function
{
    using optimizer_t = void (*)(elist_it_t, evaluator*, elist_t*);

    uint64_t            id;
    size_t              stack_req;
    string              name;
    size_t              num_args;

    vector<elist_it_t>  args;
    elist_it_t          parent;
    size_t              parent_arg_index = size_t(~0); // index in postfix order (inverted), i.e. arg1-arg0

    list<elist_t>       absorbed[2];

    string              code;
    optimizer_t         optimizer;

    bool                force_not_constant = false;

    Function(
        uint64_t    i,
        const string& n,
        size_t      n_args,
        size_t      sreq,
        size_t      size,
        uint8_t    *code_buffer,
        optimizer_t opt = 0)
    :
        id        ( i                           ),
        stack_req ( sreq                        ),
        name      ( n                           ),
        num_args  ( n_args                      ),
        args      ( vector<elist_it_t>(n_args)  ),
        code      ( (char*)code_buffer, size    ),
        optimizer ( opt                         ) {}
};


struct Element {
    Element_type type;
    uint64_t     id;

    shared_ptr<Constant> c;
    shared_ptr<Variable> v;
    shared_ptr<Function> f;

    Element(shared_ptr<Constant> c_ptr)
        : type(Element_type::CCONST), id(c_ptr->id), c(c_ptr) {}
    Element(shared_ptr<Variable> v_ptr)
        : type(Element_type::CVAR), id(v_ptr->id), v(v_ptr) {}
    Element(shared_ptr<Function> f_ptr)
        : type(Element_type::CFUNC), id(f_ptr->id), f(f_ptr) {}

    // Default constructor to allow creation in containers
    Element(): type(Element_type::CCONST), id(0) {}
};



struct mexce_charstream {
    vector<uint8_t> buf;
    mexce_charstream() { buf.reserve(256); }
    string str() const { return string(buf.begin(), buf.end()); }
    void write(const char* data, size_t n) {
        buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + n);
    }
};

template<typename T>
mexce_charstream& operator << (mexce_charstream &s, T data) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&data);
    s.buf.insert(s.buf.end(), p, p + sizeof(T));
    return s;
}

inline
mexce_charstream& operator < (mexce_charstream &s, int v) {
    s.buf.push_back(static_cast<uint8_t>(v));
    return s;
}



inline
shared_ptr<Constant> make_intermediate_constant(evaluator* ev, double v)
{
    auto name = double_to_hex(v);
    auto it = ev->m_intermediate_constants.find(name);
    if (it == ev->m_intermediate_constants.end()) {
        auto sc = make_shared<Constant>(ev->m_next_element_id++, v);
        ev->m_intermediate_constants[name] = sc;
        return sc;
    }
    else {
        return it->second;
    }
}



inline
uint8_t* push_intermediate_code(evaluator* ev, const string& s)
{
    ev->m_intermediate_code.push_back(string());
    ev->m_intermediate_code.back() = s;
    return (uint8_t*)&ev->m_intermediate_code.back()[0];
}



inline
void link_arguments(elist_t& elist)
{
    vector<elist_it_t> evec;
    for (auto y = elist.begin(); y != elist.end(); y++) {
        if (y->type == Element_type::CFUNC) {
            auto f = y->f;
            f->parent = elist.end();
            for (size_t i = 0; i < f->num_args; i++) {
                f->args[i] = evec.back();
                if (f->args[i]->type == Element_type::CFUNC) {
                    auto cf = f->args[i]->f;
                    cf->parent = y;
                    cf->parent_arg_index = i; // postfix order (inverted)
                }
                evec.pop_back();
            }
        }
        evec.push_back(y);
    }
}


inline
Token_type get_infix_rank(char infix_op)
{
    auto ret = UNDEFINED_TOKEN_TYPE;
    switch (infix_op) {
        case '<':
        case '>': ret = INFIX_4; break;
        case '+':
        case '-': ret = INFIX_3; break;
        case '*':
        case '/': ret = INFIX_2; break;
        case '^': ret = INFIX_1; break;
    }
    assert(ret != UNDEFINED_TOKEN_TYPE);
    return ret;
}


#if defined(MEXCE_ACCURACY)
// 80-bit Maclaurin coeffs as 16-byte records (mantissa 8 + exp/sign 2 + pad)
static uint64_t mexce_trig_mfactors[] = {
#if (MEXCE_ACCURACY > 9)
    0x9c9962823eb07306, 0x000000000000bf93,     // -1/(30!)
#endif
#if (MEXCE_ACCURACY > 8)
    0x850c5131a842e9ba, 0x0000000000003f9d,     // +1/(28!)
#endif
#if (MEXCE_ACCURACY > 7)
    0xc4742fe35272cd1c, 0x000000000000bfa6,     // -1/(26!)
#endif
#if (MEXCE_ACCURACY > 6)
    0xf96780cb97abbe65, 0x0000000000003faf,     // +1/(24!)
#endif
#if (MEXCE_ACCURACY > 5)
    0x8671cb6dbfc294a3, 0x000000000000bfb9,     // -1/(22!)
#endif
#if (MEXCE_ACCURACY > 4)
    0xf2a15d201011283d, 0x0000000000003fc1,     // +1/(20!)
#endif
#if (MEXCE_ACCURACY > 3)
    0xb413c31dcbecbbde, 0x000000000000bfca,     // -1/(18!)
#endif
#if (MEXCE_ACCURACY > 2)
    0xd73f9f399dc0f88f, 0x0000000000003fd2,     // +1/(16!)
#endif
#if (MEXCE_ACCURACY > 1)
    0xc9cba54603e4e906, 0x000000000000bfda,     // -1/(14!)
#endif
#if (MEXCE_ACCURACY > 0)
    0x8f76c77fc6c4bdaa, 0x0000000000003fe2,     // +1/(12!)
#endif
    0x93f27dbbc4fae397, 0x000000000000bfe9,     // -1/(10!)
    0xd00d00d00d00d00d, 0x0000000000003fef,     // +1/( 8!)
    0xb60b60b60b60b60b, 0x000000000000bff5,     // -1/( 6!)
    0xaaaaaaaaaaaaaaab, 0x0000000000003ffa,     // +1/( 4!)
    0x8000000000000000, 0x000000000000bffe,     // -1/( 2!)
    0x8000000000000000, 0x0000000000003fff      // +1
};

// Arch-specific tiny opcode helpers (NO #if inside their bodies)
#ifdef MEXCE_64
#   define MEXCE_MOV_BASE_IMM   0x48,0xB8, 0,0,0,0,0,0,0,0     /* mov rax, imm64 */
#   define MEXCE_ADD_BASE_80    0x48,0x05, 0x80,0x00,0x00,0x00 /* add rax, 80h   */
#   define MEXCE_FLD_BASE0      0xDB,0x28                      /* fld tword [rax]*/
#   define MEXCE_FLD_BASE(d)    0xDB,0x68,(d)                  /* fld [rax+d]    */
#else
#   define MEXCE_MOV_BASE_IMM   0xB8, 0,0,0,0                  /* mov eax, imm32 */
#   define MEXCE_ADD_BASE_80    0x05, 0x80,0x00,0x00,0x00      /* add eax, 80h   */
#   define MEXCE_FLD_BASE0      0xDB,0x28                      /* fld tword [eax]*/
#   define MEXCE_FLD_BASE(d)    0xDB,0x68,(d)                  /* fld [eax+d]    */
#endif

// Shared snippets
#define MEXCE_TRIG_RANGE_REDUCE  0xD9,0xEB, 0xD8,0xC0, 0xD9,0xC9, 0xD9,0xF5, 0xDD,0xD9
#define MEXCE_TRIG_Y_SQUARED     0xDC,0xC8

// t = x − π/2 using FSCALE (avoid FDIV)
// Stack dance: push -1, scale π by 2^{-1}, then subtract from x.
#define MEXCE_SIN_PRESHIFT \
    0xD9,0xE8, /* fld1          */ \
    0xD9,0xE0, /* fchs   => -1  */ \
    0xD9,0xEB, /* fldpi         */ \
    0xD9,0xFD, /* fscale => π/2 */ \
    0xDD,0xD9, /* fstp  st(1)   (pop -1) */ \
    0xDE,0xE9  /* fsubp st(1), st => x - π/2 */

#endif // MEXCE_ACCURACY


inline Function Cos()
{
#ifndef MEXCE_ACCURACY
    uint8_t code[] = { 0xD9, 0xFF }; // fcos
    return Function(0, "cos", 1, 0, sizeof(code), code);
#else
    // Range reduce, y=x^2, base -> coeff table, Horner on y
    uint8_t code[] = {
        MEXCE_TRIG_RANGE_REDUCE,
        MEXCE_TRIG_Y_SQUARED,
        MEXCE_MOV_BASE_IMM,

        // Horner (fixed part)
        MEXCE_FLD_BASE0,           0xD8,0xC9, MEXCE_FLD_BASE(0x10), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x20), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x30), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x40), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x50), 0xDE,0xC1,

        // Optional deeper terms (compile-time gated) — placed OUTSIDE of macros
#if (MEXCE_ACCURACY > 0)
        0xD8,0xC9, MEXCE_FLD_BASE(0x60), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 1)
        0xD8,0xC9, MEXCE_FLD_BASE(0x70), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 2)
        0xD8,0xC9, MEXCE_ADD_BASE_80, MEXCE_FLD_BASE0, 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 3)
        0xD8,0xC9, MEXCE_FLD_BASE(0x10), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 4)
        0xD8,0xC9, MEXCE_FLD_BASE(0x20), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 5)
        0xD8,0xC9, MEXCE_FLD_BASE(0x30), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 6)
        0xD8,0xC9, MEXCE_FLD_BASE(0x40), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 7)
        0xD8,0xC9, MEXCE_FLD_BASE(0x50), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 8)
        0xD8,0xC9, MEXCE_FLD_BASE(0x60), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 9)
        0xD8,0xC9, MEXCE_FLD_BASE(0x70), 0xDE,0xC1,
#endif
        0xDD,0xD9 // fstp st(1)
    };
#   ifdef MEXCE_64
    // imm64 starts at offset 14 (10 bytes reduce + 2 bytes y^2 + 2-byte opcode)
    *((void**)(code + 14)) = (void*)mexce_trig_mfactors;
#   else
    // imm32 starts at offset 13 (10 + 2 + 1)
    *((void**)(code + 13)) = (void*)mexce_trig_mfactors;
#   endif
    return Function(0, "cos", 1, 0, sizeof(code), code);
#endif
}



inline Function Sin()
{
#ifndef MEXCE_ACCURACY
    uint8_t code[] = { 0xD9, 0xFE }; // fsin
    return Function(0, "sin", 1, 0, sizeof(code), code);
#else
    uint8_t code[] = {
        // t = x − π/2 to reuse cosine polynomial
        MEXCE_SIN_PRESHIFT,
        MEXCE_TRIG_RANGE_REDUCE,
        MEXCE_TRIG_Y_SQUARED,
        MEXCE_MOV_BASE_IMM,

        // Same Horner ladder as in Cos()
        MEXCE_FLD_BASE0,           0xD8,0xC9, MEXCE_FLD_BASE(0x10), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x20), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x30), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x40), 0xDE,0xC1,
        0xD8,0xC9, MEXCE_FLD_BASE(0x50), 0xDE,0xC1,
#if (MEXCE_ACCURACY > 0)
        0xD8,0xC9, MEXCE_FLD_BASE(0x60), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 1)
        0xD8,0xC9, MEXCE_FLD_BASE(0x70), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 2)
        0xD8,0xC9, MEXCE_ADD_BASE_80, MEXCE_FLD_BASE0, 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 3)
        0xD8,0xC9, MEXCE_FLD_BASE(0x10), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 4)
        0xD8,0xC9, MEXCE_FLD_BASE(0x20), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 5)
        0xD8,0xC9, MEXCE_FLD_BASE(0x30), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 6)
        0xD8,0xC9, MEXCE_FLD_BASE(0x40), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 7)
        0xD8,0xC9, MEXCE_FLD_BASE(0x50), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 8)
        0xD8,0xC9, MEXCE_FLD_BASE(0x60), 0xDE,0xC1,
#endif
#if (MEXCE_ACCURACY > 9)
        0xD8,0xC9, MEXCE_FLD_BASE(0x70), 0xDE,0xC1,
#endif
        0xDD,0xD9 // fstp st(1)
    };
#   ifdef MEXCE_64
    // imm64 starts at offset 26 (10 pre-shift + 10 reduce + 2 y^2 + 2-byte opcode)
    *((void**)(code + 26)) = (void*)mexce_trig_mfactors;
#   else
    // imm32 starts at offset 25 (10 + 10 + 2 + 1)
    *((void**)(code + 25)) = (void*)mexce_trig_mfactors;
#   endif
    return Function(0, "sin", 1, 0, sizeof(code), code);
#endif
}


inline Function Tan()
{
    uint8_t code[] = {
        0xd9, 0xf2,                                 // fptan
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function(0, "tan", 1, 1, sizeof(code), code);
}


inline Function Abs()
{
    uint8_t code[] = {
        0xd9, 0xe1                                  // fabs
    };
    return Function(0, "abs", 1, 0, sizeof(code), code);
}


inline Function Sfc()
{
    uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "sfc", 1, 1, sizeof(code), code);
}


inline Function Expn()
{
    uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function(0, "expn", 1, 1, sizeof(code), code);
}


inline Function Sign()
{
    uint8_t code[]  =  {
        0xd9, 0xee,                                 // fldz
        0xdf, 0xf1,                                 // fcomip      st, st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xe0,                                 // fchs        ; default -1
        0xd9, 0xee,                                 // fldz
        0xd9, 0xc9,                                 // fxch        st(1)
        0xda, 0xc9,                                 // fcmove      st, st(1)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xda, 0xc1,                                 // fcmovb      st, st(1)
        0xdd, 0xd9,                                 // fstp        st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "sign", 1, 1, sizeof(code), code);
}


inline Function Signp()
{
    uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xdb, 0xf2,                                 // fcomi       st, st(2)
        0xdd, 0xda,                                 // fstp        st(2)
        0xdb, 0xc1,                                 // fcmovnb     st, st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "signp", 1, 2, sizeof(code), code);
}


inline Function Sqrt()
{
    uint8_t code[] = {
        0xd9, 0xfa                                  // fsqrt
    };
    return Function(0, "sqrt", 1, 0, sizeof(code), code);
}


inline
void pow_optimizer(elist_it_t it, evaluator* ev, elist_t* elist)
{
    auto f = it->f;

    // ---------------------------------------------------------
    // Optimization: Nested Power Folding (a^b)^c -> a^(b*c)
    // ---------------------------------------------------------
    // In postfix order: args[0] = exponent, args[1] = base
    if (f->args[0]->type == Element_type::CCONST) {
        double current_exp = f->args[0]->c->value;

        // Check if the base is a Function that we previously recorded as a Power term
        if (f->args[1]->type == Element_type::CFUNC) {
            uint64_t base_id = f->args[1]->f->id;
            auto map_it = ev->m_power_terms.find(base_id);

            if (map_it != ev->m_power_terms.end()) {
                // Found nested power!
                double prev_exp = map_it->second.second;
                double new_exp = prev_exp * current_exp; // Fold exponents

                // Update the exponent constant element in the list
                *f->args[0] = Element(make_intermediate_constant(ev, new_exp));

                // Replace the base (which is a single Function element)
                // with the original kernel (which is a list of elements).
                auto base_it = f->args[1];

                // Splice the kernel list into the main list right before the current base
                for (const auto& e : map_it->second.first) {
                    elist->insert(base_it, e);
                }

                // Update f->args[1] to point to the *last element* of the inserted kernel.
                f->args[1] = std::prev(base_it);

                // Erase the old intermediate base function
                elist->erase(base_it);
            }
        }
    }

    // ---------------------------------------------------------
    // Existing Optimization: Constant Exponent
    // ---------------------------------------------------------
    if (f->args[0]->type == Element_type::CCONST) {
        auto v = f->args[0]->c;

        double v_d = v->value;
        double r_d = round(v_d);
        double a_d = abs(v_d);

        bool matched = true;
        mexce_charstream s;

        // a special case, that the exponent is 0.5
        if (v_d == 0.5) {
            s < 0xd9 < 0xfa;                // fsqrt
        }
        else
        if (v_d == -0.5) {
            s < 0xd9 < 0xfa                 // fsqrt
              < 0xd9 < 0xe8                 // fld1
              < 0xde < 0xf1;                // fdivrp st(1), st
        }
        else
        if (r_d == v_d && a_d <= 65536.0) {

            uint32_t exponent = static_cast<uint32_t>(a_d);
            bool invert = (v_d < 0.0);

            if (exponent == 0) {
                s < 0xdd < 0xd8             // fstp st(0)
                  < 0xd9 < 0xe8;            // fld1
            }
            else {
                if (exponent > 1) {
                    if (exponent == 2) {
                        s < 0xdc < 0xc8;        // fmul st(0), st(0)
                    }
                    else {
                        uint32_t remaining = exponent;

                        s < 0xd9 < 0xe8         // fld1
                          < 0xd9 < 0xc9;        // fxch        (base in st0, result in st1)

                        while (true) {
                            if (remaining & 1U) {
                                s < 0xdc < 0xc9;    // fmul st(1), st
                            }

                            remaining >>= 1;
                            if (!remaining) {
                                break;
                            }

                            s < 0xdc < 0xc8;        // fmul st(0), st(0)
                        }

                        s < 0xd9 < 0xc9           // fxch        (result to st0)
                          < 0xdd < 0xd9;          // fstp st(1)
                    }
                }

                if (invert) {
                    s < 0xd9 < 0xe8           // fld1
                      < 0xde < 0xf1;          // fdivrp st(1), st
                }
            }
        }
        else {
            matched = false;
        }

        if (!matched) {
            // this is almost the generic pow, except that it does not try to figure out
            // if the exponent is an integer

            s < 0xd9 < 0xc9                         // fxch                                 }
              < 0xd9 < 0xe4                         // ftst                                 }
              < 0x9b                                // wait                                 } if base is 0, leave it in st(0)
              < 0xdf < 0xe0                         // fnstsw      ax                       } and exit
              < 0x9e                                // sahf                                 }
              < 0x74 < 0x14                         // je          store_and_exit           }
              < 0xd9 < 0xe1                         // fabs
              < 0xd9 < 0xf1                         // fyl2x                                }
              < 0xd9 < 0xe8                         // fld1                                 }
              < 0xd9 < 0xc1                         // fld         st(1)                    }
              < 0xd9 < 0xf8                         // fprem                                } b^n = 2^(n*log2(b))
              < 0xd9 < 0xf0                         // f2xm1                                }
              < 0xde < 0xc1                         // faddp       st(1), st                }
              < 0xd9 < 0xfd                         // fscale                               }
              < 0x77 < 0x02                         // ja          store_and_exit
              < 0xd9 < 0xe0                         // fchs
// store_and_exit:
              < 0xdd < 0xd9;                        // fstp        st(1)
        }


        uint8_t* cc = push_intermediate_code(ev, s.str());
        auto f_opt = make_shared<Function>(ev->m_next_element_id++, "pow_opt", 2-matched, 0, s.buf.size(), cc, nullptr);

        if (matched) {
            f_opt->args.resize(1);
            f_opt->args[0] = f->args[1];
            elist->erase(f->args[0]);
        }
        else {
            f_opt->args[0] = f->args[0];
            f_opt->args[1] = f->args[1];
        }
        *it = Element(f_opt);

        // ---------------------------------------------------------
        // Side-Channel Recording (Writer)
        // ---------------------------------------------------------
        // Record this power term so parents can fold it.
        // e.g., we just compiled "a^2". Record it so "(a^2)^3" can see it.
        double exp_val = f->args[0]->c->value;

        // The kernel is the chunk defining the base.
        auto chunk = get_dependent_chunk(f->args[1]);
        elist_t kernel_list(chunk.first, chunk.second);

        ev->m_power_terms[f_opt->id] = std::make_pair(kernel_list, exp_val);
    }
}


inline Function Pow()
{
    uint8_t code[]  =  {
        0xd9, 0xc0,                                 // fld         st(0)                    }
        0xd9, 0xfc,                                 // frndint                              }
        0xd8, 0xd1,                                 // fcom        st(1)                    } if (abs(exponent) != round(abs(exponent)))
        0xdf, 0xe0,                                 // fnstsw      ax                       }    goto generic_pow;
        0x9e,                                       // sahf                                 }
        0x75, 0x3c,                                 // jne         pop_before_generic_pow   }

        0xd9, 0xe1,                                 // fabs                                 }
        0x66, 0xc7, 0x44, 0x24, 0xfe, 0xff, 0xff,   // mov         word ptr [esp-2],0ffffh  }
        0xdf, 0x5c, 0x24, 0xfe,                     // fistp       word ptr [esp-2]         }
        0x66, 0x8b, 0x44, 0x24, 0xfe,               // mov         ax, word ptr [esp-2]     } if (abs(exponent) > 32)
        0x66, 0x83, 0xe8, 0x01,                     // sub         ax, 1                    }    goto generic_pow;
        0x66, 0x83, 0xf8, 0x21,                     // cmp         ax, 1fh                  }
        0x77, 0x22,                                 // ja          generic_pow              }

        0xd9, 0xc1,                                 // fld         st(1)
// loop_start:
        0x66, 0x85, 0xc0,                           // test        ax, ax
        0x74, 0x08,                                 // je          loop_end
        0xdc, 0xca,                                 // fmul        st(2), st
        0x66, 0x83, 0xe8, 0x01,                     // sub         ax, 1
        0xeb, 0xf3,                                 // jmp         loop_start

// loop_end:

        0xdd, 0xd8,                                 // fstp        st(0)                    }
        0xd9, 0xe4,                                 // ftst                                 }
        0xdf, 0xe0,                                 // fnstsw      ax                       } if the exponent was NOT negative
        0x9e,                                       // sahf                                 }     goto exit_point
        0xdd, 0xd8,                                 // fstp        st(0)                    }
        0x77, 0x36,                                 // ja          exit_point               }

        0xd9, 0xe8,                                 // fld1                                 }
        0xde, 0xf1,                                 // fdivrp      st(1),st                 } inverse
        0xeb, 0x30,                                 // jmp         exit_point               }

// pop_before_generic_pow:
        0xdd, 0xd8,                                 // fstp        st(0)
// generic_pow:
        0xd9, 0xe4,                                 // ftst                                 }
        0xdf, 0xe0,                                 // fnstsw      ax                       }
        0x9e,                                       // sahf                                 }
        0x75, 0x08,                                 // jne         non_zero_exponent        } if exponent is 0
        0xdd, 0xd8,                                 // fstp        st(0)                    } return 1
        0xdd, 0xd8,                                 // fstp        st(0)                    }
        0xd9, 0xe8,                                 // fld1                                 }
        0xeb, 0x1f,                                 // jmp         exit_point               }
// non_zero_exponent:
        0xd9, 0xc9,                                 // fxch                                 }
        0xd9, 0xe4,                                 // ftst                                 }
        0xdf, 0xe0,                                 // fnstsw      ax                       } if base is 0, leave it in st(0)
        0x9e,                                       // sahf                                 } and exit
        0x74, 0x14,                                 // je          store_and_exit           }
        0xd9, 0xe1,                                 // fabs
        0xd9, 0xf1,                                 // fyl2x                                }
        0xd9, 0xe8,                                 // fld1                                 }
        0xd9, 0xc1,                                 // fld         st(1)                    }
        0xd9, 0xf8,                                 // fprem                                } b^n = 2^(n*log2(b))
        0xd9, 0xf0,                                 // f2xm1                                }
        0xde, 0xc1,                                 // faddp       st(1), st                }
        0xd9, 0xfd,                                 // fscale                               }
        0x77, 0x02,                                 // ja          store_and_exit
        0xd9, 0xe0,                                 // fchs
// store_and_exit:
        0xdd, 0xd9,                                 // fstp        st(1)
// exit_point:
    };
    return Function(0, "pow", 2, 1, sizeof(code), code, pow_optimizer);
}


inline Function Exp()
{
    uint8_t code[]  =  {
        0xd9, 0xea,                                 // fldl2e
        0xde, 0xc9,                                 // fmulp       st(1), st
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc1,                                 // fld         st(1)
        0xd9, 0xf8,                                 // fprem
        0xd9, 0xf0,                                 // f2xm1
        0xde, 0xc1,                                 // faddp       st(1), st
        0xd9, 0xfd,                                 // fscale
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "exp", 1, 1, sizeof(code), code);
}


inline Function Logb()  // implementation with base
{
    uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function(0, "logb", 2, 1, sizeof(code), code);
}


inline Function Ln()
{
    uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xea,                                 // fldl2e
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function(0, "ln", 1, 1, sizeof(code), code);
}


// this is an alias, because of C's math.h
inline Function Log()
{
    Function f = Ln();
    f.name="log";
    return f;
}


inline Function Log10()
{
    uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xe9,                                 // fldl2t
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function(0, "log10", 1, 1, sizeof(code), code);
}


inline Function Log2()
{
    uint8_t code[] = {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1                                  // fyl2x
    };
    return Function(0, "log2", 1, 0, sizeof(code), code);
}


inline Function Ylog2()
{
    uint8_t code[] = {
        0xd9, 0xf1                                  // fyl2x
    };
    return Function(0, "ylog2", 2, 0, sizeof(code), code);
}


inline Function Max()
{
    uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "max", 2, 0, sizeof(code), code);
}


inline Function Min()
{
    uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xd9, 0xc9,                                 // fxch        st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "min", 2, 0, sizeof(code), code);
}


inline Function Floor()
{
    uint8_t code[] = {
        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x06,   // mov         word ptr [esp-4], 67fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function(0, "floor", 1, 0, sizeof(code), code);
}


inline Function Ceil()
{
    uint8_t code[] = {
        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x0a,   // mov         word ptr [esp-4], a7fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function(0, "ceil", 1, 0, sizeof(code), code);
}


inline Function Round()
{
    uint8_t code[] = {

        // NOTE: In this case, saving/restoring the control word is most likely redundant.

        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x02,   // mov         word ptr [esp-4], 27fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function(0, "round", 1, 0, sizeof(code), code);
}


inline Function Int()
{
    uint8_t code[] = {
        0xd9, 0xfc                                  // frndint
    };
    return Function(0, "int", 1, 0, sizeof(code), code);
}


inline Function Mod()
{
    uint8_t code[] = {
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf8,                                 // fprem
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "mod", 2, 0, sizeof(code), code);
}


inline Function Lt()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)   ; compare b vs a
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xdb, 0xc1,                                 // fcmova      st,st(1)   ; b > a  ⇒ a < b
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "lt", 2, 0, sizeof(code), code);
}

inline Function Le()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xdb, 0xc1,                                 // fcmovnb
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "le", 2, 0, sizeof(code), code);
}

inline Function Gt()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xda, 0xc1,                                 // fcmovb      st,st(1)   ; b < a  ⇒ a > b
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "gt", 2, 0, sizeof(code), code);
}

inline Function Ge()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xda, 0xd1,                                 // fcmovbe     st,st(1)   ; b ≤ a ⇒ a ≥ b
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "ge", 2, 0, sizeof(code), code);
}


inline Function Eq()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xda, 0xc9,                                 // fcmove      st,st(1)
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "eq", 2, 0, sizeof(code), code);
}


inline Function Ne()
{
    uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xdb, 0xc9,                                 // fcmovne     st,st(1)
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function(0, "ne", 2, 0, sizeof(code), code);
}



inline Function Bnd()
{
    uint8_t code[] = {
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf8,                                 // fprem
        0xd9, 0xc0,                                 // fld         st(0)
        0xdc, 0xc2,                                 // fadd        st(2), st
        0xd9, 0xee,                                 // fldz
        0xdf, 0xf1,                                 // fcomip      st,st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xdb, 0xc1,                                 // fcmovnb     st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function(0, "bnd", 2, 2, sizeof(code), code);
}


inline
pair<elist_it_t, elist_it_t> get_dependent_chunk(elist_it_t it)
{
    auto it_end = next(it);
    while (it->type == Element_type::CFUNC) {
        shared_ptr<Function> f = it->f;
        if (!f->args.size()) {
            break;
        }
        it = f->args[f->args.size()-1];
    }
    return make_pair(it, it_end);
}


struct elist_comparison {
    bool operator()(const elist_t& a, const elist_t& b) const {
        if (a.size() != b.size()) {
            return a.size() < b.size();
        }
        auto ita = a.begin();
        auto itb = b.begin();
        while (ita != a.end()) {
            if (ita->type != itb->type) {
                return ita->type < itb->type;
            }
            switch (ita->type) {
                case Element_type::CCONST:
                    // Compare value
                    if (std::abs(ita->c->value - itb->c->value) > 1e-15) {
                        return ita->c->value < itb->c->value;
                    }
                    break;
                case Element_type::CVAR:
                    // Compare address
                    if (ita->v->address != itb->v->address) {
                        return ita->v->address < itb->v->address;
                    }
                    break;
                case Element_type::CFUNC:
                    // Compare Name AND Bytecode
                    if (ita->f->name != itb->f->name) {
                        return ita->f->name < itb->f->name;
                    }
                    if (ita->f->code != itb->f->code) {
                        return ita->f->code < itb->f->code;
                    }
                    break;
            }
            ++ita;
            ++itb;
        }
        return false;
    }
};


template <uint8_t OP>
void emit_apply_op_with_constant(evaluator* ev, impl::mexce_charstream& s, double v)
{
    using namespace impl;

    auto constant = make_intermediate_constant(ev, v);

#ifdef MEXCE_64
    s < 0x48 < 0xb8;                        // mov            rax, qword ptr
#else
    s < 0xb8;                               // mov            eax, dword ptr
#endif
    s << constant->address;                 //                   [the address]

    // Constants are always emitted as 64-bit floating point values.
    assert(constant->numeric_data_type == M64FP);
    s < 0xdc < OP;                          // f[OP]  qword ptr [eax/rax]
}


inline
void emit_integer_power_sequence(impl::mexce_charstream& s, uint32_t exponent)
{
    using namespace impl;

    assert(exponent >= 1);

    if (exponent == 1) {
        return;
    }

    if (exponent == 2) {
        s < 0xdc < 0xc8;        // fmul st(0), st(0)
        return;
    }

    uint32_t remaining = exponent;

    s < 0xd9 < 0xe8         // fld1
      < 0xd9 < 0xc9;        // fxch        (base in st0, result in st1)

    while (true) {
        if (remaining & 1U) {
            s < 0xdc < 0xc9;    // fmul st(1), st
        }

        remaining >>= 1U;
        if (!remaining) {
            break;
        }

        s < 0xdc < 0xc8;        // fmul st(0), st(0)
    }

    s < 0xd9 < 0xc9         // fxch        (result to st0)
      < 0xdd < 0xd9;        // fstp st(1)
}



inline
void emit_load_constant(evaluator* ev, impl::mexce_charstream& s, double v)
{
    using namespace impl;

    double av = abs(v);
    bool hit = false;

    if (av == 0.0)                         { s < 0xd9 < 0xee; hit = true; } else  // fldz
    if (av == 1.0)                         { s < 0xd9 < 0xe8; hit = true; } else  // fld1
    if (av == 3.1415926535897932384626433) { s < 0xd9 < 0xeb; hit = true; } else  // fldpi
    if (av == 3.32192809488736234787)      { s < 0xd9 < 0xe9; hit = true; } else  // fldl2t
    if (av == 1.44269504088896340736)      { s < 0xd9 < 0xea; hit = true; } else  // fldl2e
    if (av == 0.3010299956639811952137)    { s < 0xd9 < 0xec; hit = true; } else  // fldlg2
    if (av == 0.6931471805599453094172)    { s < 0xd9 < 0xed; hit = true; }       // fldln2

    if (hit) {
        if (v < 0) {
            s < 0xd9 < 0xe0;  // fchs
        }
        return;
    }

    auto sc = make_intermediate_constant(ev, v);

#ifdef MEXCE_64
    s < 0x48 < 0xb8;                            // mov            rax, qword ptr
    s << (void*)(sc->address);
    s < 0xdd < 0x00;                            // fld            [rax]
#else
    s < 0xdd < 0x05;                            // fld            [immediate address]
    s << (void*)(sc->address);
#endif
}



inline
void compile_elist(impl::mexce_charstream& code_buffer, const impl::elist_const_it_t first, const impl::elist_const_it_t last)
{
    using namespace impl;

    int current_depth = 0;

    for (auto it = first; it != last; ++it) {
        switch (it->type) {
            case Element_type::CVAR: {
                auto tn = it->v;
#ifdef MEXCE_64
                code_buffer << (uint16_t)0xb848;   // move input address to rax (opcode)
                code_buffer << (void*)tn->address;
#endif
                switch (tn->numeric_data_type) {
#ifdef MEXCE_64
                    case M32FP:   code_buffer < 0xd9 < 0x00; break;
                    case M64FP:   code_buffer < 0xdd < 0x00; break;
                    case M16INT:  code_buffer < 0xdf < 0x00; break;
                    case M32INT:  code_buffer < 0xdb < 0x00; break;
                    case M64INT:  code_buffer < 0xdf < 0x28; break;
#else
                    case M32FP:   code_buffer < 0xd9 < 0x05; break;
                    case M64FP:   code_buffer < 0xdd < 0x05; break;
                    case M16INT:  code_buffer < 0xdf < 0x05; break;
                    case M32INT:  code_buffer < 0xdb < 0x05; break;
                    case M64INT:  code_buffer < 0xdf < 0x2d; break;
#endif
                }
#ifndef MEXCE_64
                code_buffer << (void*)(tn->address);
#endif
                ++current_depth;
                break;
            }
            case Element_type::CCONST: {
                auto tn = it->c;
#ifdef MEXCE_64
                code_buffer << (uint16_t)0xb848;
                code_buffer << (void*)tn->address;
                code_buffer < 0xdd < 0x00;
#else
                code_buffer < 0xdd < 0x05;
                code_buffer << (void*)(tn->address);
#endif
                ++current_depth;
                break;
            }
            case Element_type::CFUNC: {
                auto tf = it->f;
                code_buffer.write(tf->code.data(), tf->code.size());
                current_depth = current_depth - static_cast<int>(tf->num_args) + 1;
                break;
            }
        }

        if (current_depth > 8) {
            throw std::overflow_error("Expression too complex for x87 FPU (stack overflow)");
        }
    }
}




inline string infix_operator_to_function_name(const string& op)
{
    static map<string, string> op_map = {
        { "+", "add" },
        { "-", "sub" },
        { "*", "mul" },
        { "/", "div" },
        { "^", "pow" },
        { "<", "lt"  },
        { ">", "gt"  },
    };
    auto it = op_map.find(op);
    assert(it != op_map.end());
    return it->second;
}


inline string function_name_to_infix_operator(const string& fn)
{
    static map<string, string> op_map = {
        { "add", "+" },
        { "sub", "-" },
        { "mul", "*" },
        { "div", "/" },
        { "pow", "^" },
        { "lt" , "<" },
        { "gt" , ">" }
    };
    auto it = op_map.find(fn);
    if (it == op_map.end()) {
        return "";
    }
    return it->second;
}


inline string function_name_to_unary_operator(const string& fn)
{
    if (fn == "neg")
        return "-";
    return "";
}


inline
string double_to_pretty_string(double v)
{
    stringstream ss;
    ss << v;
    return ss.str();
}


inline
string elist_to_string(const elist_t& elist)
{

    deque<std::tuple<string, size_t, vector<string>>> st;

    // root element appears as an unnamed function of 1 argument
    st.push_back(make_tuple(string(), 2, vector<string>{""}));

    for (auto it = elist.rbegin(); it != elist.rend(); it++) {
        switch(it->type) {
            case Element_type::CFUNC: {
                auto f = it->f;
                st.push_back(make_tuple(string(), f->args.size() + 1, vector<string>{f->name}));
                break;
            }
            case Element_type::CCONST: {
                auto c = it->c;
                get<2>(st.back()).push_back(double_to_pretty_string(c->value));
                break;
            }
            case Element_type::CVAR: {
                auto v = it->v;
                get<2>(st.back()).push_back(v->name);
                break;
            }
        }

        while ( get<2>(st.back()).size() ==  get<1>(st.back()) ) {
            string& lrs = get<0>(st.back());
            vector<string>& lrv = get<2>(st.back());

            string symbolic_op;
            if ( (get<1>(st.back()) == 2) && !(symbolic_op = function_name_to_unary_operator(lrv[0])).empty() ) {
                lrs = string("(") + symbolic_op + lrv.back() + ")";
            }
            else
            if ( (get<1>(st.back()) == 3) && !(symbolic_op = function_name_to_infix_operator(lrv[0])).empty() ) {
                lrs = string("(") + lrv.back() + symbolic_op;
                lrv.pop_back();
                lrs += lrv.back() + ")";
            }
            else {
                lrs += lrv[0] + "(";
                if (lrv.size() != 1) {
                    while (true) {
                        lrs += lrv.back();
                        lrv.pop_back();
                        if (lrv.size() == 1) {
                            break;
                        }
                        lrs += ", ";
                    }
                }
                lrs += ")";
            }

            if (st.size()!=1) {
                string tmp = lrs;
                st.pop_back();
                get<2>(st.back()).push_back(tmp);
            }
        }
    }

    assert(st.size() == 1);
    string ret = get<0>(st.back());
    return ret.substr(1, ret.size()-2);
}


inline
void asmd_optimizer(elist_it_t it, evaluator* ev, elist_t* elist)
{
    auto f = it->f;
    auto fname = f->name;
    int fclass = (fname == "add" || fname == "sub") ? 1 : (fname == "mul" || fname == "div") ? 2 : 0;
    assert(fclass);

    double neutral = fclass == 1 ? 0.0 : 1.0;
    bool arg2_inv = (fname == "sub" || fname == "div");

    // Phase 1: Absorption (Flatten nested same-type operators)
    if (f->parent != elist->end() && f->parent->type == Element_type::CFUNC) {
        shared_ptr<Function> pf = f->parent->f;
        auto pname = pf->name;
        int pclass = (pname == "add" || pname == "sub") ? 1 : (pname == "mul" || pname == "div") ? 2 : 0;
        bool parg2_inv = (pname == "sub" || pname == "div");

        if (pclass && pclass == fclass) {
            bool parent_inv_op = f->parent_arg_index == 0 && parg2_inv;
            auto arg1_chunk = get_dependent_chunk(f->args[1]);
            pf->absorbed[parent_inv_op].push_back(elist_t(arg1_chunk.first, arg1_chunk.second));
            elist->erase(arg1_chunk.first, arg1_chunk.second);
            auto arg0_chunk = get_dependent_chunk(f->args[0]);
            pf->absorbed[parent_inv_op ^ arg2_inv].push_back(elist_t(arg0_chunk.first, arg0_chunk.second));
            elist->erase(arg0_chunk.first, arg0_chunk.second);
            pf->absorbed[parent_inv_op].insert(pf->absorbed[parent_inv_op].end(), f->absorbed[0].begin(), f->absorbed[0].end());
            pf->absorbed[!parent_inv_op].insert(pf->absorbed[!parent_inv_op].end(), f->absorbed[1].begin(), f->absorbed[1].end());
            pf->force_not_constant = true;
            *it = Element(make_intermediate_constant(ev, neutral));
            return;
        }
    }

    // Phase 2: Collection & Constant Reduction
    auto arg1_chunk = get_dependent_chunk(f->args[1]);
    f->absorbed[0].push_back(elist_t(arg1_chunk.first, arg1_chunk.second));
    elist->erase(arg1_chunk.first, arg1_chunk.second);
    auto arg0_chunk = get_dependent_chunk(f->args[0]);
    f->absorbed[arg2_inv].push_back(elist_t(arg0_chunk.first, arg0_chunk.second));
    elist->erase(arg0_chunk.first, arg0_chunk.second);
    f->args.clear();

    long double ac[2] = {neutral, neutral};
    for (int i = 0; i < 2; i++) {
        for (auto e = f->absorbed[i].begin(); e != f->absorbed[i].end();) {
            auto next_e = next(e);
            if (e->size() == 1 && e->front().type == Element_type::CCONST) {
                auto v = e->front().c;
                if (fclass == 1) ac[i] += static_cast<long double>(v->value);
                else             ac[i] *= static_cast<long double>(v->value);
                f->absorbed[i].erase(e);
            }
            e = next_e;
        }
    }

    // Phase 3: Term Analysis & Linearization
    struct absorbed_term {
        elist_t chunk;
        double  factor;
    };
    vector<absorbed_term> terms;
    terms.reserve(f->absorbed[0].size() + f->absorbed[1].size());

    auto drain_terms = [&](int index, double contribution) {
        for (auto &chunk : f->absorbed[index]) {
            bool expanded = false;
            // Check if this chunk is a known Linear Term ("C * Kernel")
            if (chunk.size() == 1 && chunk.front().type == Element_type::CFUNC) {
                uint64_t func_id = chunk.front().f->id;
                auto it_linear = ev->m_linear_terms.find(func_id);

                if (it_linear != ev->m_linear_terms.end()) {
                    double coeff = it_linear->second.first;

                    if (fclass == 1) {
                        // ADD/SUB: "2*x" becomes "x" with factor 2.0
                        double combined_factor = contribution * coeff;
                        terms.push_back({ std::move(it_linear->second.second), combined_factor });
                        expanded = true;
                    }
                    else {
                        // MUL/DIV: "2*x" becomes "x" (factor 1), and we absorb "2" into accumulator
                        ac[index] *= static_cast<long double>(coeff);

                        terms.push_back({ std::move(it_linear->second.second), contribution });
                        expanded = true;
                    }
                }
            }
            if (!expanded) {
                terms.push_back({std::move(chunk), contribution});
            }
        }
        f->absorbed[index].clear();
    };

    drain_terms(0, 1.0);
    drain_terms(1, -1.0);

    long double ac_final_ld = (fclass == 1) ? (ac[0] - ac[1]) : (ac[0] / ac[1]);
    double ac_final = static_cast<double>(ac_final_ld);

    elist_comparison comp;
    std::sort(terms.begin(), terms.end(), [&](const absorbed_term& lhs, const absorbed_term& rhs) {
        return comp(lhs.chunk, rhs.chunk);
    });

    vector<absorbed_term> merged;
    merged.reserve(terms.size());
    for (auto &term : terms) {
        if (!merged.empty() && !comp(term.chunk, merged.back().chunk) && !comp(merged.back().chunk, term.chunk)) {
            merged.back().factor += term.factor;
            if (abs(merged.back().factor) < 1e-12) merged.pop_back();
        }
        else if (abs(term.factor) > 1e-12) {
            merged.push_back({std::move(term.chunk), term.factor});
        }
    }

    // Phase 4: Code Generation
    mexce_charstream s;
    if (fclass == 1) { // ADD/SUB
        bool have_value = false;
        bool constant_added = (ac_final == neutral);
        auto ensure_constant = [&]() {
            if (have_value && !constant_added && ac_final != neutral) {
                emit_apply_op_with_constant<0x00>(ev, s, ac_final); // FADD
                constant_added = true;
            }
        };
        for (auto &term : merged) {
            double factor = term.factor;
            if (have_value) ensure_constant();
            compile_elist(s, term.chunk.begin(), term.chunk.end());

            if (factor == 1.0)       { /* no-op */ }
            else if (factor == -1.0) { s < 0xd9 < 0xe0; } // fchs
            else if (factor == 2.0)  { s < 0xd8 < 0xc0; } // fadd st, st
            else if (factor == -2.0) { s < 0xd8 < 0xc0 < 0xd9 < 0xe0; }
            else {
                emit_apply_op_with_constant<0x08>(ev, s, factor); // FMUL (Scale Kernel)
            }

            if (!have_value) {
                have_value = true;
                if (ac_final != neutral) {
                    emit_apply_op_with_constant<0x00>(ev, s, ac_final);
                    constant_added = true;
                }
            } else {
                s < 0xde < 0xc1; // faddp
            }
        }
        if (!have_value) emit_load_constant(ev, s, ac_final);
        else ensure_constant();
    }
    else { // MUL/DIV
        bool have_value = false;
        bool constant_multiplied = (ac_final == neutral);
        auto ensure_constant = [&]() {
            if (have_value && !constant_multiplied && ac_final != neutral) {
                emit_apply_op_with_constant<0x08>(ev, s, ac_final); // FMUL
                constant_multiplied = true;
            }
        };
        for (auto &term : merged) {
            double factor = term.factor;
            if (have_value) ensure_constant();
            compile_elist(s, term.chunk.begin(), term.chunk.end());

            double r_factor = round(factor);
            bool is_int = (abs(factor - r_factor) < 1e-9);
            uint32_t abs_int_factor = static_cast<uint32_t>(abs(r_factor));

            if (is_int && abs_int_factor > 1) {
                emit_integer_power_sequence(s, abs_int_factor);
            }
            // Non-integer powers in mul chain not generated by current parser

            if (factor < 0) {
                if (!have_value) { s < 0xd9 < 0xe8 < 0xde < 0xf1; } // 1/x
                else             { s < 0xde < 0xf9; } // fdivp
            } else if (have_value) {
                s < 0xde < 0xc9; // fmulp
            }

            if (!have_value) {
                have_value = true;
                if (ac_final != neutral) {
                    emit_apply_op_with_constant<0x08>(ev, s, ac_final);
                    constant_multiplied = true;
                }
            }
        }
        if (!have_value) emit_load_constant(ev, s, ac_final);
        else ensure_constant();
    }

    string new_name = (fclass == 1) ? "add_sub_opt" : "mul_div_opt";
    uint8_t* cc = push_intermediate_code(ev, s.str());
    auto f_opt = make_shared<Function>(ev->m_next_element_id++, new_name, 0, 0, s.buf.size(), cc, nullptr);

    // Phase 5: Register Linear Term (Optimization Side-Channel)
    if (fclass == 2) {
        if (merged.size() == 1 && abs(merged[0].factor - 1.0) < 1e-9) {
            ev->m_linear_terms[f_opt->id] = std::make_pair(ac_final, merged[0].chunk);
        }
    }

    *it = Element(f_opt);
}


inline Function Add()
{
    uint8_t code[] = {
        0xde, 0xc1                                  // faddp       st(1), st
    };
    return Function(0, "add", 2, 0, sizeof(code), code, asmd_optimizer);
}


inline Function Sub()
{
    uint8_t code[] = {
        0xde, 0xe9                                  // fsubp       st(1), st
    };
    return Function(0, "sub", 2, 0, sizeof(code), code, asmd_optimizer);
}


inline Function Mul()
{
    uint8_t code[] = {
        0xde, 0xc9                                  // fmulp       st(1), st
    };
    return Function(0, "mul", 2, 0, sizeof(code), code, asmd_optimizer);
}


inline Function Div()
{
    uint8_t code[] = {
        0xde, 0xf9                                  // fdivp       st(1), st
    };
    return Function(0, "div", 2, 0, sizeof(code), code, asmd_optimizer);
}


inline Function Neg()
{
    // TODO: this does not need its own internal name, it is a special case of add_sub
    uint8_t code[] = {
        0xd9, 0xe0                                  // fchs
    };
    return Function(0, "neg", 1, 0, sizeof(code), code);
}


inline Function Gain()
{
    //                            x
    //                 ------------------------  if x < 0.5
    //                 (1 / a - 2) (1 - 2x) + 1
    // gain(x, a) =                                               for x, a in [0, 1]
    //                 (1 / a - 2) (1 - 2x) - x
    //                 ------------------------  if x >= 0.5
    //                 (1 / a - 2) (1 - 2x) - 1

    uint8_t code[] = {                       //                       ; FPU stack
        0xd9, 0xc1,                                 // fld         st(1)     ; x, a, x
        0xd8, 0xc2,                                 // fadd        st,st(2)  ; 2x, a, x
        0xd9, 0xe8,                                 // fld1                  ; 1, 2x, a, x
        0xdf, 0xf1,                                 // fcomip      st,st(1)  ; 2x, a, x
        0xdd, 0xd8,                                 // fstp        st(0)     ; a, x
        0xd9, 0xc0,                                 // fld         st(0)     ; a, a, x
        0xd8, 0xc1,                                 // fadd        st,st(1)  ; 2a, a, x
        0xd9, 0xe8,                                 // fld1                  ; 1, 2a, a, x
        0xde, 0xe9,                                 // fsubp       st(1),st  ; 2a-1, a, x
        0xde, 0xf1,                                 // fdivrp      st(1),st  ; (2a-1)/a, x
        0xd9, 0xc1,                                 // fld         st(1)     ; x, (2a-1)/a, x
        0xdc, 0xc0,                                 // fadd        st(0),st  ; 2x, (2a-1)/a, x
        0xd9, 0xe8,                                 // fld1                  ; 1, 2x, (2a-1)/a, x
        0xde, 0xe9,                                 // fsubp       st(1),st  ; 2x-1, (2a-1)/a, x
        0xde, 0xc9,                                 // fmulp       st(1),st  ; (2x-1)*(2a-1)/a, x
        0xd9, 0xe8,                                 // fld1                  ; 1, (2x-1)*(2a-1)/a, x
        0x72, 0x06,                                 // jb          x_ge_half
        0xde, 0xc1,                                 // faddp       st(1),st  ; (2x-1)*(2a-1)/a+1, x
        0xde, 0xf9,                                 // fdivp       st(1),st  ; x/((2x-1)*(2a-1)/a+1) [result]
        0xeb, 0x0a,                                 // jmp         gain_exit
// x_ge_half:
        0xd9, 0xc1,                                 // fld         st(1)     ; (2x-1)*(2a-1)/a, 1, (2x-1)*(2a-1)/a, x
        0xde, 0xe9,                                 // fsubp       st(1),st  ; 1-(2x-1)*(2a-1)/a, (2x-1)*(2a-1)/a, x
        0xd9, 0xc9,                                 // fxch        st(1)     ; (2x-1)*(2a-1)/a, 1-(2x-1)*(2a-1)/a, x
        0xde, 0xea,                                 // fsubp       st(2),st  ; 1-(2x-1)*(2a-1)/a, x-(2x-1)*(2a-1)/a
        0xde, 0xf9,                                 // fdivp       st(1),st  ; (x-(2x-1)*(2a-1)/a)/(1-(2x-1)*(2a-1)/a)  [result]
// gain_exit:
    };
    return Function(0, "gain", 2, 1, sizeof(code), code);
}


inline Function Bias()
{
    //                         x
    // bias(x, a) = -----------------------    for x, a in [0, 1]
    //              (1 / a - 2) (1 - x) + 1

    uint8_t code[] = {
        0xd9, 0xe8,                                 // fld1
        0xdc, 0xf1,                                 // fdivr       st(1), st
        0xdc, 0xe9,                                 // fsub        st(1), st
        0xdc, 0xe9,                                 // fsub        st(1), st
        0xd8, 0xe2,                                 // fsub        st, st(2)
        0xde, 0xc9,                                 // fmulp       st(1), st
        0xd9, 0xe8,                                 // fld1
        0xde, 0xc1,                                 // faddp       st(1), st
        0xde, 0xf9                                  // fdivp       st(1), st
    };
    return Function(0, "bias", 2, 1, sizeof(code), code);
}



inline const map<string, Function>& make_function_map()
{
    static map<string, Function> ret;
    if (ret.empty()) { // Initialize only once
        Function f[] = {
            Sin(), Cos(), Tan(), Abs(), Sign(), Signp(), Expn(), Sfc(), Sqrt(), Pow(), Exp(), Lt(), Gt(), Le(), Ge(), Eq(), Ne(),
            Log(), Log2(), Ln(), Log10(), Logb(), Ylog2(), Max(), Min(), Floor(), Ceil(), Round(), Int(), Mod(),
            Bnd(), Add(), Sub(), Neg(), Mul(), Div(), Bias(), Gain()
        };
        for (auto& e : f) {
            assert(ret.find(e.name) == ret.end()); //if it fails, some functions share the same name.
            ret.insert(make_pair(e.name, e));
        }
    }
    return ret;
}


inline const map<string, Function>& function_map()
{
    static const map<string, Function>& fname_map = make_function_map();
    return fname_map;
}


inline shared_ptr<Function> make_function(evaluator* ev, const string& name) {
    auto fn_it = function_map().find(name);
    assert(fn_it != function_map().end());
    // Create a copy of the prototype and assign a new, unique ID.
    auto new_func = make_shared<Function>(fn_it->second);
    new_func->id = ev->m_next_element_id++;
    return new_func;
}


inline const map<string, shared_ptr<Constant> >& built_in_constants_map()
{
    static map<string, shared_ptr<Constant> > cname_map;
    if (cname_map.empty()) {
        cname_map["pi"] = std::make_shared<Constant>(0, "3.141592653589793238462643383", "pi");
        cname_map["e"]  = std::make_shared<Constant>(1, "2.718281828459045235360287471", "e");
    }
    return cname_map;
}


struct Token
{
    int             type        = 0;
    int             priority    = 0;
    size_t          position    = 0;
    string          content;

    Token() = default;
    ~Token() = default;
    Token(const Token& other) = default;
    Token(Token&& other) noexcept = default;
    Token& operator=(const Token& other) = default;
    Token& operator=(Token&& other) noexcept = default;

    Token(int type, size_t position, char content):
        type      ( type               ),
        priority  ( type               ),
        position  ( position           ),
        content   ( string() + content ) {}
};


template <typename> inline Numeric_data_type get_ndt()  {
    // LCOV_EXCL_START
    assert(false);
    return Numeric_data_type();
    // LCOV_EXCL_STOP
}
template <> inline Numeric_data_type get_ndt<double >() { return M64FP;  }
template <> inline Numeric_data_type get_ndt<float  >() { return M32FP;  }
template <> inline Numeric_data_type get_ndt<int16_t>() { return M16INT; }
template <> inline Numeric_data_type get_ndt<int32_t>() { return M32INT; }
template <> inline Numeric_data_type get_ndt<int64_t>() { return M64INT; }


inline bool is_operator(  char c) { return  c=='+' || c=='-'  ||  c=='*' || c=='/'  || c=='^' || c=='<'; }
inline bool is_alphabetic(char c) { return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'; }
inline bool is_numeric(   char c) { return  c>='0' && c<='9'; }


} // mexce_impl


inline
evaluator::evaluator():
    m_constants(impl::built_in_constants_map())
{
    m_next_element_id = 2; // Start after built-in constants.
    set_expression("0");
}


inline
evaluator::~evaluator()
{
    impl::free_executable_buffer(evaluate_fptr, m_buffer_size);
}


template <typename T, typename ...Args>
void evaluator::bind(T& v, const std::string& s, Args&... args)
{
    using namespace impl;
    if (function_map().find(s) != function_map().end()) {
        throw std::logic_error("Attempted to bind a variable, named as an existing function");
    }
    if (built_in_constants_map().find(s) != built_in_constants_map().end()) {
        throw std::logic_error("Attempted to bind a variable, named as an existing constant");
    }
    m_variables[s] = make_shared<Variable>(m_next_element_id++, &v, s, get_ndt<T>());

    bind(args...);
}


template <typename ...Args>
void evaluator::unbind(const std::string& s, Args&... args)
{
    if (s.length() == 0)
        throw std::logic_error("Variable name was an empty string");

    auto it = m_variables.find(s);
    if (it != m_variables.end()) {
        if (it->second->referenced) {
            set_expression("0");
        }
        m_variables.erase(it);
        unbind(args...);
        return;
    }
    throw std::logic_error("Attempted to unbind an unknown variable");
}


inline
void evaluator::unbind_all()
{
    m_variables.clear();
}


inline
double evaluator::evaluate() {
    if (is_constant_expression) {
        return constant_expression_value;
    }
    return evaluate_fptr();
}



inline
void evaluator::set_expression(std::string e)
{
    using namespace impl;
    using mpe = mexce_parsing_exception;

    deque<Token> tokens;

    m_intermediate_constants.clear();
    m_intermediate_code.clear();
    m_elist.clear();
    m_linear_terms.clear();
    m_power_terms.clear();

    if (evaluate_fptr) {
        free_executable_buffer(evaluate_fptr, m_buffer_size);
        evaluate_fptr = nullptr;
    }

    auto x = m_variables.begin();
    for (; x != m_variables.end(); x++)
        x->second->referenced = false;

    if (e.length() == 0){
        throw (std::logic_error("Expected an expression"));
    }

    e += ' ';

    //stage 1: checking expression syntax
    Token temp;
    vector< pair<int, int> > bdarray(1);
    map<string, Function>::const_iterator i_fnc;
    int state = 0;
    size_t i = 0;
    int function_parentheses = 0;
    auto emit_closing_parenthesis = [&](size_t position) {
        if (bdarray.back().first > 0) {
            tokens.push_back(Token(RIGHT_PARENTHESIS, position, ')'));
            bdarray.back().first--;
            return;
        }

        if (function_parentheses <= 0) {
            throw (mpe("\")\" not expected", position));
        }
        if (bdarray.back().second != 1) {
            throw (mpe("Expected more arguments", position));
        }

        tokens.push_back(Token(FUNCTION_RIGHT_PARENTHESIS, position, ')'));
        function_parentheses--;
        bdarray.pop_back();
    };

    auto emit_argument_separator = [&](size_t position) {
        if (bdarray.back().first != 0) {
            throw (mpe(R"MSG(Expected a ")")MSG", position));
        }
        if (bdarray.back().second-- < 2) {
            throw (mpe("Don\'t expect any arguments here", position));
        }

        tokens.push_back(Token(COMMA, position, ','));
        state = 0;
    };

    for (; i < e.length(); i++) {
        switch(state) {
            case 0: //start of expression
                if (e[i] == '-' || e[i] == '+') {
                    tokens.push_back(Token(UNARY, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    // Leading ')' at start of an expression.
                    if (function_parentheses <= 0) {
                        throw (mpe("Expected a \")\"", i));
                    }
                    // Inside a function-arg list but saw ')' immediately: missing argument.
                    throw (mpe("Expected an expression", i));
                }
                /* FALLTHROUGH */
            case 4: //just read an infix operator
                if (e[i] == ' ')
                    break;
                if (is_numeric(e[i])) {
                    temp = Token(NUMERIC_LITERAL, i, e[i]);
                    state = 1;
                    break;
                }
                if (e[i] == '.') {
                    temp = Token(NUMERIC_LITERAL, i, e[i]);
                    state = 2;
                    break;
                }
                if (is_alphabetic(e[i])) {
                    temp = Token(0, i, e[i]);
                    state = 3;
                    break;
                }
                if (e[i] == '-' || e[i] == '+') {
                    tokens.push_back(Token(UNARY, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == '(') {
                    tokens.push_back(Token(LEFT_PARENTHESIS, i, '('));
                    bdarray.back().first++;
                    state = 0;
                    break;
                }
                else {
                    throw (mpe((string("\"")+e[i])+"\" not expected", i));
                }
            case 1: //currently reading a numeric literal
                if (e[i] == '.') {
                    temp.content += e[i];
                    state = 2;
                    break;
                }
                /* FALLTHROUGH */
            case 2: // currently reading a numeric literal, found dot
                if (is_numeric(e[i])) {
                    temp.content += e[i];
                    break;
                }
                if (e[i] == ' ') {
                    tokens.push_back(temp);
                    state = 5;
                    break;
                }
                if (e[i] == ')') {
                    tokens.push_back(temp);
                    emit_closing_parenthesis(i);
                    state = 5;
                    break;
                }
                if (is_operator(e[i])) {
                    tokens.push_back(temp);
                    tokens.push_back(Token( get_infix_rank(e[i]) , i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ',') {
                    tokens.push_back(temp);
                    emit_argument_separator(i);
                    break;
                }
                if (e[i] == 'e' && state < 7) {
                    temp.content += e[i];
                    state = 7;
                    break;
                }
                throw (mpe((string("\"")+e[i])+"\" not expected", i));
            case 7: // read the 'e' (exponent) while reading a numeric literal
                if (e[i] == '+' || e[i] == '-') {
                    temp.content += e[i];
                    state = 8;
                    break;
                }
                throw (mpe("expecting '+'/'-' followed by the exponent of the numeric literal", i));
            case 8: // reading the exponent of the numeric literal
                if (is_numeric(e[i])) {
                    temp.content += e[i];
                    state = 2;
                    break;
                }
                throw (mpe("expecting the exponent of the numeric literal", i));
            case 3: //currently reading alphanumeric
                if (is_alphabetic(e[i]) || is_numeric(e[i])) {
                    temp.content += e[i];
                    break;
                }
                if (e[i] == ' ') {
                    if (m_variables.find(temp.content) != m_variables.end()) {
                        temp.type = VARIABLE_NAME;
                        tokens.push_back(temp);
                        state = 5;
                        break;
                    }
                    if (m_constants.find(temp.content) != m_constants.end()) {
                        temp.type = CONSTANT_NAME;
                        tokens.push_back(temp);
                        state = 5;
                        break;
                    }
                    if ((i_fnc = function_map().find(temp.content)) != function_map().end()) {
                        temp.type = FUNCTION_NAME;
                        tokens.push_back(temp);
                        tokens.push_back(Token(FUNCTION_LEFT_PARENTHESIS, i, '('));
                        bdarray.push_back(make_pair(0, (int)i_fnc->second.num_args));
                        function_parentheses++;
                        state = 6;
                        break;
                    }
                    throw (mpe(string(temp.content) +
                        " is not a known constant, variable or function name", i));
                }
                if (e[i] == ')') {
                    temp.type = m_variables.find(temp.content) != m_variables.end() ? VARIABLE_NAME :
                                m_constants.find(temp.content) != m_constants.end() ? CONSTANT_NAME :
                        throw (mpe(string(temp.content) +
                            " is not a known constant or variable name", i));
                    tokens.push_back(temp);
                    emit_closing_parenthesis(i);
                    state = 5;
                    break;
                }
                if (e[i] == '(') {
                    if ((i_fnc = function_map().find(temp.content)) == function_map().end()) {
                        throw (mpe(string(temp.content) + " is not a known function name", i));
                    }
                    temp.type = FUNCTION_NAME;
                    tokens.push_back(temp);
                    tokens.push_back(Token(FUNCTION_LEFT_PARENTHESIS, i, '('));
                    bdarray.push_back(make_pair(0, (int)i_fnc->second.num_args));
                    function_parentheses++;
                    state = 0;
                    break;
                }
                if (is_operator(e[i])) {
                    temp.type = m_variables.find(temp.content) != m_variables.end() ? VARIABLE_NAME :
                                m_constants.find(temp.content) != m_constants.end() ? CONSTANT_NAME :
                        throw (mpe(string(temp.content) +
                            " is not a known constant or variable name", i));
                    tokens.push_back(temp);
                    tokens.push_back(Token(get_infix_rank(e[i]), i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ',') {
                    temp.type = m_variables.find(temp.content) != m_variables.end() ? VARIABLE_NAME :
                                m_constants.find(temp.content) != m_constants.end() ? CONSTANT_NAME :
                        throw (mpe(string(temp.content)+" is not a "
                            "known constant or variable name", i));
                    tokens.push_back(temp);
                    emit_argument_separator(i);
                    break;
                }
                throw (mpe((string("\"")+e[i])+"\" not expected", i));
            case 5: //just read an expression (constant/variable/right parenthesis)
                if (e[i] == ' ')
                    break;
                if (is_operator(e[i])) {
                    tokens.push_back(Token(get_infix_rank(e[i]), i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    emit_closing_parenthesis(i);
                    state = 5;
                    break;
                }
                if (e[i] == ',') {
                    emit_argument_separator(i);
                    break;
                }
                throw (mpe((string("\"")+e[i])+"\" not expected", i));
            case 6: //just read a function name
                if (e[i] == '(') {
                    state = 0;
                    break;
                }
                throw (mpe("Expected a \"(\"", i));
        }
    }
    if ((bdarray.back().first > 0) || (function_parentheses > 0)) {
        throw (mpe("Expected a \")\"", --i));
    }
    if (state != 5) {
        throw (mpe("Unexpected end of expression", --i));
    }

    //stage 2: transform expression to postfix
    deque<Token> postfix;
    vector<Token> tstack;
    while (!tokens.empty()) {
        temp = tokens.front();
        tokens.pop_front();
        switch (temp.type) {
            case INFIX_4:
            case INFIX_3:
            case INFIX_2:
                while(!tstack.empty()) {
                    int sp = tstack.back().priority;
                    if (sp < INFIX_1 || sp > temp.type) {
                        break;
                    }
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                /* FALLTHROUGH */
            case INFIX_1:
            case LEFT_PARENTHESIS:
            case FUNCTION_NAME:
                tstack.push_back(temp);
                break;
            case UNARY:
                temp.priority = (!tstack.empty() && tstack.back().priority == INFIX_1) ?
                    INFIX_1 : INFIX_3;
                tstack.push_back(temp);
                break;
            case NUMERIC_LITERAL:
            case CONSTANT_NAME:
            case VARIABLE_NAME:
                postfix.push_back(temp);
                break;
            case RIGHT_PARENTHESIS:
                while(tstack.back().type != LEFT_PARENTHESIS) {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                tstack.pop_back();
                break;
            case FUNCTION_RIGHT_PARENTHESIS:
                do {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                while(postfix.back().type != FUNCTION_NAME);
                break;
            case COMMA:
                while(tstack.back().type != FUNCTION_NAME) {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                break;
            case FUNCTION_LEFT_PARENTHESIS:
                break;
            default:
                throw(mpe("internal error", 0));
        }
    }
    while(!tstack.empty()) {
        postfix.push_back(tstack.back());
        tstack.pop_back();
    }

    //stage 3: convert "Token" expression primitives to "Element *"

    while (!postfix.empty()) {
        temp = postfix.front();
        postfix.pop_front();
        switch (temp.type) {
            case INFIX_4:
            case INFIX_3:
            case INFIX_2:
            case INFIX_1: {
                auto name = infix_operator_to_function_name(temp.content);
                m_elist.push_back(Element(make_function(this, name)));
                break;
            }
            case FUNCTION_NAME:
                m_elist.push_back(Element(make_function(this, temp.content)));
                break;
            case UNARY:
                if (temp.content == "-") { // unary '+' is ignored

                    // Rather than having an individual function for the unary minus
                    // we insert a zero before the last argument (which is already in
                    // the list), and use a subtraction instead.
                    // The reason is to allow the optimizer to group
                    // addition/subtraction chains. Clearly, this is a bit unorthodox
                    // and could be done in the optimizer too, but the optimizer is
                    // complex enough already.

                    link_arguments(m_elist);
                    auto chunk = get_dependent_chunk(std::prev(m_elist.end()));
                    m_elist.insert(chunk.first, Element(make_intermediate_constant(this, 0.0)));
                    m_elist.push_back(Element(make_function(this, "sub")));
                }
                break;
            case NUMERIC_LITERAL: {
                double c_value = atof(temp.content.c_str());
                m_elist.push_back(Element(make_intermediate_constant(this, c_value)));
                break;
            }
            case CONSTANT_NAME: {
                m_elist.push_back(Element(m_constants.find(temp.content)->second));
                break;
            }
            case VARIABLE_NAME: {
                auto it = m_variables.find(temp.content);
                assert(it != m_variables.end());
                it->second->referenced = true;
                m_elist.push_back(Element(it->second));
                break;
            }
        }
    }

    // link functions to their arguments (1)
    link_arguments(m_elist);

    // choose more suitable functions, where applicable
    for (auto y = m_elist.begin(); y != m_elist.end(); ) {
        auto y_next = next(y);
        if (y->type == Element_type::CFUNC) {
            auto f = y->f;

            // eliminate constants
            if (!f->force_not_constant) {
                bool all_args_are_const = true;
                for (size_t j = 0; j < f->num_args; j++) {
                    if (f->args[j]->type != Element_type::CCONST) {
                        all_args_are_const = false;
                        break;
                    }
                }
                if (all_args_are_const) {
                    elist_it_t first_arg_it = y;
                    std::advance(first_arg_it, -(int64_t)f->num_args);
                    compile_and_finalize_elist(first_arg_it, next(y));
                    double res = evaluate_fptr();
                    m_elist.erase(first_arg_it, y);
                    *y = Element(make_intermediate_constant(this, res));
                    y = y_next;
                    continue;
                }
            }

            if (f->optimizer != 0) {
                f->optimizer(y, this, &m_elist);
            }
        }
        y = y_next;
    }

    is_constant_expression = m_elist.size()==1 && m_elist.back().type == Element_type::CCONST;
    if (is_constant_expression) {
        constant_expression_value = m_elist.back().c->value;
    }
    else {
        compile_and_finalize_elist(m_elist.begin(), m_elist.end());
    }
}



inline
void evaluator::compile_and_finalize_elist(impl::elist_const_it_t first, impl::elist_const_it_t last)
{
    using namespace impl;

    const static uint8_t return_sequence[] = {
#ifdef MEXCE_64
        // Right before the function returns, in 32-bit x86, the result is in
        // st(0), where it is expected to be. There is nothing further to do there
        // other than return.
        // In x64 however, the result is expected to be in xmm0, thus we should
        // move it there and pop the FPU stack. We use the stack as a temporary
        // to avoid depending on any member variable address (move-safe).

        0x48, 0x83, 0xec, 0x08,                                     // sub  rsp, 8
        0xdd, 0x1c, 0x24,                                           // fstp qword ptr [rsp]
        0xf2, 0x0f, 0x10, 0x04, 0x24,                               // movsd xmm0, qword ptr [rsp]
        0x48, 0x83, 0xc4, 0x08,                                     // add  rsp, 8
        0x58,                                                       // pop  rax
#endif
        0xc3                                                        // ret
    };

    mexce_charstream code_buffer;

#ifdef MEXCE_64
    // On x64 we are using rax to fetch/store addresses
    code_buffer < 0x50; // push rax
    // Reserve 32 bytes of stack space for ABI compliance (Win64) and scratch.
    code_buffer < 0x48 < 0x83 < 0xec < 0x20;   // sub  rsp, 32
#endif

    compile_elist(code_buffer, first, last);

#ifdef MEXCE_64
    // Deallocate stack space before return sequence.
    code_buffer < 0x48 < 0x83 < 0xc4 < 0x20;   // add  rsp, 32
#endif

    // copy the return sequence
    code_buffer.buf.insert(code_buffer.buf.end(), return_sequence, return_sequence + sizeof(return_sequence));

    m_buffer_size = code_buffer.buf.size();
    auto buffer = get_executable_buffer(m_buffer_size);

#ifdef _WIN32
    if (!buffer) {
        throw std::bad_alloc();
    }
#elif defined(__linux__)
    if (buffer == MAP_FAILED) {
        throw std::bad_alloc();
    }
#endif

    memcpy(buffer, code_buffer.buf.data(), m_buffer_size);

    evaluate_fptr = lock_executable_buffer(buffer, m_buffer_size);
}

} // mexce

#endif
