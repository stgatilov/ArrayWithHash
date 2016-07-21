//          Copyright Stepan Gatilov 2016.
// Distributed under the Boost Software License, Version 1.0.
//      (See accompanying file LICENSE.md or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "CorrectnessTests.h"
#include "PerformanceTests.h"
#include <cstring>

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Specify one of parameters: -s (speed), -t2 (tests)\n");
		return 666;
	}

/*	while (1) {
		double q = Speed_GrowthHashRandom<ArrayHash>(100000, 100);
		std::cout << q << "\n";
	}*/

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-q") == 0)
			quietTests = true;
		if (strcmp(argv[i], "-s") == 0)
			SpeedAll(false);
		if (strcmp(argv[i], "-sc") == 0)
			SpeedAll(true);
		if (strncmp(argv[i], "-t", 2) == 0) {
			int lvl;
			if (sscanf(argv[i], "-t%d", &lvl) != 1)
				lvl = 2;
			std::mt19937 rnd;
			assertLevel = lvl;
			//for (int t = 0; t < 10; t++)
			while (1)
				TestsRound(rnd);
		}
	}

	return 0;
}

