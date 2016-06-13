#include "ArrayHash.h"
#include "TestContainer.h"
#include "CorrectnessTests.h"
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

