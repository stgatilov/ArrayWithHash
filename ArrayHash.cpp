#include "ArrayHash.h"
#include "TestContainer.h"

//========================================================================================

#include <vector>
#include <numeric>
#include <ctime>
#include <random>
#include <cstring>

#include "timer.h"
inline double my_clock() {
	static bool initialized = false;
	if (!initialized) {
		timer_lib_initialize();
		initialized = true;
	}
	return timer_current() * (1000.0 / double(timer_ticks_per_second()));
}

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

template<class Container> double Speed_GrowthArraySequental(int size, int repeats) {
	double start = my_clock();

	for (int i = 0; i < repeats; i++) {
		Container cont;
		for (int x = 0; x < size; x++)
			cont.Set(x, x*x);
	}

	return my_clock() - start;
}

template<class Container> double Speed_GrowthArrayRandom(int size, int repeats) {
	std::vector<int> perm;
	for (int i = 0; i < size; i++)
		perm.push_back(i);
	std::random_shuffle(perm.begin(), perm.end());
	double start = my_clock();

	for (int i = 0; i < repeats; i++) {
		Container cont;
		for (int j = 0; j < size; j++)
			cont.Set(perm[j], j*j);
	}

	return my_clock() - start;
}

template<class Container> double Speed_GrowthHashRandom(int size, int repeats) {
	std::mt19937 rnd;
	std::vector<int> keys;
	for (int i = 0; i < size; i++)
		keys.push_back(std::uniform_int_distribution<int>(-2000000000, 2000000000)(rnd));
	double start = my_clock();

	for (int i = 0; i < repeats; i++) {
		Container cont;
		for (int j = 0; j < size; j++)
			cont.Set(keys[j], keys[j] + 1);
	}

	return my_clock() - start;
}

template<class Container> double Speed_RemoveArrayRandom(int size, int repeats) {
	std::vector<int> perm;
	for (int i = 0; i < size; i++)
		perm.push_back(i);
	std::random_shuffle(perm.begin(), perm.end());
	double start = my_clock();

	Container cont;
	for (int i = 0; i < repeats; i++) {
		for (int j = 0; j < size; j++)
			cont.Set(j, j*j);
		for (int j = 0; j < size; j++)
			cont.Remove(perm[j]);
	}

	return my_clock() - start;
}

template<class Container> double Speed_RemoveHashRandom(int size, int repeats) {
	std::mt19937 rnd;
	std::vector<int> keys;
	for (int i = 0; i < size; i++)
		keys.push_back(std::uniform_int_distribution<int>(-2000000000, 2000000000)(rnd));
	double start = my_clock();

	Container cont;
	for (int i = 0; i < repeats; i++) {
		for (int j = 0; j < size; j++)
			cont.Set(keys[j], keys[j] + 1);
		for (int j = 0; j < size; j++)
			cont.Remove(keys[j]);
	}

	return my_clock() - start;
}

template<class Container> double Speed_GetArrayRandomHit(int size, int repeats) {
	Container cont;
	std::vector<int> keys;
	for (int i = 0; i < size; i++) {
		keys.push_back(i);
		cont.Set(i, i*2);
	}
	std::random_shuffle(keys.begin(), keys.end());
	double start = my_clock();

	volatile int tmp = 0;
	for (int i = 0; i < repeats; i++) {
		int sum = 0;
		for (int j = 0; j < size; j++)
			sum += cont.Get(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_GetArrayRandomMiss(int size, int repeats) {
	Container cont;
	std::vector<int> keys;
	for (int i = 0; i < size; i++)
		if (i & 3)
			cont.Set(i, i);
	for (int i = 0; i < size; i++)
		if (!(i & 3))
			for (int c = 0; c < 4; c++)
				keys.push_back(i);
	std::random_shuffle(keys.begin(), keys.end());
	double start = my_clock();

	volatile int tmp = 0;
	for (int i = 0; i < repeats; i++) {
		int sum = 0;
		for (int j = 0; j < size; j++)
			sum += cont.Get(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_GetArrayRandomMix(int size, int repeats) {
	Container cont;
	std::vector<int> keys;
	for (int i = 0; i < size; i++) {
		keys.push_back(i);
		cont.Set(i*2, i * 17);
	}
	std::random_shuffle(keys.begin(), keys.end());
	double start = my_clock();

	volatile int tmp = 0;
	for (int i = 0; i < repeats; i++) {
		int sum = 0;
		for (int j = 0; j < size; j++)
			sum += cont.Get(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_GetHashRandomHit(int size, int repeats) {
	Container cont;
	std::mt19937 rnd;
	std::vector<int> keys;
	for (int i = 0; i < size; i++) {
		keys.push_back(std::uniform_int_distribution<int>(-2000000000, 2000000000)(rnd));
		cont.Set(keys[i], i*i);
	}
	double start = my_clock();

	volatile int tmp = 0;
	for (int i = 0; i < repeats; i++) {
		int sum = 0;
		for (int j = 0; j < size; j++)
			sum += cont.Get(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_GetHashRandomMiss(int size, int repeats) {
	Container cont;
	std::mt19937 rnd;
	for (int i = 0; i < size; i++)
		cont.Set(std::uniform_int_distribution<int>(-2000000000, 2000000000)(rnd), i*i);
	std::vector<int> keys;
	for (int i = 0; i < size; i++)
		keys.push_back(std::uniform_int_distribution<int>(-2000000000, 2000000000)(rnd));
	double start = my_clock();

	volatile int tmp = 0;
	for (int i = 0; i < repeats; i++) {
		int sum = 0;
		for (int j = 0; j < size; j++)
			sum += cont.Get(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_SetArraySequentalMiss(int size, int repeats) {
	std::vector<int> perm;
	for (int i = 0; i < size; i++)
		perm.push_back(i);
	std::random_shuffle(perm.begin(), perm.end());
	double start = my_clock();

	for (int i = 0; i < repeats; i++) {
		Container cont;
		cont.Reserve(size, 0);
		for (int j = 0; j < size; j++)
			cont.Set(j, j*j);
	}

	return my_clock() - start;
}


template<class Container> double Speed_GetPtrArrayRandomMix(int size, int repeats) {
	Container cont;
	std::vector<int> keys;
	for (int i = 0; i < size; i++) {
		keys.push_back(i);
		cont.Set(i*2, i * 17);
	}
	std::random_shuffle(keys.begin(), keys.end());
	double start = my_clock();

	volatile size_t tmp = 0;
	for (int i = 0; i < repeats; i++) {
		size_t sum = 0;
		for (int j = 0; j < size; j++)
			sum ^= (size_t)cont.GetPtr(keys[j]);
		tmp += sum;
	}

	return my_clock() - start;
}

template<class Container> double Speed_SetArrayRandomMix(int size, int repeats) {
	std::vector<int> perm;
	for (int j = 0; (1<<j) < size; j++) {	//binary tree randomized BFS order
		int k = perm.size();
		for (int i = 0; i < size; i += (1<<j))
			perm.push_back(i);
		std::random_shuffle(perm.begin() + k, perm.end());
	}
	std::reverse(perm.begin(), perm.end());
	double start = my_clock();

	for (int i = 0; i < repeats / 2; i++) {
		Container cont;
		cont.Reserve(size, 0);
		for (size_t j = 0; j < perm.size(); j++)
			cont.Set(perm[j], j*j);
	}

	return my_clock() - start;
}

template<class Container> double Speed_SetIfNewArrayRandomMix(int size, int repeats) {
	std::vector<int> perm;
	for (int j = 0; (1<<j) < size; j++) {	//binary tree randomized BFS order
		int k = perm.size();
		for (int i = 0; i < size; i += (1<<j))
			perm.push_back(i);
		std::random_shuffle(perm.begin() + k, perm.end());
	}
	std::reverse(perm.begin(), perm.end());
	double start = my_clock();

	for (int i = 0; i < repeats / 2; i++) {
		Container cont;
		cont.Reserve(size, 0);
		for (size_t j = 0; j < perm.size(); j++)
			cont.SetIfNew(perm[j], j*j);
	}

	return my_clock() - start;
}







void SpeedAll() {
#define TIME_CALL(func, params) { \
	double timeMine = Speed_##func<ArrayHash>params; \
	double timeStl = Speed_##func<StdMapWrapper>params; \
	printf("%s %s: %0.2lf mine, %0.2lf stl, %0.2lf speedup\n", #func, #params, timeMine, timeStl, timeStl / timeMine); \
}

	TIME_CALL(GetArrayRandomHit, (100000, 100));
	TIME_CALL(GetArrayRandomMiss, (100000, 100));
	TIME_CALL(GetArrayRandomMix, (100000, 100));
	TIME_CALL(GetHashRandomHit, (100000, 100));
	TIME_CALL(GetHashRandomMiss, (100000, 100));

	TIME_CALL(GrowthArraySequental, (100000, 100));
	TIME_CALL(GrowthArrayRandom, (100000, 100));
	TIME_CALL(GrowthHashRandom, (100000, 100));

	TIME_CALL(RemoveArrayRandom, (100000, 100));
	TIME_CALL(RemoveHashRandom, (100000, 100));

	TIME_CALL(SetArraySequentalMiss, (100000, 100));

	TIME_CALL(GetPtrArrayRandomMix, (100000, 100));
	TIME_CALL(SetArrayRandomMix, (100000, 100));
	TIME_CALL(SetIfNewArrayRandomMix, (100000, 100));


#undef TIME_CALL
}

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
			SpeedAll();
		if (strncmp(argv[i], "-t", 2) == 0) {
			int lvl;
			if (sscanf(argv[i], "-t%d", &lvl) != 1)
				lvl = 2;
			std::mt19937 rnd;
			while (1) TestsRound(rnd, lvl);
		}
	}

	return 0;
}

