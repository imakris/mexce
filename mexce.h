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
// eval.assign_expression("0.3+(-sin(2.33+x-log((.3*pi+(88/y)/e),3.2+z)))/98");
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
#include <cinttypes>
#include <cstring>
#include <deque>
#include <exception>
#include <list>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cmath>


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
    //#include <unistd.h>
    #include <sys/mman.h>
#endif


// adjust, if functions with more than 2 arguments are introduced
#ifndef MEXCE_NUM_FUNCTION_ARGS_MAX
#define MEXCE_NUM_FUNCTION_ARGS_MAX 2
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
     struct Value;
     struct Constant;
     struct Variable;
     struct Function;
     struct Element;

     using elist_t = std::list<std::shared_ptr<impl::Element> >;
     using elist_it_t = elist_t::iterator;

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

private:

#ifdef MEXCE_64
    volatile double             m_x64_return_var;
#endif

    size_t                      m_buffer_size;
    std::string                 m_expression;

    using constant_map_t = std::map<std::string, std::shared_ptr<impl::Constant> >;
    using variable_map_t = std::map<std::string, std::shared_ptr<impl::Variable> >;
    constant_map_t   m_literals;             // e.g. '5.88'
    constant_map_t   m_constants;            // e.g. pi (for now it's only pi and e)
    variable_map_t   m_variables;

    std::list<std::shared_ptr<impl::Constant>>   m_intermediate_constants;

    void compile_elist(impl::elist_it_t first, impl::elist_it_t last);

public:
    std::list< std::string >    m_intermediate;

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
    INFIX_4,            // infix operator, with priority 4 ( '<' )
    RIGHT_PARENTHESIS,
    LEFT_PARENTHESIS,
    COMMA,
    FUNCTION_RIGHT_PARENTHESIS,
    FUNCTION_LEFT_PARENTHESIS,
    UNARY
};


using std::list;
using std::string;


struct Element
{
    uint8_t element_type;
    Element(uint8_t ct): element_type(ct) {}
};


struct Value: public Element
{
    volatile void* address;
    uint8_t        numeric_data_type;
    string         name;

    Value(volatile void *address, uint8_t numeric_data_type, uint8_t element_type, string name):
        Element               ( element_type      ),
        address               ( address           ),
        numeric_data_type     ( numeric_data_type ),
        name                  ( name              ) {}
};


struct Constant: public Value
{
    Constant(string num, string name = ""):
        Value( (volatile void *) &internal_constant, M64FP, CCONST, name),
        internal_constant(atof(num.data()))
    {}

    Constant(double num):
        Value( (volatile void *) &internal_constant, M64FP, CCONST, ""),
        internal_constant(num)
    {}

    Constant(const Constant& rhs):
        Value(rhs),
        internal_constant(rhs.internal_constant)
    {
        address = (void*)&internal_constant;
    }

    double get_data_as_double() const {
        switch (this->numeric_data_type) {
            case M16INT: return (double)*((int16_t*)address);
            case M32INT: return (double)*((int32_t*)address);
            case M64INT: return (double)*((int64_t*)address);
            case M32FP:  return (double)*((float*)address);
            case M64FP:  return         *((double*)address);
            default: assert(false);
        }
    }

private:
    const double   internal_constant;
};


struct Variable: public Value
{
    bool referenced;

    Variable(volatile void * addr, string name, int numFormat):
        Value(addr, numFormat, CVAR, name), referenced(false)
    {}
};


struct Function: public Element
{
    using optimizer_t = std::shared_ptr<Function> (*)(std::shared_ptr<Function>, evaluator*, elist_t*);

    uint8_t         stack_req;
    size_t          code_size;
    const char*     name;
    size_t          num_args;
    elist_it_t      args[MEXCE_NUM_FUNCTION_ARGS_MAX];
    uint8_t*        code;
    optimizer_t     optimizer;
    bool            var_ref;   // true if the function's code has a direct reference
                               // to an external variable. This may only happen if the
                               // function is optimized.

    Function(
        const char* name,
        uint8_t     num_args,
        uint8_t     sreq,
        size_t      size,
        uint8_t    *code_buffer,
        optimizer_t optimizer = 0,
        bool        var_ref = false)
    :
        Element   ( CFUNC       ),
        stack_req ( sreq        ),
        code_size ( size        ),
        name      ( name        ),
        num_args  ( num_args    ),
        code      ( code_buffer ),
        optimizer ( optimizer   ),
        var_ref   ( var_ref     ) {}
};


struct mexce_charstream { std::stringstream s; };

template<typename T>
mexce_charstream& operator << (mexce_charstream &s, T data) {
    s.s.write((char*)&data, sizeof(T));
    return s;
}

inline
mexce_charstream& operator < (mexce_charstream &s, int v) {
    char ch = (char)v;
    s.s.write(&ch, 1);
    return s;
}


inline
Token_type get_infix_rank(char infix_op)
{
    switch (infix_op) {
    case '<':
        return INFIX_4;
    case '+':
    case '-':
        return INFIX_3;
    case '*':
    case '/':
        return INFIX_2;
    case '^':
        return INFIX_1;
    }

    assert(false);
    return UNDEFINED_TOKEN_TYPE;
}


inline Function Sin()
{
    static uint8_t code[] = {
        0xd9, 0xfe                                  // fsin
    };
    return Function("sin", 1, 0, sizeof(code), code);
}


inline Function Cos()
{
    static uint8_t code[] = {
        0xd9, 0xff                                  // fcos
    };
    return Function("cos", 1, 0, sizeof(code), code);
}


inline Function Tan()
{
    static uint8_t code[] = {
        0xd9, 0xf2,                                 // fptan
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function("tan", 1, 1, sizeof(code), code);
}


inline Function Abs()
{
    static uint8_t code[] = {
        0xd9, 0xe1                                  // fabs
    };
    return Function("abs", 1, 0, sizeof(code), code);
}


inline Function Sfc()
{
    static uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("sfc", 1, 1, sizeof(code), code);
}


inline Function Expn()
{
    static uint8_t code[] = {
        0xd9, 0xf4,                                 // fxtract
        0xdd, 0xd8                                  // fstp        st(0)
    };
    return Function("expn", 1, 1, sizeof(code), code);
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
    return Function("sign", 1, 1, sizeof(code), code);
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
    return Function("signp", 1, 2, sizeof(code), code);
}


inline Function Sqrt()
{
    static uint8_t code[] = {
        0xd9, 0xfa                                  // fsqrt
    };
    return Function("sqrt", 1, 0, sizeof(code), code);
}


inline
std::shared_ptr<Function> pow_optimizer(std::shared_ptr<Function> f, evaluator* ev, elist_t* elist)
{
    // If one of the two arguments is CCONST or CVAR (i.e. loaded from memory),
    // it will be applied directly, thus dropping FPU stack requirements by 1.

    if ((*f->args[0])->element_type == CCONST) {
        auto v = std::static_pointer_cast<Constant>(*f->args[0]);

        double v_d = v->get_data_as_double();
        double r_d = round(v_d);
        double a_d = abs(v_d);
        if (r_d == v_d && a_d <= 32.0) {
            mexce_charstream s;

            if (v_d == 0.0) {
                s < 0xdd < 0xd8     // fstp st(0)
                  < 0xd9 < 0xe8;    // fld1
            }
            else
            if (v_d == 1.0) {
                // do nothing
            }
            else
            if (v_d == 2.0) {
                s < 0xdc < 0xc8;    // fmul st(0)
            }
            else
            if (v_d == 3.0) {
                s < 0xd9 < 0xc0     // fld  st(0)
                  < 0xdc < 0xc8     // fmul st(0)
                  < 0xde < 0xc9;    // fmulpst(1), st(0)
            }
            else
            if (v_d == 4.0) {
                s < 0xdc < 0xc8 < 0xdc < 0xc8;
            }
            else
            if (v_d == 5.0) {
                s < 0xd9 < 0xc0     // fld  st(0)
                  < 0xdc < 0xc8 < 0xdc < 0xc8
                  < 0xde < 0xc9;    // fmulp st(1), st(0)
            }
            else
            if (v_d == 6.0) {
                s < 0xd9 < 0xc0     // fld  st(0)
                  < 0xdc < 0xc8 < 0xdc < 0xc8
                  < 0xd8 < 0xc9     // fmul  st(0), st(1)
                  < 0xde < 0xc9;    // fmulp st(1), st(0)
            }
            else
            if (v_d == 7.0) {
                s < 0xd9 < 0xc0     // fld  st(0)
                  < 0xdc < 0xc8 < 0xdc < 0xc8
                  < 0xd8 < 0xc9     // fmul  st(0), st(1)
                  < 0xd8 < 0xc9     // fmul  st(0), st(1)
                  < 0xde < 0xc9;    // fmulp st(1), st(0)
            }
            else
            if (v_d == 8.0) {
                s < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8;
            }
            else
            if (v_d == 16.0) {
                s < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8;
            }
            else
            if (v_d == 32.0) {
                s < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8 < 0xdc < 0xc8;
            }
            else {
                return f;
            }

            if (a_d < 0) {
                s < 0xd9 < 0xe8     // fld1
                  < 0xde < 0xf1;    // fdivrp  st(1),st   // inverse
            }

            ev->m_intermediate.push_back(std::string());
            ev->m_intermediate.back() = s.s.str();
            uint8_t* cc = (uint8_t*)&ev->m_intermediate.back()[0];

            auto f_opt = std::make_shared<Function>("", 1, 0, ev->m_intermediate.back().size(), cc, nullptr);
            f_opt->args[0] = f->args[1];

            elist->erase(f->args[0]);

            return f_opt;
        }
    }
    return f;
}


inline Function Pow()
{
    static uint8_t code[]  =  {
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
        0x77, 0x28,                                 // ja          exit_point               }

        0xd9, 0xe8,                                 // fld1                                 }
        0xde, 0xf1,                                 // fdivrp      st(1),st                 } inverse
        0xeb, 0x22,                                 // jmp         exit_point               }

// pop_before_generic_pow:
        0xdd, 0xd8,                                 // fstp        st(0)
// generic_pow:
        0xd9, 0xc9,                                 // fxch                                 }
        0xd9, 0xe4,                                 // ftst                                 }
        0x9b,                                       // wait                                 } if base is 0, leave it in st(0)
        0xdf, 0xe0,                                 // fnstsw      ax                       } and exit
        0x9e,                                       // sahf                                 }
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
    return Function("pow", 2, 1, sizeof(code), code, pow_optimizer);
}


inline Function Exp()
{
    static uint8_t code[]  =  {
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
    return Function("exp", 1, 1, sizeof(code), code);
}


//inline Function Log()  // old implementation, with base
//{
//    static uint8_t code[]  =  {
//        0xd9, 0xe8,                                 // fld1
//        0xd9, 0xc9,                                 // fxch        st(1)
//        0xd9, 0xf1,                                 // fyl2x
//        0xd9, 0xc9,                                 // fxch        st(1)
//        0xd9, 0xe8,                                 // fld1
//        0xd9, 0xc9,                                 // fxch        st(1)
//        0xd9, 0xf1,                                 // fyl2x
//        0xde, 0xf9                                  // fdivp       st(1),st
//    };
//    return Function("log", 2, 1, sizeof(code), code);
//}


inline Function Ln()
{
    static uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xea,                                 // fldl2e
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function("ln", 1, 1, sizeof(code), code);
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
    static uint8_t code[]  =  {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1,                                 // fyl2x
        0xd9, 0xe9,                                 // fldl2t
        0xde, 0xf9                                  // fdivp       st(1),st
    };
    return Function("log10", 1, 1, sizeof(code), code);
}


inline Function Log2()
{
    static uint8_t code[] = {
        0xd9, 0xe8,                                 // fld1
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf1                                  // fyl2x
    };
    return Function("log2", 1, 0, sizeof(code), code);
}


inline Function Ylog2()
{
    static uint8_t code[] = {
        0xd9, 0xf1                                  // fyl2x
    };
    return Function("ylog2", 2, 0, sizeof(code), code);
}


inline Function Max()
{
    static uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("max", 2, 0, sizeof(code), code);
}


inline Function Min()
{
    static uint8_t code[] = {
        0xdb, 0xf1,                                 // fcomi       st,st(1)
        0xd9, 0xc9,                                 // fxch        st(1)
        0xda, 0xc1,                                 // fcmovb      st,st(1)
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("min", 2, 0, sizeof(code), code);
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
    return Function("floor", 1, 0, sizeof(code), code);
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
    return Function("ceil", 1, 0, sizeof(code), code);
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
    return Function("round", 1, 0, sizeof(code), code);
}


inline Function Int()
{
    static uint8_t code[] = {
        0xd9, 0xfc                                  // frndint
    };
    return Function("int", 1, 0, sizeof(code), code);
}


inline Function Mod()
{
    static uint8_t code[] = {
        0xd9, 0xc9,                                 // fxch        st(1)
        0xd9, 0xf8,                                 // fprem
        0xdd, 0xd9                                  // fstp        st(1)
    };
    return Function("mod", 2, 0, sizeof(code), code);
}


inline Function Less_than()
{
    static uint8_t code[] = {
        0xdf, 0xf1,                                 // fcomip      st,st(1)  
        0xdd, 0xd8,                                 // fstp        st(0)  
        0xd9, 0xe8,                                 // fld1  
        0xd9, 0xee,                                 // fldz  
        0xdb, 0xd1,                                 // fcmovnb     st,st(1)  
        0xdd, 0xd9,                                 // fstp        st(1)  
    };
    return Function("less_than", 2, 0, sizeof(code), code);
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
    return Function("bnd", 2, 2, sizeof(code), code);
}


template <uint8_t OP0, uint8_t OP1 = OP0>
std::shared_ptr<Function> asmd_optimizer(std::shared_ptr<Function> f, evaluator* ev, elist_t* elist)
{
    // If one of the two arguments is CCONST or CVAR (i.e. loaded from memory),
    // it will be applied directly, thus dropping FPU stack requirements by 1.

    for (int i=0; i<2; i++) {
        if ((*f->args[i])->element_type == CCONST || (*f->args[i])->element_type == CVAR) {
            auto v = std::static_pointer_cast<Value>(*f->args[i]);

            uint8_t opcode = (i==0) ? OP0 : OP1;

            mexce_charstream s;
#ifdef MEXCE_64
            s < 0x48 < 0xb8;                            // mov            rax, qword ptr
#else
            s < 0xb8;                                   // mov            eax, dword ptr
#endif
            s << v->address;                            //                   [the address]
            switch(v->numeric_data_type) {
                case M16INT: s < 0xde < opcode; break;  // (instruction)  word  ptr [eax/rax]  
                case M32INT: s < 0xda < opcode; break;  // (instruction)  dword ptr [eax/rax]  
                case M32FP:  s < 0xd8 < opcode; break;  // (instruction)  dword ptr [eax/rax]
                case M64FP:  s < 0xdc < opcode; break;  // (instruction)  qword ptr [eax/rax]
            }

            ev->m_intermediate.push_back(std::string());
            ev->m_intermediate.back() = s.s.str();
            uint8_t* cc = (uint8_t*)&ev->m_intermediate.back()[0];
            bool var_ref = (*f->args[i])->element_type == CVAR;

            auto f_opt = std::make_shared<Function>("", 1, 0, ev->m_intermediate.back().size(), cc, nullptr, var_ref);
            f_opt->args[0] = f->args[(int)(i==0)];

            elist->erase(f->args[i]);

            return f_opt;
        }
    }
    return f;
}


inline Function Add()
{
    static uint8_t code[] = {
        0xde, 0xc1                                  // faddp       st(1), st
    };
    return Function("add", 2, 0, sizeof(code), code, asmd_optimizer<0x00>);
}


inline Function Sub()
{
    static uint8_t code[] = {
        0xde, 0xe9                                  // fsubp       st(1), st
    };
    return Function("sub", 2, 0, sizeof(code), code, asmd_optimizer<0x20, 0x28>);
}


inline Function Mul()
{
    static uint8_t code[] = {
        0xde, 0xc9                                  // fmulp       st(1), st
    };
    return Function("mul", 2, 0, sizeof(code), code, asmd_optimizer<0x08>);
}


inline Function Div()
{
    static uint8_t code[] = {
        0xde, 0xf9                                  // fdivp       st(1), st
    };
    return Function("div", 2, 0, sizeof(code), code, asmd_optimizer<0x30, 0x38>);
}


inline Function Neg()
{
    static uint8_t code[] = {
        0xd9, 0xe0                                  // fchs
    };
    return Function("neg", 1, 0, sizeof(code), code);
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

    static uint8_t code[] = {                       //                       ; FPU stack
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
    return Function("gain", 2, 1, sizeof(code), code);
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
    return Function("bias", 2, 1, sizeof(code), code);
}


inline const ::std::map<string, Function>& make_function_map()
{
    static Function f[] = {
        Sin(), Cos(), Tan(), Abs(), Sign(), Signp(), Expn(), Sfc(), Sqrt(), Pow(), Exp(), Less_than(),
        Log(), Log2(), Ln(), Log10(), Ylog2(), Max(), Min(), Floor(), Ceil(), Round(), Int(), Mod(),
        Bnd(), Add(), Sub(), Neg(), Mul(), Div(), Bias(), Gain()
    };

    static ::std::map<string, Function> ret;
    for (auto& e : f) {
        if (e.name) {
            assert(ret.find(e.name) == ret.end());
            ret.insert(std::make_pair(e.name, e));
        }

    }
    return ret;
}


inline const ::std::map<string, Function>& function_map()
{
    static const ::std::map<string, Function>& name_set = make_function_map();
    return name_set;
}


inline std::shared_ptr<Function> make_function(const std::string& name) {
    auto fn = function_map().find(name);
    return std::make_shared<Function>(fn->second);
}


struct Token
{
    int             type;
    int             priority;
    size_t          position;
    string          content;
    Token():
        type      ( 0 ),
        priority  ( 0 ),
        position  ( 0 ) {}
    Token(int type, size_t position, char content):
        type      ( type               ),
        priority  ( type               ),
        position  ( position           ),
        content   ( string() + content ) {}
};


template <typename> inline Numeric_data_type get_ndt()  { assert(false); }
template <> inline Numeric_data_type get_ndt<double >() { return M64FP;  }
template <> inline Numeric_data_type get_ndt<float  >() { return M32FP;  }
template <> inline Numeric_data_type get_ndt<int16_t>() { return M16INT; }
template <> inline Numeric_data_type get_ndt<int32_t>() { return M32INT; }
template <> inline Numeric_data_type get_ndt<int64_t>() { return M64INT; }


inline bool is_operator(  char c) { return  c=='+' || c=='-'  ||  c=='*' || c=='/'  || c=='^' || c=='<'; }
inline bool is_alphabetic(char c) { return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_'; }
inline bool is_numeric(   char c) { return  c>='0' && c<='9'; }


inline const char* operator_to_function_name(const std::string& op, bool unary = false) {
    if (!unary) {
        if (op == "+") return "add";
        if (op == "-") return "sub";
        if (op == "*") return "mul";
        if (op == "/") return "div";
        if (op == "^") return "pow";
        if (op == "<") return "less_than";
    }
    else {
        if (op == "-") return "neg";
    }
    assert(false);
    return 0;
}

} // mexce_impl


inline
evaluator::evaluator():
    m_buffer_size(0)
{
    // register functions
    using namespace impl;
    m_constants["pi"] = (std::make_shared<Constant>("3.141592653589793238462643383", "pi"));
    m_constants["e" ] = (std::make_shared<Constant>("2.718281828459045235360287471", "e" ));
    assign_expression("0");
}


inline
evaluator::~evaluator()
{
    impl::free_executable_buffer(evaluate, m_buffer_size);
}


template <typename T>
bool evaluator::bind(T& v, const std::string& s)
{
    using namespace impl;
    if ((m_variables.find(s) == m_variables.end()) &&
        (function_map().find(s) == function_map().end()))
    {
        m_variables[s] = std::make_shared<Variable>(&v, s, get_ndt<T>());
        return true;
    }
    return false;
}


inline
bool evaluator::unbind(const std::string& s)
{
    if (s.length() == 0)
         return false;

    auto it = m_variables.find(s);
    if (it != m_variables.end()) {
        if (it->second->element_type != impl::CVAR)
            return false;
        if (it->second->referenced) {
            assign_expression("0");
        }
        m_variables.erase(it);
        return true;
    }
    return false;
}


inline
bool evaluator::assign_expression(std::string e)
{
    using namespace impl;
    using std::list;
    using std::string;
    using mpe = mexce_parsing_exception;

    std::deque<Token> tokens;

    m_literals.clear();
    m_intermediate_constants.clear();

    auto x = m_variables.begin();
    for (; x != m_variables.end(); x++)
        x->second->referenced = false;

    if (e.length() == 0){
        assign_expression("0");
        return true;
    }

    e += ' ';

    //stage 1: checking expression syntax
    Token temp;
    std::vector< std::pair<int, int> > bdarray(1);
    std::map<string, Function>::const_iterator i_fnc;
    int state = 0;
    size_t i = 0;
    int function_parentheses = 0;
    for (; i < e.length(); i++) {
        switch(state) {
            case 0: //start of expression
                if (e[i] == '-' || e[i] == '+') {
                    tokens.push_back(Token(UNARY, i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    if (bdarray.back().first != 0)
                        throw (mpe("Expected an expression", i));
                    if (bdarray.back().second != 0)
                        throw (mpe("Expected more arguments", i));
                    tokens.push_back(Token(FUNCTION_RIGHT_PARENTHESIS, i, ')'));
                    function_parentheses--;
                    bdarray.pop_back();
                    state = 5;
                    break;
                }
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
            case 2: //currently reading a numeric literal, found dot
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
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RIGHT_PARENTHESIS, i, ')'));
                        bdarray.back().first--;
                    }
                    else {
                        if (function_parentheses <= 0)
                            throw (mpe("\")\" not expected", i));
                        if (bdarray.back().second != 1)
                            throw (mpe("Expected more arguments", i));
                        tokens.push_back(Token(FUNCTION_RIGHT_PARENTHESIS, i, ')'));
                        function_parentheses--;
                        bdarray.pop_back();
                    }
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
                    if (bdarray.back().first != 0)
                        throw (mpe("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mpe("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else {
                    throw (mpe((string("\"")+e[i])+"\" not expected", i));
                }
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
                    }
                    else
                    if (m_constants.find(temp.content) != m_constants.end()) {
                        temp.type = CONSTANT_NAME;
                        tokens.push_back(temp);
                        state = 5;
                    }
                    else
                    if ((i_fnc = function_map().find(temp.content)) != function_map().end()) {
                        temp.type = FUNCTION_NAME;
                        tokens.push_back(temp);
                        tokens.push_back(Token(FUNCTION_LEFT_PARENTHESIS, i, '('));
                        bdarray.push_back(std::make_pair(0, i_fnc->second.num_args));
                        function_parentheses++;
                        state = 6;
                    }
                    else {
                        throw (mpe(string(temp.content) +
                            " is not a known constant, variable or function name", i));
                    }
                    break;
                }
                if (e[i] == ')') {
                    temp.type = m_variables.find(temp.content) != m_variables.end() ? VARIABLE_NAME : 
                                m_constants.find(temp.content) != m_constants.end() ? CONSTANT_NAME :
                        throw (mpe(string(temp.content) +
                            " is not a known constant or variable name", i));
                    tokens.push_back(temp);
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RIGHT_PARENTHESIS, i, ')'));
                        bdarray.back().first--;
                    }
                    else
                    if (function_parentheses > 0) {
                        if (bdarray.back().second != 1)
                            throw (mpe("Expected more arguments", i));
                        tokens.push_back(Token(FUNCTION_RIGHT_PARENTHESIS, i, ')'));
                        function_parentheses--;
                        bdarray.pop_back();
                    }
                    else
                        throw (mpe("\")\" not expected", i));
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
                    bdarray.push_back(std::make_pair(0, i_fnc->second.num_args));
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
                    if (bdarray.back().first != 0)
                        throw (mpe("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mpe("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else {
                    throw (mpe((string("\"")+e[i])+"\" not expected", i));
                }
            case 5: //just read an expression (constant/variable/right parenthesis)
                if (e[i] == ' ')
                    break;
                if (is_operator(e[i])) {
                    tokens.push_back(Token(get_infix_rank(e[i]), i, e[i]));
                    state = 4;
                    break;
                }
                if (e[i] == ')') {
                    if (bdarray.back().first > 0) {
                        tokens.push_back(Token(RIGHT_PARENTHESIS, i, ')'));
                        bdarray.back().first--;
                    }
                    else
                    if (function_parentheses > 0) {
                        if (bdarray.back().second != 1)
                            throw (mpe("Expected more arguments", i));
                        tokens.push_back(Token(FUNCTION_RIGHT_PARENTHESIS, i, ')'));
                        function_parentheses--;
                        bdarray.pop_back();
                    }
                    else {
                        throw (mpe("\")\" not expected", i));
                    }
                    state = 5;
                    break;
                }
                if (e[i] == ',') {
                    if (bdarray.back().first != 0)
                        throw (mpe("Expected a \")\"", i));
                    if (bdarray.back().second-- < 2)
                        throw (mpe("Don\'t expect any arguments here", i));
                    tokens.push_back(Token(COMMA, i, ','));
                    state = 0;
                    break;
                }
                else {
                    throw (mpe((string("\"")+e[i])+"\" not expected", i));
                }
            case 6: //just read a function name
                if (e[i] == '(') {
                    state = 0;
                    break;
                }
                else {
                    throw (mpe("Expected a \"(\"", i));
                }
        }
    }
    if ((bdarray.back().first > 0) || (function_parentheses > 0)) {
        throw (mpe("Expected a \")\"", --i));
    }
    if (state != 5) {
        throw (mpe("Unexpected end of expression", --i));
    }

    //stage 2: transform expression to postfix
    std::deque<Token> postfix;
    std::vector<Token> tstack;
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
                postfix.push_back(tstack.back());
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
    list< std::shared_ptr<Element> > elist;
    while (!postfix.empty()) {
        temp = postfix.front();
        postfix.pop_front();
        switch (temp.type) {
            case INFIX_4:
            case INFIX_3:
            case INFIX_2:
            case INFIX_1: {
                auto name = operator_to_function_name(temp.content);
                elist.push_back( make_function(name) );
                break;
            }
            case FUNCTION_NAME:
                elist.push_back(make_function(temp.content));
                break;
            case UNARY:
                if (temp.content == "-") { // unary '+' is ignored
                    elist.push_back( make_function("neg") );
                }
                break;
            case NUMERIC_LITERAL: {
                auto it = m_literals.find(temp.content);
                if (it != m_literals.end()) {
                    elist.push_back(it->second);
                }
                else {
                    m_literals[temp.content] = std::make_shared<Constant>(temp.content, temp.content);
                    elist.push_back(m_literals[temp.content]);
                }
                break;
            }
            case CONSTANT_NAME: {
                elist.push_back(m_constants.find(temp.content)->second);
                break;
            }
            case VARIABLE_NAME: {
                auto it = m_variables.find(temp.content);
                assert(it != m_variables.end());
                it->second->referenced = true;
                elist.push_back(it->second);
                break;
            }
        }
    }

    // link functions to their arguments (1)
    std::vector< elist_it_t > evec;
    for (auto y = elist.begin(); y != elist.end(); y++) {
        if ((*y)->element_type == CFUNC) {
            auto fp = std::static_pointer_cast<Function>(*y);
            for (size_t i = 0; i < fp->num_args; i++) {
                fp->args[i] = evec.back();
                evec.pop_back();
            }
        }
        evec.push_back(y);
    }

    // TODO: Rearrange chains of same level operators, so that dependents are last
    // ...

    // choose more suitable functions, where applicable
    for (auto y = elist.begin(); y != elist.end(); y++) {
        if ((*y)->element_type == CFUNC) {
            auto fp = std::static_pointer_cast<Function>(*y);
            if (fp->optimizer != 0) {
                auto xx = fp->optimizer(fp, this, &elist);
                *y = xx;
            }
        }
    }

    assert(evec.size() == 1);

    // precompute constant expressions
    for (auto y = elist.begin(); y != elist.end(); y++) {
        if ((*y)->element_type == CFUNC) {
            // check if its dependents are const
            auto yt = y;
            Function* fp = (Function*)(y->get());

            if (fp->var_ref) {
                continue;
            }

            int dc = fp->num_args;
            while (true) {
                if (!(dc--)) {
                    // the loop reached the end, it is a constant function
                    auto ynext = std::next(y);
                    compile_elist(yt, ynext);
                    elist.erase(yt, ynext);
                    auto ic = std::make_shared<Constant>(evaluate());
                    m_intermediate_constants.push_back(ic);
                    y = elist.insert(ynext, ic);
                    
                    
                    break;
                }
                yt--;
                if ((*yt)->element_type != CCONST) { // either CVAR, or non-const CFUNC
                    break;
                }
            }
        }
    }

    compile_elist(elist.begin(), elist.end());
    return true;
}


inline
void evaluator::compile_elist(impl::elist_it_t first, impl::elist_it_t last)
{
    using namespace impl;

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

    mexce_charstream code_buffer;    
    elist_it_t y = first;

#ifdef MEXCE_64
    // On x64 we are using rax to fetch/store addresses
    code_buffer < 0x50; // push rax

#endif

    size_t fpu_stack_size = 0;

    for (; y != last; y++) {
        if ((*y)->element_type == CVAR  ||
            (*y)->element_type == CCONST)
        {
            if (++fpu_stack_size > 8) {
                // The caller is expected to supply this function with element lists
                // that have been adjusted accordingly, to mitigate this problem.
                // Therefore, this should be unreachable code.
                //throw mexce_parsing_exception("FPU stack limit exceeded (internal error)", 0);
            }

            Value * tn = (Value *) y->get();

#ifdef MEXCE_64
            code_buffer << (uint16_t)0xb848;   // move input address to rax (opcode)
            code_buffer << (void*)tn->address;
#endif

            switch (tn->numeric_data_type) {
#ifdef MEXCE_64
                // On x64, variable addresses are already supplied in rax.
                case M32FP:   code_buffer < 0xd9 < 0x00; break;
                case M64FP:   code_buffer < 0xdd < 0x00; break;
                case M16INT:  code_buffer < 0xdf < 0x00; break;
                case M32INT:  code_buffer < 0xdb < 0x00; break;
                case M64INT:  code_buffer < 0xdf < 0x28; break;
#else
                // On 32-bit x86, variable addresses are explicitly specified.
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
        }
        else {
            Function * tf = (Function *) y->get();

            fpu_stack_size -= tf->num_args-1;

            code_buffer.s.write((const char*)tf->code, tf->code_size);
        }
    }

    // copy the return sequence
    code_buffer.s.write((const char*)return_sequence, sizeof(return_sequence));

    auto code = code_buffer.s.str();
    auto buffer = get_executable_buffer(code.size());
    memcpy(buffer, &code[0], code.size());

#ifdef MEXCE_64
    // load the intermediate variable's address to rax
    *((uint64_t*)(buffer+code.size()-16)) = (uint64_t)&m_x64_return_var;
#endif

    evaluate = lock_executable_buffer(buffer, code.size());
}

} // mexce

#endif
