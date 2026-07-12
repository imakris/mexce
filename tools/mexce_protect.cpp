#include "mexce_protect_lib.h"

#include <cstdio>


#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc != 5) {
        std::fprintf(stderr,
            "Usage: mexce_protect <expression-file> <schema-file> "
            "<program-output> <key-output>\n");
        return 2;
    }

    try {
        mexce::issuer::protect_expression(argv[1], argv[2], argv[3], argv[4]);
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "mexce_protect: %s\n", error.what());
        return 1;
    }
}
