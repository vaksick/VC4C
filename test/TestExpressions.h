/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef TEST_EXPRESSIONS_H
#define TEST_EXPRESSIONS_H

#include "cpptest.h"

class TestExpressions : public Test::Suite
{
public:
    TestExpressions();
    
    void testCreation();
    void testCombination();
};

#endif /* TEST_EXPRESSIONS_H */