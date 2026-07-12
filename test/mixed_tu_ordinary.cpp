#include "mexce.h"

#include <cstddef>


mexce::evaluator* make_ordinary_evaluator()
{
    return new mexce::evaluator;
}


double evaluate_on_ordinary_side(mexce::evaluator* evaluator)
{
    return evaluator->evaluate();
}


void destroy_on_ordinary_side(mexce::evaluator* evaluator)
{
    delete evaluator;
}


size_t ordinary_evaluator_size()
{
    return sizeof(mexce::evaluator);
}
