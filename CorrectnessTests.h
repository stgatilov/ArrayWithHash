//          Copyright Stepan Gatilov 2016.
// Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <random>

extern bool quietTests;
extern int assertLevel;

void TestsRound(std::mt19937 &rnd);
