#pragma once

#include <random>

extern bool quietTests;
extern int assertLevel;

void TestsRound(std::mt19937 &rnd);
