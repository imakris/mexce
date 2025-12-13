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
 *   (power) and the comparison operators `<` and `>` are supported.  The `**`
 *   operator is also available as an alias for `^` (power). Both comparison
 *   operators yield `1` when the comparison is true and `0` otherwise.
 * * Precedence: The power operators (`^` and `**`) have the highest precedence
 *   and bind tighter than unary minus. For example, `-a^2` and `-a**2` are
 *   evaluated as `-(a^2)`, matching Python semantics. Use parentheses to change
 *   the grouping: `(-a)^2`.
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
#include <functional>
#include <iomanip>
#include <list>
#include <map>
#include <memory>
#include <set>
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
    string elist_to_string(const elist_t& elist);
    void run_cse(evaluator* ev, elist_t& elist);
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

    /**
     * @brief Returns the reconstructed string of the optimized internal expression tree.
     * Useful for debugging optimizations.
     */
    std::string get_optimized_expression() const;

    /**
     * @brief Returns a hex byte representation of the compiled machine code.
     * Format: hex pairs separated by spaces (e.g., "50 48 83 EC 20 ...").
     * Useful for debugging the generated code.
     */
    std::string get_byte_representation() const;

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
    std::list<double>       m_cse_temps;               // Storage for common subexpressions
    std::map<uint64_t, std::pair<impl::elist_t, double>> m_power_terms; // pow-optimized kernel/exponent for nested folding

    double                (*evaluate_fptr)()            = nullptr;

    void compile_and_finalize_elist(impl::elist_const_it_t first, impl::elist_const_it_t last);

    // Grant friend access to helpers that need private state.
    friend std::shared_ptr<impl::Constant> impl::make_intermediate_constant(evaluator* ev, double v);
    friend std::shared_ptr<impl::Function> impl::make_function(evaluator* ev, const std::string& name);
    friend void impl::asmd_optimizer(impl::elist_it_t it, evaluator* ev, impl::elist_t* elist);
    friend void impl::pow_optimizer(impl::elist_it_t it, evaluator* ev, impl::elist_t* elist);
    friend uint8_t* impl::push_intermediate_code(evaluator* ev, const std::string& s);
    friend void impl::run_cse(evaluator* ev, impl::elist_t& elist);

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
    string              debug_desc;
    string              cse_store_suffix;  // CSE store code to emit after function code

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
void patch_rel32(mexce_charstream& s, size_t rel32_pos, size_t target_pos)
{
    const int32_t rel = static_cast<int32_t>(target_pos - (rel32_pos + 4));
    std::memcpy(s.buf.data() + rel32_pos, &rel, sizeof(rel));
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


// --- Trigonometry implementation ---
//
// By default mexce avoids x87 FSIN/FCOS/FPTAN (typically microcoded and slow on modern CPUs)
// and evaluates sin/cos via a Maclaurin polynomial with a runtime "stop condition":
// fewer terms are used when |x| is small, while still converging to full double precision.
//
// Define `MEXCE_TRIG_USE_X87` to 1 before including mexce.h to force the legacy x87
// trig instructions instead.
#ifndef MEXCE_TRIG_USE_X87
#   define MEXCE_TRIG_USE_X87 0
#endif

// Optional trig path: range-reduce by multiples of π/2 and evaluate fixed-degree kernels on |r| <= π/4.
// This removes the runtime degree-selection ladder at the cost of extra reduction work.
#ifndef MEXCE_TRIG_USE_PIO2_KERNEL
#   define MEXCE_TRIG_USE_PIO2_KERNEL 0
#endif

// 80-bit Maclaurin coeffs as 16-byte records (mantissa 8 + exp/sign 2 + pad).
// Order: highest degree -> constant, for Horner on y=x^2.
static const uint64_t mexce_trig_mfactors[] = {
    0x9c9962823eb07306, 0x000000000000bf93,     // -1/(30!)
    0x850c5131a842e9ba, 0x0000000000003f9d,     // +1/(28!)
    0xc4742fe35272cd1c, 0x000000000000bfa6,     // -1/(26!)
    0xf96780cb97abbe65, 0x0000000000003faf,     // +1/(24!)
    0x8671cb6dbfc294a3, 0x000000000000bfb9,     // -1/(22!)
    0xf2a15d201011283d, 0x0000000000003fc1,     // +1/(20!)
    0xb413c31dcbecbbde, 0x000000000000bfca,     // -1/(18!)
    0xd73f9f399dc0f88f, 0x0000000000003fd2,     // +1/(16!)
    0xc9cba54603e4e906, 0x000000000000bfda,     // -1/(14!)
    0x8f76c77fc6c4bdaa, 0x0000000000003fe2,     // +1/(12!)
    0x93f27dbbc4fae397, 0x000000000000bfe9,     // -1/(10!)
    0xd00d00d00d00d00d, 0x0000000000003fef,     // +1/( 8!)
    0xb60b60b60b60b60b, 0x000000000000bff5,     // -1/( 6!)
    0xaaaaaaaaaaaaaaab, 0x0000000000003ffa,     // +1/( 4!)
    0x8000000000000000, 0x000000000000bffe,     // -1/( 2!)
    0x8000000000000000, 0x0000000000003fff      // +1
};

// 80-bit Maclaurin coeffs for sin(x) = x * P(y), where y=x^2.
// P(y) = sum_{k=0..14} (-1)^k * y^k / (2k+1)!
// Order: highest degree -> constant, for Horner on y.
static const uint64_t mexce_trig_sinfactors[] = {
    0x92cfcc5a1ac56bd6, 0x0000000000003f98,     // +1/(29!)
    0xe8d58e16e6751905, 0x000000000000bfa1,     // -1/(27!)
    0x9f9e66e8b2fd46a7, 0x0000000000003fab,     // +1/(25!)
    0xbb0da098b1c0cecc, 0x000000000000bfb4,     // -1/(23!)
    0xb8dc77b6e7ab8c5f, 0x0000000000003fbd,     // +1/(21!)
    0x97a4da340a0ab926, 0x000000000000bfc6,     // -1/(19!)
    0xca963b81856a5359, 0x0000000000003fce,     // +1/(17!)
    0xd73f9f399dc0f88f, 0x000000000000bfd6,     // -1/(15!)
    0xb092309d43684be5, 0x0000000000003fde,     // +1/(13!)
    0xd7322b3faa271c7f, 0x000000000000bfe5,     // -1/(11!)
    0xb8ef1d2ab6399c7d, 0x0000000000003fec,     // +1/( 9!)
    0xd00d00d00d00d00d, 0x000000000000bff2,     // -1/( 7!)
    0x8888888888888889, 0x0000000000003ff8,     // +1/( 5!)
    0xaaaaaaaaaaaaaaab, 0x000000000000bffc,     // -1/( 3!)
    0x8000000000000000, 0x0000000000003fff      // +1/( 1!)
};

// Runtime term-cut thresholds for y=x^2 (after reduction to [-pi, pi]).
// Pick the smallest polynomial that guarantees the next omitted term is below ~2^-55.
// This keeps accuracy comparable to a fully evaluated series while skipping work for small |x|.
static const double mexce_trig_y_thresholds[] = {
    0.04867222504602886, // stop after 10!  (degree 5 in y)
    0.3940000065382974,  // stop after 14!  (degree 7 in y)
    1.5238684779698788,  // stop after 18!  (degree 9 in y)
    4.008710180954158,   // stop after 22!  (degree 11 in y)
    8.38286010518693     // stop after 26!  (degree 13 in y)
};

// Runtime term-cut thresholds for sin(x) = x * P(y), y=x^2 (after reduction to [-pi, pi]).
// Pick the smallest polynomial that guarantees the next omitted term is below ~2^-55.
static const double mexce_trig_sin_y_thresholds[] = {
    0.023535766987280552, // stop after  9! (degree  4 in y)
    0.25584679442517155,  // stop after 13! (degree  6 in y)
    1.13664743101261,     // stop after 17! (degree  8 in y)
    3.230010127826897,    // stop after 21! (degree 10 in y)
    7.085900329143115     // stop after 25! (degree 12 in y)
};

// Constants for k-based reduction by π/2 (fdlibm style): r = x - k*(π/2), k = rint(x * 2/π).
// pio2_1 is truncated so that k*pio2_1 is exact for moderate k; pio2_1t is the tail correction.
static const double mexce_trig_pio2_reduce_consts[] = {
    0.6366197723675814,      // 2/pi      (0x3fe45f306dc9c883)
    1.5707963267341256,      // pio2_1    (0x3ff921fb54400000)
    6.077100506506192e-11    // pio2_1t   (0x3dd0b4611a626331)
};

// Fixed-degree kernels for |r| <= pi/4, stored as 80-bit values in 16-byte records.
// sin(r) = r * P(y),  y=r^2, P(y)=1 + S1*y + ... + S6*y^6  (fdlibm k_sin)
static const uint64_t mexce_trig_sin_kernel_factors[] = {
    0xaec9d2d67eabe000, 0x0000000000003fde,     // +S6
    0xd72f34515ce75800, 0x000000000000bfe5,     // -S5
    0xb8ef1abd8ff3e800, 0x0000000000003fec,     // +S4
    0xd00d00ce0b0ea800, 0x000000000000bff2,     // -S3
    0x8888888887c53000, 0x0000000000003ff8,     // +S2
    0xaaaaaaaaaaaa4800, 0x000000000000bffc,     // -S1
    0x8000000000000000, 0x0000000000003fff      // +1
};

// cos(r) = Q(y), y=r^2, Q(y)=1 - 0.5*y + C1*y^2 + ... + C6*y^7  (fdlibm k_cos)
static const uint64_t mexce_trig_cos_kernel_factors[] = {
    0xc7d74df441c6a000, 0x000000000000bfda,     // -C6
    0x8f74f5eda58e2000, 0x0000000000003fe2,     // +C5
    0x93f27c04e2956800, 0x000000000000bfe9,     // -C4
    0xd00d00ce58ac8000, 0x0000000000003fef,     // +C3
    0xb60b60b60a8bb800, 0x000000000000bff5,     // -C2
    0xaaaaaaaaaaaa6000, 0x0000000000003ffa,     // +C1
    0x8000000000000000, 0x000000000000bffe,     // -0.5
    0x8000000000000000, 0x0000000000003fff      // +1
};

// Range thresholds for tan argument folding (pi/4 splits tan/cot domain).
static const double mexce_trig_tan_x_thresholds[] = {
    0.7853981633974483,  // +pi/4
    -0.7853981633974483  // -pi/4
};

// Arch-specific tiny opcode helpers (NO #if inside their bodies)
#ifdef MEXCE_64
#   define MEXCE_MOV_RAX_IMM    0x48,0xb8, 0,0,0,0,0,0,0,0     /* mov rax, imm64 */
#   define MEXCE_MOV_RDX_IMM    0x48,0xba, 0,0,0,0,0,0,0,0     /* mov rdx, imm64 */
#   define MEXCE_ADD_RAX_IMM32  0x48,0x05, 0,0,0,0             /* add rax, imm32 */
#   define MEXCE_ADD_RAX_80     0x48,0x05, 0x80,0x00,0x00,0x00 /* add rax, 80h   */
#   define MEXCE_FLD_BASE0      0xdb,0x28                      /* fld tword [rax]*/
#   define MEXCE_FLD_BASE(d)    0xdb,0x68,(d)                  /* fld [rax+d]    */
#   define MEXCE_FLDQ_RDX(d)    0xdd,0x42,(d)                  /* fld qword [rdx+d] */
#else
#   define MEXCE_MOV_RAX_IMM    0xb8, 0,0,0,0                  /* mov eax, imm32 */
#   define MEXCE_MOV_RDX_IMM    0xba, 0,0,0,0                  /* mov edx, imm32 */
#   define MEXCE_ADD_RAX_IMM32  0x05, 0,0,0,0                  /* add eax, imm32 */
#   define MEXCE_ADD_RAX_80     0x05, 0x80,0x00,0x00,0x00      /* add eax, 80h   */
#   define MEXCE_FLD_BASE0      0xdb,0x28                      /* fld tword [eax]*/
#   define MEXCE_FLD_BASE(d)    0xdb,0x68,(d)                  /* fld [eax+d]    */
#   define MEXCE_FLDQ_RDX(d)    0xdd,0x42,(d)                  /* fld qword [edx+d] */
#endif

// Shared snippets
// x = remainder(x, 2π) using FPREM1 (single step; assumes typical small quotients).
#define MEXCE_TRIG_RANGE_REDUCE  0xd9,0xeb, 0xd8,0xc0, 0xd9,0xc9, 0xd9,0xf5, 0xdd,0xd9
#define MEXCE_TRIG_Y_SQUARED     0xdc,0xc8

// t = x − π/2 using FSCALE (avoid FDIV)
// Stack dance: push -1, scale π by 2^{-1}, then subtract from x.
#define MEXCE_SIN_PRESHIFT \
    0xd9,0xe8, /* fld1          */ \
    0xd9,0xe0, /* fchs   => -1  */ \
    0xd9,0xeb, /* fldpi         */ \
    0xd9,0xfd, /* fscale => π/2 */ \
    0xdd,0xd9, /* fstp  st(1)   (pop -1) */ \
    0xde,0xe9  /* fsubp st(1), st => x - π/2 */

inline Function Cos()
{
#if MEXCE_TRIG_USE_X87
    uint8_t code[] = { 0xd9, 0xff }; // fcos
    return Function(0, "cos", 1, 0, sizeof(code), code);
#else
    auto emit_add_rax = [](mexce_charstream& buf, uint32_t imm32) {
#ifdef MEXCE_64
        buf < 0x48 < 0x05;                          // add rax, imm32
#else
        buf < 0x05;                                 // add eax, imm32
#endif
        buf << imm32;
    };

    auto emit_horner = [&](mexce_charstream& buf, int coeff_count) {
        // y in st(0); Horner on y with coeffs at [rax + i*16].
        buf < 0xdb < 0x28;                          // fld tword ptr [rax]

        const int first_block = (coeff_count < 8) ? coeff_count : 8;
        for (int i = 1; i < first_block; ++i) {
            buf < 0xd8 < 0xc9;                      // fmul st, st(1)
            buf < 0xdb < 0x68 < (i * 0x10);         // fld tword ptr [rax + i*16]
            buf < 0xde < 0xc1;                      // faddp st(1), st
        }

        if (coeff_count > 8) {
            const int remaining = coeff_count - 8;
            for (int j = 0; j < remaining; ++j) {
                buf < 0xd8 < 0xc9;                  // fmul st, st(1)
                if (j == 0) {
                    emit_add_rax(buf, 0x80);
                    buf < 0xdb < 0x28;              // fld tword ptr [rax]
                } else {
                    buf < 0xdb < 0x68 < (j * 0x10); // fld tword ptr [rax + j*16]
                }
                buf < 0xde < 0xc1;                  // faddp st(1), st
            }
        }

        buf < 0xdd < 0xd9;                          // fstp st(1)  (pop y)
    };

    mexce_charstream buf;

#if MEXCE_TRIG_USE_PIO2_KERNEL
    auto emit_jb = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x82;                          // jb rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jz = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x84;                          // jz rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jmp = [&](size_t& rel32_pos) {
        buf < 0xe9;                                 // jmp rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    // Default quadrant: q=0 (no reduction needed).
    buf < 0x31 < 0xc9;                              // xor ecx, ecx

    // mov rdx, tan_threshold_base (pi/4 at +0)
#ifdef MEXCE_64
    buf < 0x48 < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Fast-path check: if |x| > pi/4, do reduction; otherwise directly evaluate the cos kernel on x.
    size_t jb_to_reduce = 0;
    buf
        < 0xd9 < 0xc0                               // fld st(0)          (x, x)
        < 0xd9 < 0xe1                               // fabs              (|x|, x)
        < 0xdd < 0x42 < 0x00                        // fld qword [rdx+0]  (pi/4, |x|, x)
        < 0xdb < 0xf1                               // fcomi st,st(1)     (pi/4 vs |x|)
        < 0xdd < 0xd8                               // fstp st(0)         (pop pi/4)
        < 0xd9 < 0xc9                               // fxch st(1)         (x, |x|)
        < 0xdd < 0xd9;                              // fstp st(1)         (pop |x|) => x
    emit_jb(jb_to_reduce);                          // jump if pi/4 < |x|

    // Fast path: cos(x) kernel on |x| <= pi/4.
    buf
        < 0xd9 < 0xc0                               // fld st(0)          (x, x)
        < 0xdc < 0xc8;                              // fmul st(0), st(0)  (y, x)

#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t fast_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t fast_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 8);
    buf < 0xdd < 0xd9;                              // fstp st(1)         (pop x)

    size_t jmp_to_end = 0;
    emit_jmp(jmp_to_end);

    const size_t label_reduce = buf.buf.size();

    // mov rdx, pio2_const_base
#ifdef MEXCE_64
    buf < 0x48 < 0xba;
    const size_t pio2_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xba;
    const size_t pio2_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Reduce x = k*(pi/2) + r, with r in [-pi/4, pi/4], using k = rint(x * 2/pi).
    buf < 0xd9 < 0xc0;                              // fld st(0)            (x, x)
    buf < 0xdd < 0x42 < 0x00;                       // fld qword [rdx+0]    (2/pi, x, x)
    buf < 0xde < 0xc9;                              // fmulp st(1), st      (x*2/pi, x)
    buf < 0xd9 < 0xfc;                              // frndint              (k, x)
#ifndef MEXCE_64
    buf < 0x83 < 0xec < 0x08;                       // sub esp, 8  (scratch)
#endif
    buf < 0xdf < 0x3c < 0x24;                       // fistp qword [rsp]    (x)

    buf < 0x8b < 0x0c < 0x24;                       // mov ecx, dword [rsp]
    buf < 0x83 < 0xe1 < 0x03;                       // and ecx, 3

    buf < 0xdf < 0x2c < 0x24;                       // fild qword [rsp]     (k, x)
#ifndef MEXCE_64
    buf < 0x83 < 0xc4 < 0x08;                       // add esp, 8  (release scratch)
#endif
    buf < 0xd9 < 0xc0;                              // fld st(0)             (k, k, x)
    buf < 0xdd < 0x42 < 0x08;                       // fld qword [rdx+8]     (pio2_1, k, k, x)
    buf < 0xde < 0xc9;                              // fmulp st(1), st       (k*pio2_1, k, x)
    buf < 0xde < 0xea;                              // fsubp st(2), st       (k, x-k*pio2_1)
    buf < 0xdd < 0x42 < 0x10;                       // fld qword [rdx+16]    (pio2_1t, k, r1)
    buf < 0xde < 0xc9;                              // fmulp st(1), st       (k*pio2_1t, r1)
    buf < 0xde < 0xe9;                              // fsubp st(1), st       (r)

    // y = r^2, keep r: (y, r)
    buf < 0xd9 < 0xc0;                              // fld st(0)
    buf < 0xdc < 0xc8;                              // fmul st(0), st(0)

    // If (q&1)==0 => cos kernel, else sin kernel.
    buf < 0xf6 < 0xc1 < 0x01;                       // test cl, 1
    size_t jz_to_cos = 0;
    emit_jz(jz_to_cos);

    // sin kernel: r * P(y)
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t red_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t red_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 7);
    buf < 0xde < 0xc9;                              // fmulp st(1), st

    size_t jmp_to_have_kernel = 0;
    emit_jmp(jmp_to_have_kernel);

    const size_t label_cos = buf.buf.size();
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t red_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t red_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 8);
    buf < 0xdd < 0xd9;                              // fstp st(1) (pop r)

    const size_t label_have_kernel = buf.buf.size();

    // Sign is negative for q in {1,2} => ((q+1) & 2) != 0.
    buf
        < 0xd9 < 0xc0                               // fld st(0)
        < 0xd9 < 0xe0                               // fchs            (-v, v)
        < 0x8d < 0x41 < 0x01                        // lea eax, [ecx+1]
        < 0xf6 < 0xc0 < 0x02                        // test al, 2
        < 0xda < 0xc9                               // fcmove st,st(1) (choose +v when sign not set)
        < 0xdd < 0xd9;                              // fstp st(1)

    const size_t label_end = buf.buf.size();

    patch_rel32(buf, jb_to_reduce, label_reduce);
    patch_rel32(buf, jmp_to_end, label_end);
    patch_rel32(buf, jz_to_cos, label_cos);
    patch_rel32(buf, jmp_to_have_kernel, label_have_kernel);

    auto patch_ptr = [&](size_t imm_pos, void* ptr) {
        std::memcpy(buf.buf.data() + imm_pos, &ptr, sizeof(ptr));
    };

    void* tan_thr_ptr = (void*)mexce_trig_tan_x_thresholds;
    void* pio2_ptr    = (void*)mexce_trig_pio2_reduce_consts;
    void* sin_ptr     = (void*)mexce_trig_sin_kernel_factors;
    void* cos_ptr     = (void*)mexce_trig_cos_kernel_factors;

    patch_ptr(tan_thr_imm_pos, tan_thr_ptr);
    patch_ptr(pio2_imm_pos, pio2_ptr);
    patch_ptr(fast_cos_coeff_imm_pos, cos_ptr);
    patch_ptr(red_sin_coeff_imm_pos, sin_ptr);
    patch_ptr(red_cos_coeff_imm_pos, cos_ptr);

    return Function(0, "cos", 1, 0, buf.buf.size(), buf.buf.data());
#else
    auto emit_jb = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x82;                          // jb rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jmp = [&](size_t& rel32_pos) {
        buf < 0xe9;                                 // jmp rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    // Fast path: for |x| <= π, skip range reduction and evaluate the polynomial directly.
    size_t jb_to_reduce = 0;
    size_t jmp_to_have_y = 0;

    buf
        < 0xd9 < 0xe1                               // fabs
        < 0xd9 < 0xeb                               // fldpi
        < 0xdb < 0xf1                               // fcomi st,st(1)  (compare pi vs |x|)
        < 0xdd < 0xd8;                              // fstp st(0)      (pop pi)
    emit_jb(jb_to_reduce);                          // jump if pi < |x|

    buf < 0xdc < 0xc8;                              // y = x^2
    emit_jmp(jmp_to_have_y);

    const size_t label_reduce = buf.buf.size();
    {
        const uint8_t prefix[] = { MEXCE_TRIG_RANGE_REDUCE, MEXCE_TRIG_Y_SQUARED };
        buf.write(reinterpret_cast<const char*>(prefix), sizeof(prefix));
    }

    const size_t label_have_y = buf.buf.size();

    // mov rax, coeff_base  / mov rdx, threshold_base
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
    buf < 0x48 < 0xba;
    const size_t thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
    buf < 0xba;
    const size_t thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Compare thr[i] (st0) vs y (st1): JB when thr[i] < y (i.e., y > thr[i]).
    auto emit_thr_cmp = [&](uint8_t thr_disp, size_t& jb_rel32_pos) {
        buf < 0xdd < 0x42 < thr_disp;               // fld qword ptr [rdx+thr_disp]
        buf < 0xdb < 0xf1;                          // fcomi st,st(1)
        buf < 0xdd < 0xd8;                          // fstp st(0)  (pop threshold)
        emit_jb(jb_rel32_pos);
    };

    enum { cmp_count = 5, end_jmp_count = 5 };
    size_t jb_to_cmp[cmp_count];
    size_t label_cmp[cmp_count];
    size_t jmp_to_end[end_jmp_count];

    // thr0 -> degree5 (10!)
    emit_thr_cmp(0, jb_to_cmp[0]);
    emit_add_rax(buf, 0xa0);
    emit_horner(buf, 6);
    emit_jmp(jmp_to_end[0]);

    label_cmp[0] = buf.buf.size();
    // thr1 -> degree7 (14!)
    emit_thr_cmp(8, jb_to_cmp[1]);
    emit_add_rax(buf, 0x80);
    emit_horner(buf, 8);
    emit_jmp(jmp_to_end[1]);

    label_cmp[1] = buf.buf.size();
    // thr2 -> degree9 (18!)
    emit_thr_cmp(16, jb_to_cmp[2]);
    emit_add_rax(buf, 0x60);
    emit_horner(buf, 10);
    emit_jmp(jmp_to_end[2]);

    label_cmp[2] = buf.buf.size();
    // thr3 -> degree11 (22!)
    emit_thr_cmp(24, jb_to_cmp[3]);
    emit_add_rax(buf, 0x40);
    emit_horner(buf, 12);
    emit_jmp(jmp_to_end[3]);

    label_cmp[3] = buf.buf.size();
    // thr4 -> degree13 (26!), else degree15 (30!)
    emit_thr_cmp(32, jb_to_cmp[4]);
    emit_add_rax(buf, 0x20);
    emit_horner(buf, 14);
    emit_jmp(jmp_to_end[4]);

    label_cmp[4] = buf.buf.size();
    emit_horner(buf, 16);
    const size_t label_end = buf.buf.size();

    patch_rel32(buf, jb_to_reduce, label_reduce);
    patch_rel32(buf, jmp_to_have_y, label_have_y);
    for (int i = 0; i < cmp_count; ++i) patch_rel32(buf, jb_to_cmp[i], label_cmp[i]);
    for (int i = 0; i < end_jmp_count; ++i) patch_rel32(buf, jmp_to_end[i], label_end);

    void* coeff_ptr = (void*)mexce_trig_mfactors;
    void* thr_ptr   = (void*)mexce_trig_y_thresholds;
    std::memcpy(buf.buf.data() + coeff_imm_pos, &coeff_ptr, sizeof(coeff_ptr));
    std::memcpy(buf.buf.data() + thr_imm_pos, &thr_ptr, sizeof(thr_ptr));

    return Function(0, "cos", 1, 0, buf.buf.size(), buf.buf.data());
#endif
#endif
}



inline Function Sin()
{
#if MEXCE_TRIG_USE_X87
    uint8_t code[] = { 0xd9, 0xfe }; // fsin
    return Function(0, "sin", 1, 0, sizeof(code), code);
#else
    auto emit_add_rax = [](mexce_charstream& buf, uint32_t imm32) {
#ifdef MEXCE_64
        buf < 0x48 < 0x05;                          // add rax, imm32
#else
        buf < 0x05;                                 // add eax, imm32
#endif
        buf << imm32;
    };

    auto emit_horner = [&](mexce_charstream& buf, int coeff_count) {
        // y in st(0); Horner on y with coeffs at [rax + i*16].
        buf < 0xdb < 0x28;                          // fld tword ptr [rax]

        const int first_block = (coeff_count < 8) ? coeff_count : 8;
        for (int i = 1; i < first_block; ++i) {
            buf < 0xd8 < 0xc9;                      // fmul st, st(1)
            buf < 0xdb < 0x68 < (i * 0x10);         // fld tword ptr [rax + i*16]
            buf < 0xde < 0xc1;                      // faddp st(1), st
        }

        if (coeff_count > 8) {
            const int remaining = coeff_count - 8;
            for (int j = 0; j < remaining; ++j) {
                buf < 0xd8 < 0xc9;                  // fmul st, st(1)
                if (j == 0) {
                    emit_add_rax(buf, 0x80);
                    buf < 0xdb < 0x28;              // fld tword ptr [rax]
                } else {
                    buf < 0xdb < 0x68 < (j * 0x10); // fld tword ptr [rax + j*16]
                }
                buf < 0xde < 0xc1;                  // faddp st(1), st
            }
        }

        buf < 0xdd < 0xd9;                          // fstp st(1)  (pop y)
    };

    mexce_charstream buf;

#if MEXCE_TRIG_USE_PIO2_KERNEL
    auto emit_jb = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x82;                          // jb rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jz = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x84;                          // jz rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jmp = [&](size_t& rel32_pos) {
        buf < 0xe9;                                 // jmp rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    // Default quadrant: q=0 (no reduction needed).
    buf < 0x31 < 0xc9;                              // xor ecx, ecx

    // mov rdx, tan_threshold_base (pi/4 at +0)
#ifdef MEXCE_64
    buf < 0x48 < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Fast-path check: if |x| > pi/4, do reduction; otherwise directly evaluate the sin kernel on x.
    size_t jb_to_reduce = 0;
    buf
        < 0xd9 < 0xc0                               // fld st(0)          (x, x)
        < 0xd9 < 0xe1                               // fabs              (|x|, x)
        < 0xdd < 0x42 < 0x00                        // fld qword [rdx+0]  (pi/4, |x|, x)
        < 0xdb < 0xf1                               // fcomi st,st(1)     (pi/4 vs |x|)
        < 0xdd < 0xd8                               // fstp st(0)         (pop pi/4)
        < 0xd9 < 0xc9                               // fxch st(1)         (x, |x|)
        < 0xdd < 0xd9;                              // fstp st(1)         (pop |x|) => x
    emit_jb(jb_to_reduce);                          // jump if pi/4 < |x|

    // Fast path: sin(x) kernel on |x| <= pi/4.
    buf
        < 0xd9 < 0xc0                               // fld st(0)          (x, x)
        < 0xdc < 0xc8;                              // fmul st(0), st(0)  (y, x)

#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t fast_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t fast_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 7);
    buf < 0xde < 0xc9;                              // fmulp st(1), st

    size_t jmp_to_end = 0;
    emit_jmp(jmp_to_end);

    const size_t label_reduce = buf.buf.size();

    // mov rdx, pio2_const_base
#ifdef MEXCE_64
    buf < 0x48 < 0xba;
    const size_t pio2_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xba;
    const size_t pio2_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Reduce x = k*(pi/2) + r, with r in [-pi/4, pi/4], using k = rint(x * 2/pi).
    buf < 0xd9 < 0xc0;                              // fld st(0)            (x, x)
    buf < 0xdd < 0x42 < 0x00;                       // fld qword [rdx+0]    (2/pi, x, x)
    buf < 0xde < 0xc9;                              // fmulp st(1), st      (x*2/pi, x)
    buf < 0xd9 < 0xfc;                              // frndint              (k, x)
#ifndef MEXCE_64
    buf < 0x83 < 0xec < 0x08;                       // sub esp, 8  (scratch)
#endif
    buf < 0xdf < 0x3c < 0x24;                       // fistp qword [rsp]    (x)

    buf < 0x8b < 0x0c < 0x24;                       // mov ecx, dword [rsp]
    buf < 0x83 < 0xe1 < 0x03;                       // and ecx, 3

    buf < 0xdf < 0x2c < 0x24;                       // fild qword [rsp]     (k, x)
#ifndef MEXCE_64
    buf < 0x83 < 0xc4 < 0x08;                       // add esp, 8  (release scratch)
#endif
    buf < 0xd9 < 0xc0;                              // fld st(0)             (k, k, x)
    buf < 0xdd < 0x42 < 0x08;                       // fld qword [rdx+8]     (pio2_1, k, k, x)
    buf < 0xde < 0xc9;                              // fmulp st(1), st       (k*pio2_1, k, x)
    buf < 0xde < 0xea;                              // fsubp st(2), st       (k, x-k*pio2_1)
    buf < 0xdd < 0x42 < 0x10;                       // fld qword [rdx+16]    (pio2_1t, k, r1)
    buf < 0xde < 0xc9;                              // fmulp st(1), st       (k*pio2_1t, r1)
    buf < 0xde < 0xe9;                              // fsubp st(1), st       (r)

    // y = r^2, keep r: (y, r)
    buf < 0xd9 < 0xc0;                              // fld st(0)
    buf < 0xdc < 0xc8;                              // fmul st(0), st(0)

    // If (q&1)==0 => sin kernel, else cos kernel.
    buf < 0xf6 < 0xc1 < 0x01;                       // test cl, 1
    size_t jz_to_sin = 0;
    emit_jz(jz_to_sin);

    // cos kernel: Q(y)
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t red_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t red_cos_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 8);
    buf < 0xdd < 0xd9;                              // fstp st(1) (pop r)

    size_t jmp_to_have_kernel = 0;
    emit_jmp(jmp_to_have_kernel);

    const size_t label_sin = buf.buf.size();

    // sin kernel: r * P(y)
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t red_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t red_sin_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif
    emit_horner(buf, 7);
    buf < 0xde < 0xc9;                              // fmulp st(1), st

    const size_t label_have_kernel = buf.buf.size();

    // Sign is negative for q in {2,3} => (q & 2) != 0.
    buf
        < 0xd9 < 0xc0                               // fld st(0)
        < 0xd9 < 0xe0                               // fchs            (-v, v)
        < 0xf6 < 0xc1 < 0x02                        // test cl, 2
        < 0xda < 0xc9                               // fcmove st,st(1) (choose +v when sign not set)
        < 0xdd < 0xd9;                              // fstp st(1)

    const size_t label_end = buf.buf.size();

    patch_rel32(buf, jb_to_reduce, label_reduce);
    patch_rel32(buf, jmp_to_end, label_end);
    patch_rel32(buf, jz_to_sin, label_sin);
    patch_rel32(buf, jmp_to_have_kernel, label_have_kernel);

    auto patch_ptr = [&](size_t imm_pos, void* ptr) {
        std::memcpy(buf.buf.data() + imm_pos, &ptr, sizeof(ptr));
    };

    void* tan_thr_ptr = (void*)mexce_trig_tan_x_thresholds;
    void* pio2_ptr    = (void*)mexce_trig_pio2_reduce_consts;
    void* sin_ptr     = (void*)mexce_trig_sin_kernel_factors;
    void* cos_ptr     = (void*)mexce_trig_cos_kernel_factors;

    patch_ptr(tan_thr_imm_pos, tan_thr_ptr);
    patch_ptr(pio2_imm_pos, pio2_ptr);
    patch_ptr(fast_sin_coeff_imm_pos, sin_ptr);
    patch_ptr(red_sin_coeff_imm_pos, sin_ptr);
    patch_ptr(red_cos_coeff_imm_pos, cos_ptr);

    return Function(0, "sin", 1, 0, buf.buf.size(), buf.buf.data());
#else
    auto emit_jb = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x82;                          // jb rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jmp = [&](size_t& rel32_pos) {
        buf < 0xe9;                                 // jmp rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    // Fast path: for |x| <= pi, skip range reduction and evaluate the polynomial directly.
    size_t jb_to_reduce = 0;
    size_t jmp_to_have_r = 0;

    buf
        < 0xd9 < 0xc0                               // fld st(0)        (dup x)
        < 0xd9 < 0xe1                               // fabs            (|x|, x)
        < 0xd9 < 0xeb                               // fldpi
        < 0xdb < 0xf1                               // fcomi st,st(1)  (compare pi vs |x|)
        < 0xdd < 0xd8;                              // fstp st(0)      (pop pi)
    emit_jb(jb_to_reduce);                          // jump if pi < |x|

    buf < 0xdd < 0xd8;                              // fstp st(0)      (pop |x|, keep x)
    emit_jmp(jmp_to_have_r);

    const size_t label_reduce = buf.buf.size();
    {
        // stack: |x|, x
        buf < 0xd9 < 0xc9;                          // fxch st(1)     (x, |x|)
        buf < 0xdd < 0xd9;                          // fstp st(1)     (pop |x|, keep x)
        const uint8_t prefix[] = { MEXCE_TRIG_RANGE_REDUCE };
        buf.write(reinterpret_cast<const char*>(prefix), sizeof(prefix));
    }

    const size_t label_have_r = buf.buf.size();

    // y = r^2, keep r for the final multiplication: stack becomes (y, r).
    buf < 0xd9 < 0xc0;                              // fld st(0)
    buf < 0xdc < 0xc8;                              // fmul st(0), st(0)

    // mov rax, coeff_base  / mov rdx, threshold_base
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
    buf < 0x48 < 0xba;
    const size_t thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
    buf < 0xba;
    const size_t thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // Compare thr[i] (st0) vs y (st1): JB when thr[i] < y (i.e., y > thr[i]).
    auto emit_thr_cmp = [&](uint8_t thr_disp, size_t& jb_rel32_pos) {
        buf < 0xdd < 0x42 < thr_disp;               // fld qword ptr [rdx+thr_disp]
        buf < 0xdb < 0xf1;                          // fcomi st,st(1)
        buf < 0xdd < 0xd8;                          // fstp st(0)  (pop threshold)
        emit_jb(jb_rel32_pos);
    };

    enum { cmp_count = 5, mul_jmp_count = 5 };
    size_t jb_to_cmp[cmp_count];
    size_t label_cmp[cmp_count];
    size_t jmp_to_mul[mul_jmp_count];

    // thr0 -> degree4 (9!)
    emit_thr_cmp(0, jb_to_cmp[0]);
    emit_add_rax(buf, 0xa0);
    emit_horner(buf, 5);
    emit_jmp(jmp_to_mul[0]);

    label_cmp[0] = buf.buf.size();
    // thr1 -> degree6 (13!)
    emit_thr_cmp(8, jb_to_cmp[1]);
    emit_add_rax(buf, 0x80);
    emit_horner(buf, 7);
    emit_jmp(jmp_to_mul[1]);

    label_cmp[1] = buf.buf.size();
    // thr2 -> degree8 (17!)
    emit_thr_cmp(16, jb_to_cmp[2]);
    emit_add_rax(buf, 0x60);
    emit_horner(buf, 9);
    emit_jmp(jmp_to_mul[2]);

    label_cmp[2] = buf.buf.size();
    // thr3 -> degree10 (21!)
    emit_thr_cmp(24, jb_to_cmp[3]);
    emit_add_rax(buf, 0x40);
    emit_horner(buf, 11);
    emit_jmp(jmp_to_mul[3]);

    label_cmp[3] = buf.buf.size();
    // thr4 -> degree12 (25!), else degree14 (29!)
    emit_thr_cmp(32, jb_to_cmp[4]);
    emit_add_rax(buf, 0x20);
    emit_horner(buf, 13);
    emit_jmp(jmp_to_mul[4]);

    label_cmp[4] = buf.buf.size();
    emit_horner(buf, 15);

    const size_t label_mul = buf.buf.size();
    buf < 0xde < 0xc9;                              // fmulp st(1), st  => r * P(y)

    patch_rel32(buf, jb_to_reduce, label_reduce);
    patch_rel32(buf, jmp_to_have_r, label_have_r);
    for (int i = 0; i < cmp_count; ++i) patch_rel32(buf, jb_to_cmp[i], label_cmp[i]);
    for (int i = 0; i < mul_jmp_count; ++i) patch_rel32(buf, jmp_to_mul[i], label_mul);

    void* coeff_ptr = (void*)mexce_trig_sinfactors;
    void* thr_ptr   = (void*)mexce_trig_sin_y_thresholds;
    std::memcpy(buf.buf.data() + coeff_imm_pos, &coeff_ptr, sizeof(coeff_ptr));
    std::memcpy(buf.buf.data() + thr_imm_pos, &thr_ptr, sizeof(thr_ptr));

    return Function(0, "sin", 1, 0, buf.buf.size(), buf.buf.data());
#endif
#endif
}


inline Function Tan()
{
#if MEXCE_TRIG_USE_X87
    uint8_t code[] = {
        0xd9, 0xf2,                                 // fptan
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function(0, "tan", 1, 1, sizeof(code), code);
#else
    auto emit_add_rax = [](mexce_charstream& buf, uint32_t imm32) {
#ifdef MEXCE_64
        buf < 0x48 < 0x05;                          // add rax, imm32
#else
        buf < 0x05;                                 // add eax, imm32
#endif
        buf << imm32;
    };

    auto emit_horner = [&](mexce_charstream& buf, int coeff_count) {
        // y in st(0); Horner on y with coeffs at [rax + i*16].
        buf < 0xdb < 0x28;                          // fld tword ptr [rax]

        const int first_block = (coeff_count < 8) ? coeff_count : 8;
        for (int i = 1; i < first_block; ++i) {
            buf < 0xd8 < 0xc9;                      // fmul st, st(1)
            buf < 0xdb < 0x68 < (i * 0x10);         // fld tword ptr [rax + i*16]
            buf < 0xde < 0xc1;                      // faddp st(1), st
        }

        if (coeff_count > 8) {
            const int remaining = coeff_count - 8;
            for (int j = 0; j < remaining; ++j) {
                buf < 0xd8 < 0xc9;                  // fmul st, st(1)
                if (j == 0) {
                    emit_add_rax(buf, 0x80);
                    buf < 0xdb < 0x28;              // fld tword ptr [rax]
                } else {
                    buf < 0xdb < 0x68 < (j * 0x10); // fld tword ptr [rax + j*16]
                }
                buf < 0xde < 0xc1;                  // faddp st(1), st
            }
        }

        buf < 0xdd < 0xd9;                          // fstp st(1)  (pop y)
    };

    mexce_charstream buf;

    auto emit_jb = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x82;                          // jb rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_ja = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x87;                          // ja rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jz = [&](size_t& rel32_pos) {
        buf < 0x0f < 0x84;                          // jz rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    auto emit_jmp = [&](size_t& rel32_pos) {
        buf < 0xe9;                                 // jmp rel32
        rel32_pos = buf.buf.size();
        buf << (uint32_t)0;
    };

    // ecx = case selector: 0=direct, 1=pos fold, 2=neg fold.
    buf < 0xb9;
    buf << (uint32_t)0;

    // Fast path: if |x| <= pi/2, skip range reduction; else reduce by pi.
    size_t jb_to_reduce = 0;
    size_t jmp_to_have_r = 0;

    buf
        < 0xd9 < 0xc0                               // fld st(0)  (dup x)
        < 0xd9 < 0xe1                               // fabs       (|x|, x)
        < 0xd9 < 0xeb                               // fldpi
        < 0xd9 < 0xe8                               // fld1
        < 0xd9 < 0xe0                               // fchs       => -1
        < 0xd9 < 0xc9                               // fxch st(1)  (pi, -1, |x|, x)
        < 0xd9 < 0xfd                               // fscale      => pi/2
        < 0xdd < 0xd9                               // fstp st(1)   (pop -1)
        < 0xdb < 0xf1                               // fcomi st,st(1) (compare pi/2 vs |x|)
        < 0xdd < 0xd8;                              // fstp st(0)     (pop pi/2)
    emit_jb(jb_to_reduce);                          // jump if pi/2 < |x|

    buf < 0xdd < 0xd8;                              // fstp st(0)     (pop |x|) => x
    emit_jmp(jmp_to_have_r);

    const size_t label_reduce = buf.buf.size();
    buf
        < 0xd9 < 0xc9                               // fxch st(1)     (x, |x|)
        < 0xdd < 0xd9                               // fstp st(1)     (pop |x|) => x
        < 0xd9 < 0xeb                               // fldpi          (pi, x)
        < 0xd9 < 0xc9                               // fxch st(1)     (x, pi)
        < 0xd9 < 0xf5                               // fprem1         (remainder, pi)
        < 0xdd < 0xd9;                              // fstp st(1)     (pop pi)

    const size_t label_have_r = buf.buf.size();

    // mov rdx, tan_threshold_base (pi/4 and -pi/4)
#ifdef MEXCE_64
    buf < 0x48 < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xba;
    const size_t tan_thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // If r > pi/4: u = pi/2 - r, and take reciprocal at the end.
    size_t jb_to_pos_large = 0;
    buf
        < 0xdd < 0x42 < 0x00                        // fld qword ptr [rdx+0]  (pi/4, r)
        < 0xdb < 0xf1                               // fcomi st,st(1)
        < 0xdd < 0xd8;                              // fstp st(0)  (pop pi/4)
    emit_jb(jb_to_pos_large);

    // If r < -pi/4: u = pi/2 + r, take reciprocal and negate.
    size_t ja_to_neg_large = 0;
    buf
        < 0xdd < 0x42 < 0x08                        // fld qword ptr [rdx+8]  (-pi/4, r)
        < 0xdb < 0xf1                               // fcomi st,st(1)
        < 0xdd < 0xd8;                              // fstp st(0)  (pop -pi/4)
    emit_ja(ja_to_neg_large);                       // jump if (-pi/4) > r

    // Direct: u = r
    size_t jmp_to_have_u = 0;
    emit_jmp(jmp_to_have_u);

    const size_t label_pos_large = buf.buf.size();
    buf < 0xb9;
    buf << (uint32_t)1;
    // u = pi/2 - r
    buf
        < 0xd9 < 0xe8                               // fld1
        < 0xd9 < 0xe0                               // fchs   => -1
        < 0xd9 < 0xeb                               // fldpi
        < 0xd9 < 0xfd                               // fscale => pi/2
        < 0xdd < 0xd9                               // fstp st(1)  (pop -1)
        < 0xd9 < 0xc9                               // fxch st(1)  (r, pi/2)
        < 0xde < 0xe9;                              // fsubp st(1), st  => pi/2 - r
    size_t jmp_pos_to_have_u = 0;
    emit_jmp(jmp_pos_to_have_u);

    const size_t label_neg_large = buf.buf.size();
    buf < 0xb9;
    buf << (uint32_t)2;
    // u = pi/2 + r  (r is negative here, so u is in [0, pi/4])
    buf
        < 0xd9 < 0xe8                               // fld1
        < 0xd9 < 0xe0                               // fchs   => -1
        < 0xd9 < 0xeb                               // fldpi
        < 0xd9 < 0xfd                               // fscale => pi/2
        < 0xdd < 0xd9                               // fstp st(1)  (pop -1)
        < 0xd9 < 0xc9                               // fxch st(1)  (r, pi/2)
        < 0xde < 0xc1;                              // faddp st(1), st  => pi/2 + r

    const size_t label_have_u = buf.buf.size();

    // y = u^2; keep u for the final multiplication.
    buf
        < 0xd9 < 0xc0                               // fld st(0)  (u, u)
        < 0xdc < 0xc8                               // fmul st(0), st(0) (y, u)
        < 0xd9 < 0xc0;                              // fld st(0)  (y, y, u)

    // ---- cos(u) polynomial on y ----
    enum { cos_cmp_count = 5, cos_end_jmp_count = 5 };
    size_t cos_jb_to_cmp[cos_cmp_count];
    size_t cos_label_cmp[cos_cmp_count];
    size_t cos_jmp_to_end[cos_end_jmp_count];

    // mov rax, cos_coeff_base  / mov rdx, cos_threshold_base
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t cos_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
    buf < 0x48 < 0xba;
    const size_t cos_thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t cos_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
    buf < 0xba;
    const size_t cos_thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    auto emit_thr_cmp = [&](uint8_t thr_disp, size_t& jb_rel32_pos) {
        buf < 0xdd < 0x42 < thr_disp;               // fld qword ptr [rdx+thr_disp]
        buf < 0xdb < 0xf1;                          // fcomi st,st(1)
        buf < 0xdd < 0xd8;                          // fstp st(0)  (pop threshold)
        emit_jb(jb_rel32_pos);
    };

    // thr0 -> degree5 (10!)
    emit_thr_cmp(0, cos_jb_to_cmp[0]);
    emit_add_rax(buf, 0xa0);
    emit_horner(buf, 6);
    emit_jmp(cos_jmp_to_end[0]);

    cos_label_cmp[0] = buf.buf.size();
    // thr1 -> degree7 (14!)
    emit_thr_cmp(8, cos_jb_to_cmp[1]);
    emit_add_rax(buf, 0x80);
    emit_horner(buf, 8);
    emit_jmp(cos_jmp_to_end[1]);

    cos_label_cmp[1] = buf.buf.size();
    // thr2 -> degree9 (18!)
    emit_thr_cmp(16, cos_jb_to_cmp[2]);
    emit_add_rax(buf, 0x60);
    emit_horner(buf, 10);
    emit_jmp(cos_jmp_to_end[2]);

    cos_label_cmp[2] = buf.buf.size();
    // thr3 -> degree11 (22!)
    emit_thr_cmp(24, cos_jb_to_cmp[3]);
    emit_add_rax(buf, 0x40);
    emit_horner(buf, 12);
    emit_jmp(cos_jmp_to_end[3]);

    cos_label_cmp[3] = buf.buf.size();
    // thr4 -> degree13 (26!), else degree15 (30!)
    emit_thr_cmp(32, cos_jb_to_cmp[4]);
    emit_add_rax(buf, 0x20);
    emit_horner(buf, 14);
    emit_jmp(cos_jmp_to_end[4]);

    cos_label_cmp[4] = buf.buf.size();
    emit_horner(buf, 16);

    const size_t cos_label_end = buf.buf.size();

    // ---- sin(u) polynomial on y ----
    buf < 0xd9 < 0xc9;                              // fxch st(1)  (y, cos, u)

    enum { sin_cmp_count = 5, sin_mul_jmp_count = 5 };
    size_t sin_jb_to_cmp[sin_cmp_count];
    size_t sin_label_cmp[sin_cmp_count];
    size_t sin_jmp_to_mul[sin_mul_jmp_count];

    // mov rax, sin_coeff_base  / mov rdx, sin_threshold_base
#ifdef MEXCE_64
    buf < 0x48 < 0xb8;
    const size_t sin_coeff_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
    buf < 0x48 < 0xba;
    const size_t sin_thr_imm_pos = buf.buf.size();
    buf << (uint64_t)0;
#else
    buf < 0xb8;
    const size_t sin_coeff_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
    buf < 0xba;
    const size_t sin_thr_imm_pos = buf.buf.size();
    buf << (uint32_t)0;
#endif

    // thr0 -> degree4 (9!)
    emit_thr_cmp(0, sin_jb_to_cmp[0]);
    emit_add_rax(buf, 0xa0);
    emit_horner(buf, 5);
    emit_jmp(sin_jmp_to_mul[0]);

    sin_label_cmp[0] = buf.buf.size();
    // thr1 -> degree6 (13!)
    emit_thr_cmp(8, sin_jb_to_cmp[1]);
    emit_add_rax(buf, 0x80);
    emit_horner(buf, 7);
    emit_jmp(sin_jmp_to_mul[1]);

    sin_label_cmp[1] = buf.buf.size();
    // thr2 -> degree8 (17!)
    emit_thr_cmp(16, sin_jb_to_cmp[2]);
    emit_add_rax(buf, 0x60);
    emit_horner(buf, 9);
    emit_jmp(sin_jmp_to_mul[2]);

    sin_label_cmp[2] = buf.buf.size();
    // thr3 -> degree10 (21!)
    emit_thr_cmp(24, sin_jb_to_cmp[3]);
    emit_add_rax(buf, 0x40);
    emit_horner(buf, 11);
    emit_jmp(sin_jmp_to_mul[3]);

    sin_label_cmp[3] = buf.buf.size();
    // thr4 -> degree12 (25!), else degree14 (29!)
    emit_thr_cmp(32, sin_jb_to_cmp[4]);
    emit_add_rax(buf, 0x20);
    emit_horner(buf, 13);
    emit_jmp(sin_jmp_to_mul[4]);

    sin_label_cmp[4] = buf.buf.size();
    emit_horner(buf, 15);

    const size_t sin_label_mul = buf.buf.size();
    buf < 0xde < 0xca;                              // fmulp st(2), st  => cos, sin

    buf < 0xd9 < 0xc9;                              // fxch st(1)      => sin, cos
    buf < 0xde < 0xf1;                              // fdivrp st(1),st => tan(u)

    // If folded (ecx!=0), tan(x) = +/- 1/tan(u).
    size_t jz_to_end = 0;

    buf < 0x85 < 0xc9;                              // test ecx,ecx
    emit_jz(jz_to_end);
    buf < 0xd9 < 0xe8;                              // fld1
    buf < 0xde < 0xf1;                              // fdivrp st(1),st  => 1/tan(u)
    // Negate for ecx==2 (neg fold): (-v, v), test cl,2 and fcmove to pick +v.
    buf
        < 0xd9 < 0xc0                               // fld st(0)
        < 0xd9 < 0xe0                               // fchs            (-v, v)
        < 0xf6 < 0xc1 < 0x02                        // test cl, 2
        < 0xda < 0xc9                               // fcmove st,st(1) (choose +v when sign not set)
        < 0xdd < 0xd9;                              // fstp st(1)

    const size_t label_end = buf.buf.size();

    // Patch control-flow rel32s.
    patch_rel32(buf, jb_to_reduce, label_reduce);
    patch_rel32(buf, jmp_to_have_r, label_have_r);
    patch_rel32(buf, jb_to_pos_large, label_pos_large);
    patch_rel32(buf, ja_to_neg_large, label_neg_large);
    patch_rel32(buf, jmp_to_have_u, label_have_u);
    patch_rel32(buf, jmp_pos_to_have_u, label_have_u);

    for (int i = 0; i < cos_cmp_count; ++i) patch_rel32(buf, cos_jb_to_cmp[i], cos_label_cmp[i]);
    for (int i = 0; i < cos_end_jmp_count; ++i) patch_rel32(buf, cos_jmp_to_end[i], cos_label_end);

    for (int i = 0; i < sin_cmp_count; ++i) patch_rel32(buf, sin_jb_to_cmp[i], sin_label_cmp[i]);
    for (int i = 0; i < sin_mul_jmp_count; ++i) patch_rel32(buf, sin_jmp_to_mul[i], sin_label_mul);

    patch_rel32(buf, jz_to_end, label_end);

    // Patch embedded pointers.
    void* tan_thr_ptr = (void*)mexce_trig_tan_x_thresholds;
    std::memcpy(buf.buf.data() + tan_thr_imm_pos, &tan_thr_ptr, sizeof(tan_thr_ptr));

    void* cos_coeff_ptr = (void*)mexce_trig_mfactors;
    void* cos_thr_ptr   = (void*)mexce_trig_y_thresholds;
    std::memcpy(buf.buf.data() + cos_coeff_imm_pos, &cos_coeff_ptr, sizeof(cos_coeff_ptr));
    std::memcpy(buf.buf.data() + cos_thr_imm_pos, &cos_thr_ptr, sizeof(cos_thr_ptr));

    void* sin_coeff_ptr = (void*)mexce_trig_sinfactors;
    void* sin_thr_ptr   = (void*)mexce_trig_sin_y_thresholds;
    std::memcpy(buf.buf.data() + sin_coeff_imm_pos, &sin_coeff_ptr, sizeof(sin_coeff_ptr));
    std::memcpy(buf.buf.data() + sin_thr_imm_pos, &sin_thr_ptr, sizeof(sin_thr_ptr));

    return Function(0, "tan", 1, 0, buf.buf.size(), buf.buf.data());
#endif
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
pair<elist_it_t, elist_it_t> get_dependent_chunk(elist_it_t it);

inline
void compile_elist(impl::mexce_charstream& code_buffer, const impl::elist_const_it_t first, const impl::elist_const_it_t last);

inline
void emit_integer_power_sequence(impl::mexce_charstream& s, uint32_t exponent)
{
    using namespace impl;

    assert(exponent >= 1);

    if (exponent == 1) {
        return;
    }

    // Peel off trailing zeros (squaring the base)
    // Corresponds to right-associative power towers of 2: ((a^2)^2)...
    // Or simply: a ^ (m * 2^k) = (a^(2^k))^m
    while ((exponent & 1) == 0) {
        s < 0xdc < 0xc8;                            // fmul st(0), st(0)
        exponent >>= 1;
    }

    if (exponent == 1) {
        return;
    }

    // Exponent is odd and > 1.
    // We need to accumulate, but avoid multiplying by 1.0.
    // Initialize result with base.
    s < 0xd9 < 0xc0;                                // fld st(0)   -> stack: res, base
    s < 0xd9 < 0xc9;                                // fxch st(1)  -> stack: base, res

    exponent >>= 1;

    while (true) {
        s < 0xdc < 0xc8;                            // fmul st(0), st(0) (base *= base)

        if (exponent & 1) {
             s < 0xdc < 0xc9;                       // fmul st(1), st(0) (res *= base)
        }

        exponent >>= 1;
        if (exponent == 0) {
            s < 0xdd < 0xd8;                        // fstp st(0) (pop base) -> stack: res
            break;
        }
    }
}

inline
void pow_optimizer(elist_it_t it, evaluator* ev, elist_t* elist)
{
    auto f = it->f;

    // Nested power folding: (a^b)^n -> a^(b*n) when the outer exponent is an integer.
    // This matches the benchmark reference semantics (SymPy rational evaluation) and
    // avoids spurious NaNs from intermediate real-domain pow/sqrt on negative bases.
    if (f->args[0]->type == Element_type::CCONST && f->args[1]->type == Element_type::CFUNC) {
        const uint64_t base_id = f->args[1]->id;
        auto pit = ev->m_power_terms.find(base_id);
        if (pit != ev->m_power_terms.end()) {
            const double outer_exp_d = f->args[0]->c->value;
            const double outer_round = round(outer_exp_d);
            if (std::isfinite(outer_exp_d) && outer_round == outer_exp_d) {
                const double inner_exp = pit->second.second;
                const double combined_exp = inner_exp * outer_exp_d;
                if (std::isfinite(combined_exp)) {
                    *f->args[0] = Element(make_intermediate_constant(ev, combined_exp));

                    auto base_it = f->args[1];
                    elist->splice(base_it, pit->second.first);

                    f->args[1] = std::prev(base_it);
                    if (f->args[1]->type == Element_type::CFUNC) {
                        auto cf = f->args[1]->f;
                        cf->parent = it;
                        cf->parent_arg_index = 1;
                    }

                    elist->erase(base_it);
                    ev->m_power_terms.erase(pit);
                }
            }
        }
    }

    // Arg 0 is Exponent (top of stack), Arg 1 is Base (below top) in list
    if (f->args[0]->type == Element_type::CCONST) {
        auto v = f->args[0]->c;

        double v_d = v->value;
        double r_d = round(v_d);
        double a_d = abs(v_d);

        bool matched = true;
        mexce_charstream s;
        string optimized_name;
        string debug_desc;

        // Identify the base arguments
        auto base_chunk_range = get_dependent_chunk(f->args[1]);

        // a special case, that the exponent is 0.5
        if (v_d == 0.5) {
            s < 0xd9 < 0xfa;                        // fsqrt
            optimized_name = "sqrt";
        }
        else
        if (v_d == -0.5) {
            s < 0xd9 < 0xfa                         // fsqrt
              < 0xd9 < 0xe8                         // fld1
              < 0xde < 0xf1;                        // fdivrp st(1), st
            optimized_name = "inv_sqrt";
        }
        else
        if (r_d == v_d && a_d <= 65536.0) {

            uint32_t exponent = static_cast<uint32_t>(a_d);
            bool invert = (v_d < 0.0);

            if (exponent == 0) {
                s < 0xdd < 0xd8                     // fstp st(0)
                  < 0xd9 < 0xe8;                    // fld1
            }
            else {
                emit_integer_power_sequence(s, exponent);

                if (invert) {
                    s < 0xd9 < 0xe8                 // fld1
                      < 0xde < 0xf1;                // fdivrp st(1), st
                }
            }
            optimized_name = "pow_int";
        }
        else {
            matched = false;
        }

        if (matched) {
            // Pre-compile the base code to maintain execution order
            elist_t base_chunk;
            base_chunk.splice(base_chunk.end(), *elist, base_chunk_range.first, base_chunk_range.second);

            mexce_charstream final_s;
            compile_elist(final_s, base_chunk.begin(), base_chunk.end());
            final_s.write((const char*)s.buf.data(), s.buf.size());

            // Generate correct debug string
            string base_str = elist_to_string(base_chunk);
            stringstream ss;
            if (optimized_name == "sqrt") {
                ss << "sqrt(" << base_str << ")";
            }
            else
            if (optimized_name == "inv_sqrt") {
                ss << "inv_sqrt(" << base_str << ")";
            }
            else
            if (optimized_name == "pow_int") {
                ss << "pow_int(" << base_str << ", " << static_cast<int64_t>(v_d) << ")";
            }
            debug_desc = ss.str();

            uint8_t* cc = push_intermediate_code(ev, final_s.str());
            auto f_opt = make_shared<Function>(ev->m_next_element_id++, optimized_name, 0, 0, final_s.buf.size(), cc, nullptr);
            f_opt->debug_desc = debug_desc;
            f_opt->cse_store_suffix = f->cse_store_suffix;  // Preserve CSE store code

            if (optimized_name == "sqrt" || optimized_name == "inv_sqrt" || optimized_name == "pow_int") {
                ev->m_power_terms[f_opt->id] = std::make_pair(std::move(base_chunk), v_d);
            }

            elist->erase(f->args[0]); // Exponent (constant)

            *it = Element(f_opt);
        }
        else {
            // Generic pow (runtime exponent)
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

            uint8_t* cc = push_intermediate_code(ev, s.str());
            auto f_opt = make_shared<Function>(ev->m_next_element_id++, "pow_opt", 2, 0, s.buf.size(), cc, nullptr);
            f_opt->args[0] = f->args[0];
            f_opt->args[1] = f->args[1];
            f_opt->cse_store_suffix = f->cse_store_suffix;  // Preserve CSE store code
            *it = Element(f_opt);
        }
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

        // empty element lists cannot exist
        assert(a.size() && b.size());

        // if their size differs, we use that for comparison
        if (a.size() != b.size()) {
            return a.size() > b.size();
        }

        // they are the same size, thus we need to traverse both
        auto ita = a.begin();
        auto itb = b.begin();
        for (; ita != a.end(); ita++, itb++) {
            // start by comparing element types
            if (ita->type != itb->type) {
                return ita->type > itb->type;
            }

            // Compare deterministic IDs.
            if (ita->id != itb->id) {
                return ita->id < itb->id;
            }
        }

        return false; // they are equal
    }
};


template <uint8_t OP>
void emit_apply_op_with_constant(evaluator* ev, impl::mexce_charstream& s, double v)
{
    using namespace impl;

    auto constant = make_intermediate_constant(ev, v);

#ifdef MEXCE_64
    s < 0x48 < 0xb8;                                // mov            rax, qword ptr
#else
    s < 0xb8;                                       // mov            eax, dword ptr
#endif
    s << constant->address;                         //                     [the address]

    // Constants are always emitted as 64-bit floating point values.
    assert(constant->numeric_data_type == M64FP);
    s < 0xdc < OP;                                  // f[OP]  qword ptr [eax/rax]
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
            s < 0xd9 < 0xe0;                        // fchs
        }
        return;
    }

    auto sc = make_intermediate_constant(ev, v);

#ifdef MEXCE_64
    s < 0x48 < 0xb8;                                // mov            rax, qword ptr
    s << (void*)(sc->address);
    s < 0xdd < 0x00;                                // fld            [rax]
#else
    s < 0xdd < 0x05;                                // fld            [immediate address]
    s << (void*)(sc->address);
#endif
}



inline
void compile_elist(impl::mexce_charstream& code_buffer, const impl::elist_const_it_t first, const impl::elist_const_it_t last)
{
    using namespace impl;

    int current_depth = 0;

    for (auto it = first; it != last; ++it) {
        // Peephole: if the same leaf is loaded twice in a row, duplicate ST(0)
        // instead of reloading from memory.
        if (it != first && (it->type == Element_type::CVAR || it->type == Element_type::CCONST)) {
            auto prev = it;
            --prev;
            if (prev->type == it->type && prev->id == it->id) {
                code_buffer < 0xd9 < 0xc0;          // fld st(0)
                ++current_depth;
                if (current_depth > 8) {
                    throw std::overflow_error("Expression too complex for x87 FPU (stack overflow)");
                }
                continue;
            }
        }

        switch (it->type) {
            case Element_type::CVAR: {
                auto tn = it->v;
#ifdef MEXCE_64
                code_buffer << (uint16_t)0xb848;    // move input address to rax (opcode)
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

                // Fast path for common x87 constants, avoids memory load.
                // Important: do not convert negative zero to positive zero.
                double v  = tn->value;
                double av = std::abs(v);
                bool hit  = false;

                if (av == 0.0 && !std::signbit(v))     { code_buffer < 0xd9 < 0xee; hit = true; } else  // fldz
                if (av == 1.0)                         { code_buffer < 0xd9 < 0xe8; hit = true; } else  // fld1
                if (av == 3.1415926535897932384626433) { code_buffer < 0xd9 < 0xeb; hit = true; } else  // fldpi
                if (av == 3.32192809488736234787)      { code_buffer < 0xd9 < 0xe9; hit = true; } else  // fldl2t
                if (av == 1.44269504088896340736)      { code_buffer < 0xd9 < 0xea; hit = true; } else  // fldl2e
                if (av == 0.3010299956639811952137)    { code_buffer < 0xd9 < 0xec; hit = true; } else  // fldlg2
                if (av == 0.6931471805599453094172)    { code_buffer < 0xd9 < 0xed; hit = true; }       // fldln2

                if (hit) {
                    if (v < 0.0) {
                        code_buffer < 0xd9 < 0xe0;  // fchs
                    }
                } else {
#ifdef MEXCE_64
                    code_buffer << (uint16_t)0xb848;
                    code_buffer << (void*)tn->address;
                    code_buffer < 0xdd < 0x00;
#else
                    code_buffer < 0xdd < 0x05;
                    code_buffer << (void*)(tn->address);
#endif
                }

                ++current_depth;
                break;
            }
            case Element_type::CFUNC: {
                auto tf = it->f;
                code_buffer.write(tf->code.data(), tf->code.size());
                // Emit CSE store suffix if present (stores result to temp for reuse)
                if (!tf->cse_store_suffix.empty()) {
                    code_buffer.write(tf->cse_store_suffix.data(), tf->cse_store_suffix.size());
                }
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
                if (!f->debug_desc.empty()) {
                    // It's an optimized node with no children in this list (absorbed)
                    // Treat as a value node (like CVAR)
                    get<2>(st.back()).push_back(f->debug_desc);
                }
                else {
                    st.push_back(make_tuple(string(), f->args.size() + 1, vector<string>{f->name}));
                }
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

    if (f->parent != elist->end() && f->parent->type == Element_type::CFUNC) {
        shared_ptr<Function> pf = f->parent->f;
        auto pname = pf->name;
        int pclass = (pname == "add" || pname == "sub") ? 1 : (pname == "mul" || pname == "div") ? 2 : 0;
        bool parg2_inv = (pname == "sub" || pname == "div");

        if (pclass && pclass == fclass) {

            // the function will be absorbed by its parent

            bool parent_inv_op = f->parent_arg_index == 0 && parg2_inv; // arg 0 in postfix, is the second infix argument

            // this is the first infix argument (postfix order is inverse)
            auto arg1_chunk = get_dependent_chunk(f->args[1]);
            pf->absorbed[parent_inv_op].push_back(elist_t(arg1_chunk.first, arg1_chunk.second));
            elist->erase(arg1_chunk.first, arg1_chunk.second);

            // the second infix argument
            auto arg0_chunk = get_dependent_chunk(f->args[0]);
            pf->absorbed[parent_inv_op ^ arg2_inv].push_back(elist_t(arg0_chunk.first, arg0_chunk.second));
            elist->erase(arg0_chunk.first, arg0_chunk.second);
            pf->absorbed[ parent_inv_op].insert(pf->absorbed[ parent_inv_op].end(), f->absorbed[0].begin(), f->absorbed[0].end());
            pf->absorbed[!parent_inv_op].insert(pf->absorbed[!parent_inv_op].end(), f->absorbed[1].begin(), f->absorbed[1].end());
            pf->force_not_constant = true;
            *it = Element(make_intermediate_constant(ev, neutral));
            return;
        }
    }

    // end of chain - simplify and generate code

    // accumulate own args

    // first infix argument
    auto arg1_chunk = get_dependent_chunk(f->args[1]);
    f->absorbed[0].push_back(elist_t(arg1_chunk.first, arg1_chunk.second));
    elist->erase(arg1_chunk.first, arg1_chunk.second);

    // second infix argument
    auto arg0_chunk = get_dependent_chunk(f->args[0]);
    f->absorbed[arg2_inv].push_back(elist_t(arg0_chunk.first, arg0_chunk.second));
    elist->erase(arg0_chunk.first, arg0_chunk.second);

    // at this point, this is a function of 0 arguments, all of them were absorbed
    f->args.clear();

    // reduce constants with extended precision
    long double ac[2] = {neutral, neutral};
    for (int i = 0; i < 2; i++) {
        for (auto e = f->absorbed[i].begin(); e != f->absorbed[i].end();) {
            auto next_e = next(e);
            if (e->size() == 1 && e->front().type == Element_type::CCONST) {
                auto v = e->front().c;
                if (fclass == 1) { ac[i] += static_cast<long double>(v->value); }
                else {             ac[i] *= static_cast<long double>(v->value); }
                f->absorbed[i].erase(e);
            }
            e = next_e;
        }
    }
    long double ac_final_ld = (fclass == 1) ? (ac[0] - ac[1]) : (ac[0] / ac[1]);
    double ac_final = static_cast<double>(ac_final_ld);

    struct absorbed_term {
        elist_t chunk;
        int     factor;
    };
    vector<absorbed_term> terms;
    terms.reserve(f->absorbed[0].size() + f->absorbed[1].size());

    auto drain_terms = [&](int index, int contribution) {
        for (auto &chunk : f->absorbed[index]) {
            terms.push_back({std::move(chunk), contribution});
        }
        f->absorbed[index].clear();
    };

    drain_terms(0, 1);
    drain_terms(1, -1);

    elist_comparison comp;
    std::sort(terms.begin(), terms.end(), [&](const absorbed_term& lhs, const absorbed_term& rhs) {
        return comp(lhs.chunk, rhs.chunk);
    });

    vector<absorbed_term> merged;
    merged.reserve(terms.size());
    for (auto &term : terms) {
        if (!merged.empty() && !comp(term.chunk, merged.back().chunk) && !comp(merged.back().chunk, term.chunk)) {
            merged.back().factor += term.factor;
            if (merged.back().factor == 0) {
                merged.pop_back();
            }
        }
        else 
        if (term.factor != 0) {
            merged.push_back({std::move(term.chunk), term.factor});
        }
    }

    mexce_charstream s;
    stringstream debug_ss;
    bool debug_first = true;

    if (fclass == 1) {
        bool have_value = false;
        bool constant_added = (ac_final == neutral);
        auto ensure_constant = [&]() {
            if (have_value && !constant_added && ac_final != neutral) {
                emit_apply_op_with_constant<0x00>(ev, s, ac_final);
                constant_added = true;
            }
        };

        if (ac_final != neutral) {
            debug_ss << double_to_pretty_string(ac_final);
            debug_first = false;
        }

        for (auto &term : merged) {
            int factor = term.factor;
            if (factor == 0) {
                continue;
            }

            string term_str = "(" + elist_to_string(term.chunk) + ")";
            if (factor > 0) {
                if (!debug_first) debug_ss << "+";
                if (factor != 1) debug_ss << factor << "*";
                debug_ss << term_str;
            }
            else {
                debug_ss << "-";
                if (factor != -1) debug_ss << abs(factor) << "*";
                debug_ss << term_str;
            }
            debug_first = false;

            if (have_value) {
                ensure_constant();
            }

            compile_elist(s, term.chunk.begin(), term.chunk.end());

            switch (factor) {
                case  1:  break;
                case  2:  s < 0xd8 < 0xc0; break;               // fadd st(0), st(0)
                case -2:  s < 0xd8 < 0xc0 < 0xd9 < 0xe0; break; // fadd st(0), st(0) + fchs
                case -1:  s < 0xd9 < 0xe0; break;               // fchs
                default:  emit_apply_op_with_constant<0x08>(ev, s, static_cast<double>(factor)); break;
            }

            if (!have_value) {
                have_value = true;
                if (ac_final != neutral) {
                    emit_apply_op_with_constant<0x00>(ev, s, ac_final);
                    constant_added = true;
                }
            }
            else {
                s < 0xde < 0xc1;                    // faddp       st(1), st
            }
        }

        if (!have_value) {
            emit_load_constant(ev, s, ac_final);
        }
        else {
            ensure_constant();
        }
    }
    else {
        bool have_value = false;
        bool constant_multiplied = (ac_final == neutral);
        auto ensure_constant = [&]() {
            if (have_value && !constant_multiplied && ac_final != neutral) {
                emit_apply_op_with_constant<0x08>(ev, s, ac_final);
                constant_multiplied = true;
            }
        };

        if (ac_final != neutral) {
            debug_ss << double_to_pretty_string(ac_final);
            debug_first = false;
        }

        for (auto &term : merged) {
            int factor = term.factor;
            if (factor == 0) {
                continue;
            }

            string term_str = "(" + elist_to_string(term.chunk) + ")";
            if (factor > 0) {
                if (!debug_first) debug_ss << "*";
                debug_ss << term_str;
                if (factor != 1) debug_ss << "^" << factor;
            }
            else {
                if (debug_first) debug_ss << "1"; // e.g. 1/x
                debug_ss << "/";
                debug_ss << term_str;
                if (factor != -1) debug_ss << "^" << abs(factor);
            }
            debug_first = false;

            if (have_value) {
                ensure_constant();
            }

            compile_elist(s, term.chunk.begin(), term.chunk.end());

            uint32_t abs_factor = static_cast<uint32_t>(abs(factor));
            if (abs_factor > 1U) {
                emit_integer_power_sequence(s, abs_factor);
            }

            if (factor < 0) {
                if (!have_value) {
                    s < 0xd9 < 0xe8;                // fld1
                    s < 0xde < 0xf1;                // fdivrp   st(1), st
                }
                else {
                    s < 0xde < 0xf9;                // fdivp    st(1), st
                }
            }
            else if (have_value) {
                s < 0xde < 0xc9;                    // fmulp   st(1), st
            }

            if (!have_value) {
                have_value = true;
                if (ac_final != neutral) {
                    emit_apply_op_with_constant<0x08>(ev, s, ac_final);
                    constant_multiplied = true;
                }
            }
        }

        if (!have_value) {
            emit_load_constant(ev, s, ac_final);
        }
        else {
            ensure_constant();
        }
    }

    if (debug_first) { // Loop didn't run or output anything, just constant
        if (debug_ss.str().empty()) debug_ss << double_to_pretty_string(ac_final);
    }

    string new_name = (fclass == 1) ? "add_sub_opt" : "mul_div_opt";
    uint8_t* cc = push_intermediate_code(ev, s.str());
    auto f_opt = make_shared<Function>(ev->m_next_element_id++, new_name, 0, 0, s.buf.size(), cc, nullptr);
    f_opt->debug_desc = "(" + debug_ss.str() + ")";
    f_opt->cse_store_suffix = f->cse_store_suffix;  // Preserve CSE store code

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


inline
string get_element_signature(const Element& e)
{
    stringstream ss;
                                               // Exact double bits (no aliasing UB)
    if      (e.type == Element_type::CCONST) { ss << "C:" << double_to_hex(e.c->value); }
    else if (e.type == Element_type::CVAR  ) { ss << "V:" << e.v->name; }
    else if (e.type == Element_type::CFUNC ) {
        ss << "F:" << e.f->name << "(";
        for (const auto& arg_it : e.f->args) {
            ss << get_element_signature(*arg_it) << ",";
        }
        ss << ")";
    }
    return ss.str();
}

inline
void run_cse(evaluator* ev, elist_t& elist)
{
    // 1. Identify common subexpressions via signature
    // Map Signature -> Vector of (Parent Iterator, Element Pointer) pairs?
    // No, elements are shared pointers inside the list.
    // We need the iterator to the root of the subtree in 'elist'.

    // But 'elist' is a list of roots (usually 1). The arguments are children.
    // We need to traverse the tree.

    // Map Signature -> List of occurrences.
    // Store ID alongside pointer to avoid dereferencing freed memory.
    // Store scan position to reliably reflect evaluation order (list order),
    // rather than relying on Element::id allocation order.
    // Track whether each occurrence is "absorbed" (will be merged into parent by ASMD).
    struct Occurrence {
        Element*  element;
        uint64_t  id;
        size_t    position;
        bool      absorbed;  // If true, cannot be the CSE source (but can be replaced)
    };
    map<string, vector<Occurrence> > occurrences;

    // Track erased element IDs to avoid accessing freed memory
    std::set<uint64_t> erased_ids;

    // Helper to determine operation class (1 = add/sub, 2 = mul/div, 0 = other)
    auto get_fclass = [](const string& name) -> int {
        return (name == "add" || name == "sub") ? 1 : (name == "mul" || name == "div") ? 2 : 0;
    };

    // Helper to check if a function will be absorbed by its parent (same class)
    auto will_be_absorbed = [&](const Element& e) -> bool {
        if (e.type != Element_type::CFUNC) return false;
        int fclass = get_fclass(e.f->name);
        // Not add/sub/mul/div, or no function parent.
        if (fclass == 0 || e.f->parent == elist.end() || e.f->parent->type != Element_type::CFUNC) {
            return false;
        }
        int pclass = get_fclass(e.f->parent->f->name);
        return pclass == fclass;  // Will be absorbed if same class
    };

    // Skip constant-only subtrees: they'll be folded by the constant-elimination pass and
    // should not be turned into CVAR temporaries (which would block further folding).
    std::function<bool(const Element&)> is_constant_subtree;
    is_constant_subtree = [&](const Element& e) -> bool {
        if (e.type == Element_type::CCONST) return true;
        if (e.type == Element_type::CVAR) return false;
        if (e.type != Element_type::CFUNC) return false;
        for (auto arg_it : e.f->args) {
            if (!is_constant_subtree(*arg_it)) {
                return false;
            }
        }
        return true;
    };

    // Collect all CFUNC elements with their signatures.
    // Include absorbed functions: they can't be the CSE source (since they get
    // restructured by ASMD), but they CAN be replaced with cse_temp loads.
    // This allows CSE when the same subexpression appears in both absorbed and
    // non-absorbed contexts.
    size_t scan_pos = 0;
    for (auto& root : elist) {
        if (root.type == Element_type::CFUNC && !is_constant_subtree(root)) {
            bool absorbed = will_be_absorbed(root);
            string sig = get_element_signature(root);
            occurrences[sig].push_back({&root, root.id, scan_pos, absorbed});
        }
        ++scan_pos;
    }

    // 2. Apply CSE
    for (auto& entry : occurrences) {
        if (entry.second.size() > 1) {
            // Sort by list position to find the first occurrence (evaluation order)
            std::sort(entry.second.begin(), entry.second.end(), [](const Occurrence& a, const Occurrence& b){
                return a.position < b.position;
            });

            // Re-check signatures: earlier CSE mutations can change subtrees, making
            // precomputed signature groups stale.
            std::vector<char> sig_match(entry.second.size(), 0);
            for (size_t i = 0; i < entry.second.size(); ++i) {
                const uint64_t elem_id = entry.second[i].id;
                if (erased_ids.find(elem_id) != erased_ids.end()) {
                    continue;
                }
                Element* elem = entry.second[i].element;
                if (elem && get_element_signature(*elem) == entry.first) {
                    sig_match[i] = 1;
                }
            }

            // Find the first NON-ABSORBED, non-erased, still-matching element.
            // The source must be non-absorbed so its cse_store_suffix is preserved
            // through the ASMD optimizer. Absorbed functions get restructured and
            // would lose the store code.
            Element* source = nullptr;
            size_t source_idx = SIZE_MAX;
            for (size_t i = 0; i < entry.second.size(); ++i) {
                if (sig_match[i] && !entry.second[i].absorbed) {
                    source = entry.second[i].element;
                    source_idx = i;
                    break;
                }
            }

            // If all occurrences are absorbed, we cannot apply CSE (no valid source).
            // Skip this signature group.
            if (source == nullptr) {
                continue;
            }

            // Count valid elements that can be replaced (all except the source).
            // This includes both absorbed and non-absorbed occurrences.
            size_t replaceable_count = 0;
            for (size_t i = 0; i < entry.second.size(); ++i) {
                if (sig_match[i] && i != source_idx) {
                    replaceable_count++;
                }
            }

            // Need at least 1 replaceable occurrence for CSE to be worthwhile
            if (replaceable_count < 1) {
                continue;
            }

            // Allocate a temporary storage slot
            ev->m_cse_temps.push_back(0.0);
            double* temp_addr = &ev->m_cse_temps.back();

            // Transform Source: Set CSE store suffix
            // Generate FST instruction to store result after function executes.
            // We store this as cse_store_suffix which survives optimizer transformations.
            mexce_charstream store_code;
#ifdef MEXCE_64
            store_code < 0x48 < 0xb8;
            store_code << (void*)temp_addr;
            store_code < 0xdd < 0x10;               // fst qword ptr [rax]
#else
            store_code < 0xdd < 0x15;               // fst qword ptr [addr]
            store_code << (void*)temp_addr;
#endif

            // Set the CSE store suffix on the source function.
            // This will be emitted after the function's code during compilation.
            source->f->cse_store_suffix = store_code.str();

            // Shared temp variable for all replacements in this group.
            const uint64_t temp_var_id = ev->m_next_element_id++;
            auto temp_var = make_shared<Variable>(temp_var_id, (volatile void*)temp_addr, "cse_temp_" + std::to_string(temp_var_id), M64FP);


            // Transform Others: Replace with Load (both absorbed and non-absorbed)
            for (size_t i = 0; i < entry.second.size(); ++i) {
                // Skip the source element
                if (i == source_idx) {
                    continue;
                }

                uint64_t other_id = entry.second[i].id;

                // Skip elements that were erased by a previous CSE pass (check stored ID)
                if (erased_ids.find(other_id) != erased_ids.end()) {
                    continue;
                }

                // Skip if the element no longer matches this signature (tree was mutated)
                if (!sig_match[i]) {
                    continue;
                }

                Element* other = entry.second[i].element;

                // Delete the subtree (arguments) of 'other' and track erased IDs
                std::function<void(Element*, elist_it_t)> delete_subtree = [&](Element* e, elist_it_t it) {
                    erased_ids.insert(e->id);
                    if (e->type == Element_type::CFUNC) {
                        for (auto arg_it : e->f->args) {
                            delete_subtree(&(*arg_it), arg_it);
                        }
                    }
                    elist.erase(it);
                };

                // Execute deletion of children (but not 'other' itself)
                if (other->type == Element_type::CFUNC) {
                    for (auto arg_it : other->f->args) {
                        delete_subtree(&(*arg_it), arg_it);
                    }
                }

                // Now replace 'other' (the root) with Load
                other->type = Element_type::CVAR;
                other->id   = temp_var_id;
                other->v = temp_var;
                other->c.reset();
                other->f.reset();
            }
        }
    }
}

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
std::string evaluator::get_optimized_expression() const {
    return impl::elist_to_string(m_elist);
}


inline
std::string evaluator::get_byte_representation() const {
    if (is_constant_expression || !evaluate_fptr || m_buffer_size == 0) {
        return std::string();
    }

    std::string result;
    result.reserve(m_buffer_size * 3);  // 2 hex chars + space per byte

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(evaluate_fptr);
    char hex_buf[4];

    for (size_t i = 0; i < m_buffer_size; ++i) {
        if (i > 0) {
            result += ' ';
        }
        std::snprintf(hex_buf, sizeof(hex_buf), "%02X", bytes[i]);
        result += hex_buf;
    }

    return result;
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
    m_cse_temps.clear(); // Clear previous CSE temps
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

    auto parse_infix_op = [&](size_t position, char& out_op, size_t& out_len) {
        if (e[position] == '*' && position + 1 < e.length() && e[position + 1] == '*') {
            out_op = '^';
            out_len = 2;
            return true;
        }
        if (is_operator(e[position])) {
            out_op = e[position];
            out_len = 1;
            return true;
        }
        return false;
    };

    for (; i < e.length(); i++) {
        char infix_op;
        size_t infix_op_len;
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
                if (parse_infix_op(i, infix_op, infix_op_len)) {
                    tokens.push_back(temp);
                    tokens.push_back(Token(get_infix_rank(infix_op), i, infix_op));
                    state = 4;
                    i += infix_op_len - 1;
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
                if (parse_infix_op(i, infix_op, infix_op_len)) {
                    temp.type = m_variables.find(temp.content) != m_variables.end() ? VARIABLE_NAME :
                                m_constants.find(temp.content) != m_constants.end() ? CONSTANT_NAME :
                        throw (mpe(string(temp.content) +
                            " is not a known constant or variable name", i));
                    tokens.push_back(temp);
                    tokens.push_back(Token(get_infix_rank(infix_op), i, infix_op));
                    state = 4;
                    i += infix_op_len - 1;
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
                if (parse_infix_op(i, infix_op, infix_op_len)) {
                    tokens.push_back(Token(get_infix_rank(infix_op), i, infix_op));
                    state = 4;
                    i += infix_op_len - 1;
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

    // Run Common Subexpression Elimination (CSE)
    // This must run before destructive optimizers (asmd, pow) to catch 
    // identical subtrees like div(2.2, y).
    run_cse(this, m_elist);

    // choose more suitable functions, where applicable
    for (auto y = m_elist.begin(); y != m_elist.end(); ) {
        auto y_next = next(y);
        if (y->type == Element_type::CFUNC) {
            auto f = y->f;

            // eliminate constants
            bool allow_constant_elimination = !f->force_not_constant;

            // Avoid prematurely folding nested powers: keep an inner pow(..) node intact so that
            // the pow optimizer can fold (a^b)^n -> a^(b*n) when applicable.
            if (allow_constant_elimination &&
                f->name == "pow" &&
                f->parent != m_elist.end() &&
                f->parent->type == Element_type::CFUNC)
            {
                auto pf = f->parent->f;
                if (pf->name == "pow" && f->parent_arg_index == 1) {
                    allow_constant_elimination = false;
                }
            }

            if (allow_constant_elimination) {
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
        0xdd, 0x1c, 0x24,                                           // fstp qword ptr [rsp]
        0xf2, 0x0f, 0x10, 0x04, 0x24,                               // movsd xmm0, qword ptr [rsp]
        0x48, 0x83, 0xc4, 0x20,                                     // add  rsp, 32
#endif
        0xc3                                                        // ret
    };

    mexce_charstream code_buffer;
    // Reduce vector growth reallocations during compilation (helps when compiling many
    // expressions and also when compiling small chunks during constant folding).
    {
        size_t n = 0;
        for (auto it = first; it != last; ++it) {
            ++n;
        }
        // Heuristic: a small fixed prologue plus ~2 dozen bytes per element.
        code_buffer.buf.reserve(96 + n * 24);
    }

#ifdef MEXCE_64
    // Reserve 32 bytes of stack space for ABI compliance (Win64) and scratch.
    code_buffer < 0x48 < 0x83 < 0xec < 0x20;        // sub  rsp, 32
#endif

    compile_elist(code_buffer, first, last);

#ifdef MEXCE_64
    // Stack space is released as part of the return sequence on x64.
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
