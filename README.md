# mexce 

Mini Expression Compiler/Evaluator

## Overview

mexce is a small runtime compiler of mathematical expressions, written in C++. It generates machine code that primarily uses the x87 FPU and it is a single header with no dependencies.

I wrote this back in 2003 as part of a larger application and then its existence was almost forgotten. The code was now updated with added support for Data Execution Prevention (which probably did not exist back then) and x64.

It currently supports Windows and Linux.

## Usage

Here is an example, taken from the source:

```cpp
float   x = 0.0f;
double  y = 0.1;
int     z = 200;
double  wv[200];

mexce::evaluator eval;
eval.bind(x, "x");
eval.bind(y, "y");
eval.bind(z, "x");   // This will return false and will have no effect.
                     // That is because an "x" is already bound.
eval.bind(z, "z");

eval.assign_expression("0.3+(-sin(2.33+x-log((.3*pi+(88/y)/e),3.2+z)))/98");

for (int i = 0; i < 200; i++, x-=0.1f, y+=0.212, z+=2) {
    wv[i] = eval.evaluate(); // results will be different, depending on x, y, z
}

eval.unbind("x");    // releasing a committed variable which is contained in the
                     // assigned expression will automatically invalidate the
                     // expression.

wv[0] = eval.evaluate(); //Now it will return 0
```

## Performance

Apparently, mexce is quite fast.
Here is how it compares in the [math-parser-benchmark-project](https://github.com/ArashPartow/math-parser-benchmark-project) on an AMD Zen2:

Scores:
| #   | Parser               |  Type        |   Points  |Score  |Failures
  ----|----------------------|--------------|-----------|-------|--------
  00  | ExprTk               |  double      |     2689  |  100  |  0
 ***01***|***mexce***        |  ***double***|***2474*** |***161***|  ***0***
  02  | METL                 |  double      |     1993  |   52  |  0
  03  | FParser 4.5          |  double      |     1768  |   49  |  2
  04  | muparser 2.2.4       |  double      |     1715  |   45  |  0
  05  | atmsp 1.0.4          |  double      |     1606  |   43  |  4
  06  | muparserSSE          |  float       |     1545  |   80  | 72
  07  | muparser 2.2.4 (omp) |  double      |     1441  |   42  |  0
  08  | ExprTkFloat          |  float       |     1350  |   41  | 71
  09  | TinyExpr             |  double      |     1181  |   36  |  3
  10  | MathExpr             |  double      |      944  |   27  | 12
  11  | MTParser             |  double      |      906  |   30  |  9
  12  | Lepton               |  double      |      458  |    9  |  4
  13  | muparserx            |  double      |      236  |    5  |  0

## License

The source code of the library is licensed under the Simplified BSD License.
