//
// Mini Expression Compiler/Evaluator
// ==================================
//
// 14.09.2003 [updated 21.8.2020]
// mexce.h
// Ioannis Makris
//
// mexce can compile and evaluate a mathematical expression at runtime.
// The generated machine code will mostly use the FPU.
//
// An example:
// -----------
//
// float   x = 0.0f;
// double  y = 0.1;
// int     z = 200;
// double  wv[200];
//
// mexce::evaluator eval;
// eval.bind(x, "x");
// eval.bind(y, "y");
// eval.bind(z, "x");   // This will return false and will have no effect.
//                      // That is because an "x" is already bound.
// eval.bind(z, "z");
//
// eval.assign_expression("0.3f+(-sin(2.33f+x-log((.3*PI+(88/y)/E),3.2+z)))/98");
//
// for (int i = 0; i < 200; i++, x-=0.1f, y+=0.212, z+=2) {
//     wv[i] = eval.evaluate(); // results will be different, depending on x, y, z
// }
//
// eval.unbind("x");    // releasing a committed variable which is contained in the
//                      // assigned expression will automatically invalidate the
//                      // expression.
//
// wv[0] = eval.evaluate(); // Now it will return 0
//

#ifndef MEXCE_INCLUDED
#define MEXCE_INCLUDED

#include <cassert>
#include <cstring>
#include <cinttypes>
#include <deque>
#include <exception>
#include <list>
#include <string>
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
    #include <sys/mman.h>
#endif


namespace mexce {


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
    std::string  m_message;
    size_t       m_position;
};


namespace impl {
     struct Numeral;
     struct Function;
}


class evaluator
{
public:
    evaluator();
    ~evaluator();

    template <typename T>
    bool bind(T& v, const std::string& s);

    bool unbind(const std::string&);
    bool assign_expression(std::string);

    double (* evaluate)();

    std::string get_c_expression() const { return m_c_expression; }

private:

#ifdef MEXCE_64
    volatile double             m_x64_return_var;
#endif

    size_t                      m_buffer_size;
    std::string                 m_expression;
    std::string                 m_c_expression;

    std::list<impl::Numeral >   m_numerals;
    std::list<impl::Function>   m_functions;

    std::list<impl::Numeral >::iterator find_numeral  (const std::string&);
    std::list<impl::Function>::iterator find_function (const std::string&);
    void neutralize();

};


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
    static auto const page_size = get_page_size();
    return (uint8_t*)VirtualAlloc(nullptr, page_size, MEM_COMMIT, PAGE_READWRITE);
#elif defined(__linux__)
    return (uint8_t*)mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}


inline
double (*lock_executable_buffer(uint8_t* buffer, size_t sz))()
{
#ifdef _WIN32
    DWORD dummy;
    VirtualProtect(buffer, sz, PAGE_EXECUTE_READ, &dummy);
#elif defined(__linux__)
    if (mprotect((void*) buffer, sz, PROT_EXEC) != 0) {
        buffer = 0;
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

enum Element_type
{
    CNUM,
    CCONST,
    CVAR,
    CFUNC
};

enum Token_type
{
    NULL_TOKEN,
    CONSTANT,
    VARIABLE,
    FUNCTION,
    ADDITIVE,
    MULTIPLICATIVE,
    RBRACKET,
    LBRACKET,
    COMMA,
    FRBRACKET,
    FLBRACKET,
    MINUSSIGN
};


struct Element
{
    uint8_t    element_type;
    Element(uint8_t ct): element_type(ct){}
};


struct Function: public Element
{
    uint8_t        num_args;
    uint8_t        stack_req;
    size_t         code_size;
    const char*    name;
    const char*    cname;
    uint8_t *      code;

    Function(const char* name, const char* cname, uint8_t args, uint8_t sreq, int size, uint8_t *code_buffer):
        Element   ( CFUNC ),
        num_args  ( args  ),
        stack_req ( sreq  ),
        code_size ( size  ),
        name      ( name  ),
        cname     ( cname ),
        code      ( code_buffer  ){}
};


inline Function Sin()
{
    static uint8_t code[] = {
        0xd9, 0xfe                                  // fsin
    };
    return Function("sin", "sin", 1, 0, sizeof(code), code);
}


inline Function Cos()
{
    static uint8_t code[] = {
        0xd9, 0xff                                  // fcos
    };
    return Function("cos", "cos", 1, 0, sizeof(code), code);
}


inline Function Tan()
{
    static uint8_t code[] = {
        0xd9, 0xf2,                                 // fptan
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function("tan", "tan", 1, 1, sizeof(code), code);
}


inline Function Abs()
{
    static uint8_t code[] = {
        0xd9, 0xe1                                  // fabs
    };
    return Function("abs", "_abs", 1, 0, sizeof(code), code);
}


inline Function Sfc()
{
    static uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("sfc", "_sfc", 1, 1, sizeof(code), code);
}


inline Function Expn()
{
    static uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function("expn", "_expn", 1, 1, sizeof(code), code);
}


inline Function Sign()
{
    static uint8_t code[]  =  {
        0xd9, 0xee,                                 // fldz
        0xdf, 0xf1,                                 // fcomip      st, st(1)
        0xdd, 0xd8,                                 // fstp        st(0)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xe0,                                 // fchs
        0xda, 0xc1,                                 // fcmovb      st, st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("sign", "_sign", 1, 1, sizeof(code), code);
}


inline Function Signp()
{
    static uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xee,                                 // fldz
        0xdb, 0xf2,                                 // fcomi       st, st(2)
        0xdd, 0xda,                                 // fstp        st(2)
        0xdb, 0xc1,                                 // fcmovnb     st, st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("signp", "_signp", 1, 2, sizeof(code), code);
}


inline Function Sqrt()
{
    static uint8_t code[] = {
        0xd9, 0xfa                                  // fsqrt
    };
    return Function("sqrt", "sqrt", 1, 0, sizeof(code), code);
}


inline Function Pow()
{
    static uint8_t code[]  =  {
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xe4,                                 // ftst
        0x9b,                                       // wait
        0xdf, 0xe0,                                 // fnstsw      ax
        0x9e,                                       // sahf
        0x74, 0x14,                                 // je          00f8002a
        0xd9, 0xe1,                                 // fabs
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc1,                                 // fld         st(1)
        0xd9, 0xf8,                                 // fprem
        0xd9, 0xf0,                                 // f2xm1
        0xde, 0xc1,                                 // faddp       st(1), st
        0xd9, 0xfd,                                 // fscale
        0x77, 0x02,                                 // ja          00f8002a
        0xd9, 0xe0,                                 // fchs
        0xdd, 0xd9,                                 // fstp        st(1)
    };
    return Function("pow", "pow", 2, 1, sizeof(code), code);
}


inline Function Log()
{
    static uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function("log", "_log", 2, 1, sizeof(code), code);
}


inline Function Log2()
{
    static uint8_t code[] = {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1                                  // fyl2x
    };
    return Function("log2", "_log2", 1, 0, sizeof(code), code);
}


inline Function Ylog2()
{
    static uint8_t code[] = {
        0xd9, 0xf1                                  // fyl2x
    };
    return Function("ylog2", "_ylog2", 2, 0, sizeof(code), code);
}


inline Function Max()
{
    static uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("max", "_max", 2, 0, sizeof(code), code);
}


inline Function Min()
{
    static uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xd9, 0xc9,                                 // fxch        st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("min", "_min", 2, 0, sizeof(code), code);
}


inline Function Floor()
{
    static uint8_t code[] = {
        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x06,   // mov         word ptr [esp-4], 67fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function("floor", "floor", 1, 0, sizeof(code), code);
}


inline Function Ceil()
{
    static uint8_t code[] = {
        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x0a,   // mov         word ptr [esp-4], a7fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function("ceil", "ceil", 1, 0, sizeof(code), code);
}


inline Function Round()
{
    static uint8_t code[] = {

        // NOTE: In this case, saving/restoring the control word is most likely redundant.

        0x66, 0xc7, 0x44, 0x24, 0xfc, 0x7f, 0x02,   // mov         word ptr [esp-4], 27fh
        0xd9, 0x7c, 0x24, 0xfe,                     // fnstcw      word ptr [esp-2]
        0xd9, 0x6c, 0x24, 0xfc,                     // fldcw       word ptr [esp-4]
        0xd9, 0xfc,                                 // frndint
        0xd9, 0x6c, 0x24, 0xfe                      // fldcw       word ptr [esp-2]
    };
    return Function("round", "_round", 1, 0, sizeof(code), code);
}


inline Function Int()
{
    static uint8_t code[] = {
        0xd9, 0xfc                                  // frndint
    };
    return Function("int", "int", 1, 0, sizeof(code), code);
}


inline Function Mod()
{
    static uint8_t code[] = {
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf8,                                 // fprem
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("mod", "fmod", 2, 0, sizeof(code), code);
}


inline Function Bnd()
{
    static uint8_t code[] = {
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
    return Function("bnd", "_bnd", 2, 2, sizeof(code), code);
}


inline Function Add()
{
    static uint8_t code[] = {
        0xde, 0xc1                                  // faddp       st(1), st
    };
    return Function("+", "", 2, 0, sizeof(code), code);
}


inline Function Sub()
{
    static uint8_t code[] = {
        0xde, 0xe9                                  // fsubp       st(1), st
    };
    return Function("-", "", 2, 0, sizeof(code), code);
}


inline Function Neg()
{
    static uint8_t code[] = {
        0xd9, 0xe0                                  // fchs
    };
    return Function("#", "", 1, 0, sizeof(code), code);
}


inline Function Mul()
{
    static uint8_t code[] = {
        0xde, 0xc9                                  // fmulp       st(1), st
    };
    return Function("*", "", 2, 0, sizeof(code), code);
}


inline Function Div()
{
    static uint8_t code[] = {
        0xde, 0xf9                                  // fdivp       st(1), st
    };
    return Function("/", "", 2, 0, sizeof(code), code);
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

    static uint8_t code[] = {
        0xd9, 0xe8,                                 // fld1
        0xdc, 0xf1,                                 // fdivr       st(1), st
        0xdc, 0xe9,                                 // fsub        st(1), st
        0xdc, 0xe9,                                 // fsub        st(1), st
        0xd8, 0xe2,                                 // fsub        st, st(2)
        0xd8, 0xe2,                                 // fsub        st, st(2)
        0xde, 0xc9,                                 // fmulp       st(1), st
        0xd9, 0xe8,                                 // fld1                   }
        0xd9, 0xe0,                                 // fchs                   }
        0xd9, 0xe8,                                 // fld1                   } <= these instructions will
        0xd9, 0xfd,                                 // fscale                 }    create a 2.0 in st(0)
        0xdd, 0xd9,                                 // fstp        st(1)      }
        0xdf, 0xf2,                                 // fcomip      st, st(2)
        0x0f, 0x82, 0x0b, 0x00, 0x00, 0x00,         // jb          00a80037
        0xd9, 0xe8,                                 // fld1
        0xde, 0xc1,                                 // faddp       st(1), st
        0xde, 0xf9,                                 // fdivp       st(1), st
        0xe9, 0x08, 0x00, 0x00, 0x00,               // jmp         00a8003f
        0xdc, 0xe1,                                 // fsubr       st(1), st
        0xd9, 0xe8,                                 // fld1
        0xde, 0xe9,                                 // fsubp       st(1), st
        0xde, 0xf9                                  // fdivp       st(1), st
    };
    return Function("gain", "_gain", 2, 1, sizeof(code), code);
}


inline Function Bias()
{
    //                         x
    // bias(x, a) = -----------------------    for x, a in [0, 1]
    //              (1 / a - 2) (1 - x) + 1

    static uint8_t code[] = {
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
    return Function("bias", "_bias", 2, 1, sizeof(code), code);
}


inline ::std::list<Function>& functions()
{
    static Function f[] = {
        Sin(), Cos(), Tan(), Abs(), Sign(), Signp(), Expn(), Sfc(), Sqrt(), Pow(),
        Log(), Log2(), Ylog2(), Max(), Min(), Floor(), Ceil(), Round(), Int(), Mod(),
        Bnd(), Add(), Sub(), Neg(), Mul(), Div(), Bias(), Gain()
    };
    static ::std::list<Function> ret(f, f + sizeof(f) / sizeof(*f));
    return ret;
}


struct Numeral: public Element
{
    void*          address;
    uint8_t        numeric_data_type;
    bool           referenced;
    double         referenced_variable;
    std::string    name;

    Numeral(void *address, uint8_t numeric_data_type, uint8_t element_type, std::string name, double constant_value = 0.0):
        Element               ( element_type      ),
        address               ( address           ),
        numeric_data_type     ( numeric_data_type ),
        referenced            ( false             ),
        referenced_variable   ( constant_value    ),
        name                  ( name              ) {}

    Numeral(const Numeral &rhs):
        Element               ( rhs.element_type          ),
        address               ( rhs.address               ),
        numeric_data_type     ( rhs.numeric_data_type     ),
        referenced            ( rhs.referenced            ),
        referenced_variable   ( rhs.referenced_variable   ),
        name                  ( rhs.name                  )
    {
        if (element_type == CCONST)
            address = &referenced_variable;
    }
};


struct Constant: public Numeral
{
    Constant(std::string num, std::string name = ""):
        Numeral(&referenced_variable, M64FP, CCONST, name, atof(num.data())){}
};


struct Variable: public Numeral
{
    Variable(void * addr, std::string name, int numFormat):
        Numeral(addr, numFormat, CVAR, name){}
};


struct Token
{
    int            type;
    size_t         position;
    std::string    content;
    Token():
        type      ( 0 ),
        position  ( 0 ) {}
    Token(int type, size_t position, char content):
        type      ( type                      ),
        position  ( position                  ),
        content   ( std::string() + content   ) {}
};


template <typename> inline Numeric_data_type get_ndt()  { assert(false); }
template <> inline Numeric_data_type get_ndt<double >() { return M64FP;  }
template <> inline Numeric_data_type get_ndt<float  >() { return M32FP;  }
template <> inline Numeric_data_type get_ndt<int16_t>() { return M16INT; }
template <> inline Numeric_data_type get_ndt<int32_t>() { return M32INT; }
template <> inline Numeric_data_type get_ndt<int64_t>() { return M64INT; }


} // mexce_impl


inline
evaluator::evaluator():
    m_buffer_size(0)
{
    neutralize();
    m_numerals.push_back(impl::Constant("3.141592653589793238462643383", "PI"));
    m_numerals.push_back(impl::Constant("2.718281828459045235360287471", "E" ));
}


inline
evaluator::~evaluator()
{
    impl::free_executable_buffer(evaluate, m_buffer_size);
}


template <typename T>
bool evaluator::bind(T& v, const std::string& s)
{
    if ((find_numeral(s) == m_numerals.end()) &&
        (find_function(s) == impl::functions().end()))
    {
        m_numerals.push_back(impl::Variable(&v, s, impl::get_ndt<T>() ));
        return true;
    }
    return false;
}


inline
std::list<impl::Numeral>::iterator evaluator::find_numeral(const std::string& s)
{
    std::list<impl::Numeral>::iterator i = m_numerals.begin();
    for (; i != m_numerals.end(); i++)
        if (i->name == s)
            return i;
    return i;
}


inline
std::list<impl::Function>::iterator evaluator::find_function(const std::string& s)
{
    std::list<impl::Function>::iterator i = impl::functions().begin();
    for (; i != impl::functions().end(); i++)
        if (i->name == s)
            return i;
    return i;
}


inline
bool evaluator::unbind(const std::string& s)
{
    if (s.length() == 0)
         return false;

    std::list<impl::Numeral>::iterator pos = find_numeral(s);
    if (pos != m_numerals.end()) {
        if (pos->element_type != impl::CVAR)
            return false;
        if (pos->referenced) {
            neutralize();
        }
        m_numerals.erase(pos);
        return true;
    }
    return false;
}


inline
void evaluator::neutralize()
{
    impl::free_executable_buffer(evaluate, m_buffer_size);
    static uint8_t return0[] = { 0xd9, 0xee, 0xc3 };
    m_buffer_size = sizeof(return0);
    auto buffer = impl::get_executable_buffer(m_buffer_size);
    memcpy(buffer, return0, m_buffer_size);
    evaluate = impl::lock_executable_buffer(buffer, m_buffer_size);
}


inline
bool evaluator::assign_expression(std::string e)
{
    using namespace impl;

    using std::deque;
    using std::list;
    using std::string;
    using std::vector;
    using std::pair;

    static char numbers[] = "0123456789", operators[] = "+-*/",
    letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    deque<Token> tokens;

    m_c_expression = "";

    while (m_numerals.back().name == "")
        m_numerals.pop_back();

    list<Numeral>::iterator x = m_numerals.begin();
    for (; x != m_numerals.end(); x++)
        x->referenced = false;

    if (e.length() == 0){
        neutralize();
        return true;
    }

    e += ' ';

    //stage 1: checking expression syntax
    Token temp;
    vector< pair<int, int> > bdarray(1);

    list<Numeral >::iterator i_num;
    list<Function>::iterator i_fnc;
    int state = 0;
    size_t i = 0;
    int fbrackets = 0;
    for (; i < e.length(); i++) {
        switch(state) {
            case 0: //start of expression
                if (e[i] == '+')
                    break;
                if (e[i] == '-') {
                    tokens.push_back(Token(MINUSSIGN, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    if (bdarray.back().first != 0)
                        throw (mexce_parsing_exception("Expected an expression", i));
                    if (bdarray.back().second != 0)
                        throw (mexce_parsing_exception("Expected more arguments", i));
                    tokens.push_back(Token(FRBRACKET, i, ')'));
                    fbrackets--;
                    bdarray.pop_back();
                    state = 5;
                    break;
                }
            case 4: //just read an operator
                if (e[i] == ' ')
                    break;
                if (strchr(numbers, e[i]) != NULL) {
                    temp = Token(CONSTANT, i, e[i]);
                    state = 1;
                    break;
                }
                if (e[i] == '.') {
                    temp = Token(CONSTANT, i, e[i]);
                    state = 2;
                    break;
                }
                if (strchr(letters, e[i]) != NULL) {
                    temp = Token(0, i, e[i]);
                    state = 3;
                    break;
                }
                if (e[i] == '(') {
                    tokens.push_back(Token(LBRACKET, i, '('));
                    bdarray.back().first++;
                    state = 0;
                    break;
                }
                else
                    throw (mexce_parsing_exception((string("\"")+e[i])+"\" not expected", i));
            case 1: //currently reading a constant
                if (e[i] == '.') {
                    temp.content += e[i];
                    state = 2;
                    break;
                }
                goto case1or2;
            case 2: //currently reading a constant, found dot
                if (e[i] == 'f') {
                    temp.type = CONSTANT;
                    tokens.push_back(temp);
                    state = 5;
                    break;
                }
            case1or2:
                if (e[i] == ' ') {
                    tokens.push_back(temp);
                    state = 5;
                    break;
                }
                if (strchr(numbers, e[i]) != NULL) {
                    temp.content += e[i];
                    break;
                }
                if (e[i] == ')') {
                    tokens.push_back(temp);
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RBRACKET, i, ')'));
                        bdarray.back().first--;
                    }
                    else {
                        if (fbrackets <= 0)
                            throw (mexce_parsing_exception("\")\" not expected", i));
                        if (bdarray.back().second != 1)
                            throw (mexce_parsing_exception("Expected more arguments", i));
                        tokens.push_back(Token(FRBRACKET, i, ')'));
                        fbrackets--;
                        bdarray.pop_back();
                    }
                    state = 5;
                    break;
                }
                if (strchr(operators, e[i]) != NULL) {
                    tokens.push_back(temp);
                    tokens.push_back(Token(((e[i]=='+')||(e[i]=='-'))?
                        ADDITIVE:MULTIPLICATIVE, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ',') {
                    tokens.push_back(temp);
                    if (bdarray.back().first != 0)
                        throw (mexce_parsing_exception("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mexce_parsing_exception("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else
                    throw (mexce_parsing_exception((string("\"")+e[i])+"\" not expected", i));
            case 3: //currently reading alphanumeric
                if (e[i] == ' ') {
                    if ((i_num = find_numeral(temp.content)) != m_numerals.end()) {
                        temp.type = i_num->element_type;
                        tokens.push_back(temp);
                        state = 5;
                    }
                    else
                    if ((i_fnc = find_function(temp.content)) != functions().end()) {
                        temp.type = FUNCTION;
                        tokens.push_back(temp);
                        tokens.push_back(Token(FLBRACKET, i, '('));
                        bdarray.push_back(std::make_pair(0, i_fnc->num_args));
                        fbrackets++;
                        state = 6;
                    }
                    else
                        throw (mexce_parsing_exception(string(temp.content)+" is not a "
                            "recognized variable or function name", i));
                    break;
                }
                if ((strchr(letters, e[i]) != NULL) ||
                    (strchr(numbers, e[i]) != NULL)) {
                    temp.content += e[i];
                    break;
                }
                if (e[i] == ')') {
                    if ((i_num = find_numeral(temp.content)) == m_numerals.end())
                        throw (mexce_parsing_exception(string(temp.content)+" is not a "
                            "recognized variable name", i));
                    temp.type = i_num->element_type;
                    tokens.push_back(temp);
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RBRACKET, i, ')'));
                        bdarray.back().first--;
                    }
                    else
                    if (fbrackets > 0) {
                        if (bdarray.back().second != 1)
                            throw (mexce_parsing_exception("Expected more arguments", i));
                        tokens.push_back(Token(FRBRACKET, i, ')'));
                        fbrackets--;
                        bdarray.pop_back();
                    }
                    else
                        throw (mexce_parsing_exception("\")\" not expected", i));
                    state = 5;
                    break;
                }
                if (e[i] == '(') {
                    if ((i_fnc = find_function(temp.content)) == functions().end())
                        throw (mexce_parsing_exception(string(temp.content)+" is not a "
                            "recognized function name", i));
                    temp.type = FUNCTION;
                    tokens.push_back(temp);
                    tokens.push_back(Token(FLBRACKET, i, '('));
                    bdarray.push_back(std::make_pair(0, i_fnc->num_args));
                    fbrackets++;
                    state = 0;
                    break;
                }
                if (strchr(operators, e[i]) != NULL) {
                    if ((i_num = find_numeral(temp.content)) == m_numerals.end())
                        throw (mexce_parsing_exception(string(temp.content)+" is not a "
                            "recognized variable name", i));
                    temp.type = i_num->element_type;
                    tokens.push_back(temp);
                    tokens.push_back(Token(((e[i]=='+')||(e[i]=='-'))?
                        ADDITIVE:MULTIPLICATIVE, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ',') {
                    if ((i_num = find_numeral(temp.content)) == m_numerals.end())
                        throw (mexce_parsing_exception(string(temp.content)+" is not a "
                            "recognized variable name", i));
                    temp.type = i_num->element_type;
                    tokens.push_back(temp);
                    if (bdarray.back().first != 0)
                        throw (mexce_parsing_exception("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mexce_parsing_exception("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else
                    throw (mexce_parsing_exception((string("\"")+e[i])+"\" not expected", i));
            case 5: //just read an expression (constant/variable/right bracket)
                if (e[i] == ' ')
                    break;
                if (strchr(operators, e[i]) != NULL) {
                    tokens.push_back(Token(((e[i]=='+')||(e[i]=='-'))?
                        ADDITIVE:MULTIPLICATIVE, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RBRACKET, i, ')'));
                        bdarray.back().first--;
                    }
                    else
                    if (fbrackets > 0) {
                        if (bdarray.back().second != 1)
                            throw (mexce_parsing_exception("Expected more arguments", i));
                        tokens.push_back(Token(FRBRACKET, i, ')'));
                        fbrackets--;
                        bdarray.pop_back();
                    }
                    else {
                        throw (mexce_parsing_exception("\")\" not expected", i));
                    }
                    state = 5;
                    break;
                }
                if (e[i] == ',') {
                    if (bdarray.back().first != 0)
                        throw (mexce_parsing_exception("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mexce_parsing_exception("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else {
                    throw (mexce_parsing_exception((string("\"")+e[i])+"\" not expected", i));
                }
            case 6: //just read a function name
                if (e[i] == '(') {
                    state = 0;
                    break;
                }
                else {
                    throw (mexce_parsing_exception("Expected a \"(\"", i));
                }
        }
    }
    if ((bdarray.back().first > 0) || (fbrackets > 0)) {
        throw (mexce_parsing_exception("Expected a \")\"", --i));
    }
    if (state != 5) {
        throw (mexce_parsing_exception("Unexpected end of expression", --i));
    }

    //stage 2: transform expression to postfix
    deque<Token> postfix;
    vector<Token> tstack;
    while (!tokens.empty()) {
        temp = tokens.front();
        tokens.pop_front();
        switch (temp.type) {
            case CONSTANT:
            case VARIABLE:
                postfix.push_back(temp);
                m_c_expression += temp.content;
                break;
            case ADDITIVE:
                while(!tstack.empty()) {
                    if ((tstack.back().type == ADDITIVE        ) ||
                         (tstack.back().type == MINUSSIGN      ) ||
                         (tstack.back().type == MULTIPLICATIVE)) {
                        postfix.push_back(tstack.back());
                        tstack.pop_back();
                    }
                    else {
                        break;
                    }
                }
            case LBRACKET:
            case MINUSSIGN:
                tstack.push_back(temp);
                m_c_expression += temp.content;
                break;
            case MULTIPLICATIVE:
                while(!tstack.empty()) {
                    if (tstack.back().type == MULTIPLICATIVE) {
                        postfix.push_back(tstack.back());
                        tstack.pop_back();
                    }
                    else {
                        break;
                    }
                }
                tstack.push_back(temp);
                m_c_expression += temp.content;
                break;
            case FUNCTION:
                tstack.push_back(temp);
                m_c_expression += find_function(temp.content)->cname;
                break;
            case RBRACKET:
                while(tstack.back().type != LBRACKET) {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                postfix.push_back(tstack.back());
                tstack.pop_back();
                m_c_expression += temp.content;
                break;
            case FRBRACKET:
                do {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                while(postfix.back().type != FUNCTION);
                m_c_expression += temp.content;
                break;
            case COMMA:
                while(tstack.back().type != FUNCTION) {
                    postfix.push_back(tstack.back());
                    tstack.pop_back();
                }
                m_c_expression += temp.content;
                break;
            case FLBRACKET:
                m_c_expression += temp.content;
                break;
            default:
                throw(mexce_parsing_exception("Something went terribly wrong...", 0));
        }
    }
    while(!tstack.empty()) {
        postfix.push_back(tstack.back());
        tstack.pop_back();
    }

    //stage 3: convert "Token" expression primitives to "Element *"
    list<Element *> elist;
    while (!postfix.empty()) {
        temp = postfix.front();
        postfix.pop_front();
        switch (temp.type) {
            case ADDITIVE:
                elist.push_back(&(*find_function( temp.content == "+" ? "+" : "-" )));
                break;
            case MULTIPLICATIVE:
                elist.push_back(&(*find_function( temp.content == "*" ? "*" : "/" )));
                break;
            case MINUSSIGN:
                elist.push_back(&(*find_function("#")));
                break;
            case FUNCTION:
                elist.push_back(&(*find_function(temp.content)));
                break;
            case CONSTANT: {
                list<Numeral>::iterator x;
                if ((x = find_numeral(temp.content)) != m_numerals.end()) {
                    x->referenced = true;
                    elist.push_back(&(*x));
                }
                else {
                    m_numerals.push_back(Constant(temp.content));
                    elist.push_back(&(m_numerals.back()));
                }
                break;
            }
            case VARIABLE: {
                list<Numeral>::iterator x = find_numeral(temp.content);
                assert(x != m_numerals.end());
                x->referenced = true;
                elist.push_back(&(*find_numeral(temp.content)));
                break;
            }
        }
    }


    //stage 4: calculate code size
    list<Element *>::iterator y = elist.begin();
    size_t code_buffer_size = 0;
    for (; y != elist.end(); y++) {
        if (((*y)->element_type == CVAR) ||
            ((*y)->element_type == CCONST))
#ifdef MEXCE_64
            code_buffer_size += 12;   // mov rax, qword ptr (10 bytes) + fld [rax] (2 bytes)
#else
            code_buffer_size += 6;    // fld dword ptr (6 bytes)
#endif
        else
            code_buffer_size += ((Function *)*y)->code_size;
    }

    const static uint8_t return_sequence[] = {
#ifdef MEXCE_64
        // Right before the function returns, in 32-bit x86, the result is in
        // st(0), where it is expected to be. There is nothing further to do there
        // other than return.
        // In x64 however, the result is expected to be in xmm0, thus we should
        // move it there and pop the FPU stack. To achieve that, we  store the
        // result to memory and then load it to xmm0, which requires a temporary.
        // This code is used at the very end (see below), but its size must be known here.

        //  load return address to rax (last 8 bytes - address is uninitialized)
        0x48, 0xb8, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, // mov         rax, cdcdcdcdcdcdcdcdh

        // store the return value
        0xdd, 0x18,                                                 // fstp        qword ptr [rax]

        // load from the return value to xmm0
        0xF3, 0x0F, 0x7E, 0x00,                                     // movq        xmm0, mmword ptr [rax]
        0x58,                                                       // pop rax
#endif
        0xc3                                                        // return
    };

    code_buffer_size += sizeof(return_sequence);

#ifdef MEXCE_64
    code_buffer_size += 1; //this is the initiation sequence, only applicable to x64, for pushing rax (1 byte)
#endif

    auto code_buffer = get_executable_buffer(code_buffer_size);

    //stage 5: generate executable code
    y = elist.begin();
    size_t idx = 0;

#ifdef MEXCE_64
    // On x64 we are using rax to fetch/store addresses
    code_buffer[0] = 0x50; // push rax
    idx++;
#endif

    for (; y != elist.end(); y++) {
        if (((*y)->element_type == CVAR) ||
             ((*y)->element_type == CCONST)){
            Numeral * tn = (Numeral *) *y;

#ifdef MEXCE_64
            *((uint16_t*)(code_buffer+idx)) = 0xb848;   // move input address to rax (only the opcode)
            memcpy(code_buffer + idx + 2, &(tn->address), 8);
            idx += 10;                                  // 2 for the opcode, 8 for the address
#endif

            switch (tn->numeric_data_type) {
#ifdef MEXCE_64
                // On x64, variable addresses are already supplied in rax.
                case M32FP:   *((uint16_t*)(code_buffer+idx)) = 0x00d9; break;
                case M64FP:   *((uint16_t*)(code_buffer+idx)) = 0x00dd; break;
                case M16INT:  *((uint16_t*)(code_buffer+idx)) = 0x00df; break;
                case M32INT:  *((uint16_t*)(code_buffer+idx)) = 0x00db; break;
                case M64INT:  *((uint16_t*)(code_buffer+idx)) = 0x28df; break;
#else
                // On 32-bit x86, variable addresses are explicitly specified.
                case M32FP:   *((uint16_t*)(code_buffer+idx)) = 0x05d9; break;
                case M64FP:   *((uint16_t*)(code_buffer+idx)) = 0x05dd; break;
                case M16INT:  *((uint16_t*)(code_buffer+idx)) = 0x05df; break;
                case M32INT:  *((uint16_t*)(code_buffer+idx)) = 0x05db; break;
                case M64INT:  *((uint16_t*)(code_buffer+idx)) = 0x2ddf; break;
#endif
            }
            idx += 2;

#ifndef MEXCE_64
            memcpy(code_buffer + idx, &(tn->address), 4);
            idx += 4;
#endif
        }
        else {
            Function * tf = (Function *)*y;
            memcpy(code_buffer+idx, tf->code, tf->code_size);
            idx += tf->code_size;
        }
    }

    // copy the return sequence
    memcpy(code_buffer+idx, return_sequence, sizeof(return_sequence));

#ifdef MEXCE_64
    // load the intermediate variable's address to rax
    *((uint64_t*)(code_buffer+idx+2)) = (uint64_t)&m_x64_return_var;
#endif

    idx +=sizeof(return_sequence);

    evaluate = lock_executable_buffer(code_buffer, code_buffer_size);

    return true;
}

}

#endif
