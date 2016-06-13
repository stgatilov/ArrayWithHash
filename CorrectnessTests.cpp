#include "CorrectnessTests.h"
#include "TestContainer.h"

#include <vector>
#include <numeric>
#include <cstring>

bool quietTests = false;

void TestRandom(TestContainer &dict, std::vector<double> typeProbs, int operationsCount, Key minKey, Key maxKey, std::mt19937 &rnd) {
	double allSum = std::accumulate(typeProbs.begin(), typeProbs.end(), 0.0);
	std::string signature = "|";
	for (size_t i = 0; i < typeProbs.size(); i++) {
		typeProbs[i] /= allSum;
		char buff[16];
		int percent = int(typeProbs[i] * 100.0 + 0.5);
		if (percent > 0)
			sprintf(buff, "%02d", std::min(percent, 99));
		else
			sprintf(buff, typeProbs[i] > 0.0 ? "0x" : "00");
			
		signature += buff + std::string("|");
	}

	if (!quietTests) {
		printf("TestRandom: %d opers, keys in [%d, %d]\n", operationsCount, minKey, maxKey);
		printf("     probs: %s\n", signature.c_str());
		fflush(stdout);
	}

	double sum = 0.0;
	std::vector<double> prefSums(1, 0.0);
	for (size_t i = 0; i < typeProbs.size(); i++) {
		sum += typeProbs[i];
		prefSums.push_back(sum);
	}
	prefSums.front() = -1e+50;
	prefSums.back() = 1e+50;

	int doneOps = 0;
	while (doneOps < operationsCount) {
		double param = std::uniform_real_distribution<double>(0.0, 1.0)(rnd);
		int type = std::lower_bound(prefSums.begin(), prefSums.end(), param) - prefSums.begin() - 1;

		Key key = std::uniform_int_distribution<Key>(minKey, maxKey)(rnd);
		Value value = std::uniform_int_distribution<int>(-10000, 10000)(rnd);

		if (type == 0) {
			dict.GetSize();
		}
		else if (type == 1) {
			dict.Get(key);
		}
		else if (type == 2) {
			dict.GetPtr(key);
		}
		else if (type == 3) {
			dict.Set(key, value);
		}
		else if (type == 4) {
			dict.SetIfNew(key, value);
		}
		else if (type == 5) {
			dict.Remove(key);
		}
		else if (type == 6) {
			if (dict.GetSize() == 0)
				continue;
			dict.RemovePtr(dict.SomePtr(rnd));
		}
		else if (type == 7) {
			int arrSz = std::uniform_int_distribution<int>(0, operationsCount)(rnd);
			int hashSz = std::uniform_int_distribution<int>(0, operationsCount)(rnd);
			int flag = std::uniform_int_distribution<int>(0, 1)(rnd);
			dict.Reserve(arrSz, hashSz, flag == 0);
		}
		else if (type == 8) {
			TestContainer tmp;
			tmp.Set(42, 8);
			dict.Swap(tmp);
		}
		else if (type == 9) {
			dict.Clear();
		}
		else if (type == 10) {
			dict.CalcCheckSum();
		}

		doneOps++;
	}
}

void TestsRound(std::mt19937 &rnd, int level) {
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1000, -100, 100, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01, 0.01, 0.01, 0.01}, 1000, -10, 10, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 2000, -100, 100, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, 0, 100, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1}, 1000, -50, 50, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 50, 50, 1, 1, 1, 1, 1}, 1000, -10, 10, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, -100, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, -120, 120, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 0, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 100, 300, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, 0, 500, rnd);
	}
	{
		TestContainer dict(level);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, -2000000000, 2000000000, rnd);
	}
}
