# mexce 

Mini Expression Compiler/Evaluator

## Overview

mexce is a small runtime compiler of mathematical expressions, written in C++. It generates machine code that primarily uses the x87 FPU and it is a single header with no dependencies.

I wrote this back in 2003 as part of an application and then almost forgot its existence. The code was now updated with added support for Data Execution Prevention (which probably did not exist back then) and x64.

It currently supports Windows only, but Linux will be supported soon.

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

eval.assign_expression("0.3f+(-sin(2.33f+x-log((.3*PI+(88/y)/E),3.2+z)))/98");

for (int i = 0; i < 200; i++, x-=0.1f, y+=0.212, z+=2) {
    wv[i] = eval.evaluate(); // results will be different, depending on x, y, z
}

eval.unbind("x");    // releasing a committed variable which is contained in the
                     // assigned expression will automatically invalidate the
                     // expression.

wv[0] = eval.evaluate(); //Now it will return 0
```

## Disclaimer

The library probably works fine, but don't take my word for it. It should be thorougly re-tested.

## License

The source code of the library is licensed under the Simplified BSD License.
