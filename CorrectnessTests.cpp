#include "CorrectnessTests.h"
#include "TestContainer.h"

#include <vector>
#include <numeric>
#include <cstring>

//for leak detection
//#include "vld.h"

bool quietTests = false;
int assertLevel = 2;


namespace std {
	//Workaround for a bug in MSVC2013 compiler:
	//http://stackoverflow.com/q/34135409/556899
	template<class T> struct is_copy_constructible<std::unique_ptr<T>> : false_type {};
};

//ugly way to exclude for compilation on condition
template<bool Enabled> struct Getter {
	template<class Container, class Key>
	static void Do(Container &dict, Key key) {}
};
template<> struct Getter<true> {
	template<class Container, class Key>
	static void Do(Container &dict, Key key) { dict.Get(key); }
};

template<class Container, class Key>
void TestRandom(Container &dict, std::vector<double> typeProbs, int operationsCount, Key minKey, Key maxKey, std::mt19937 &rnd) {
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
		printf("TestRandom<%s>: %d opers, keys in [%d, %d]\n", dict.label, operationsCount, minKey, maxKey);
		printf("    probs: %s\n", signature.c_str());
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

	dict.assertLevel = assertLevel;
	typedef Container::Value Value;

	int doneOps = 0;
	while (doneOps < operationsCount) {
		double param = std::uniform_real_distribution<double>(0.0, 1.0)(rnd);
		int type = std::lower_bound(prefSums.begin(), prefSums.end(), param) - prefSums.begin() - 1;

		Key key = std::uniform_int_distribution<Key>(minKey, maxKey)(rnd);
		Value value = ValueTestingUtils<Value>::Generate(rnd);

		if (type == 0) {
			dict.GetSize();
		}
		else if (type == 1) {
			//Note: excluded from compilation for e.g. unique_ptr
			Getter<std::is_copy_constructible<Value>::value>::Do(dict, key);
		}
		else if (type == 2) {
			dict.GetPtr(key);
		}
		else if (type == 3) {
			dict.Set(key, std::move(value));
		}
		else if (type == 4) {
			dict.SetIfNew(key, std::move(value));
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
			Container tmp;
			tmp.Set(0, ValueTestingUtils<Value>::Generate(rnd));
			tmp.Set(1, ValueTestingUtils<Value>::Generate(rnd));
			tmp.Set(2, ValueTestingUtils<Value>::Generate(rnd));
			tmp.Set(42, ValueTestingUtils<Value>::Generate(rnd));
			tmp.Set(27, ValueTestingUtils<Value>::Generate(rnd));
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

#define DECL_CONTAINER(Key, Value) \
	TestContainer<Key, Value> dict; \
	sprintf(dict.label, "%s->%s", #Key, #Value);

void TestsRound_Int32(std::mt19937 &rnd) {
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1000, -100, 100, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01, 0.01, 0.01, 0.01}, 1000, -10, 10, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 2000, -100, 100, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, 0, 100, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1}, 1000, -50, 50, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 50, 50, 1, 1, 1, 1, 1}, 1000, -10, 10, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, -100, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, -120, 120, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 0, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 100, 300, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, 0, 500, rnd);
	}
	{
		DECL_CONTAINER(int32_t, int32_t);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, -2000000000, 2000000000, rnd);
	}
}

void TestsRound_UniquePtr(std::mt19937 &rnd) {
	{
		DECL_CONTAINER(int32_t, std::unique_ptr<int32_t>);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1000, -100, 100, rnd);
	}
	{
		DECL_CONTAINER(int32_t, std::unique_ptr<int32_t>);
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, -2000000000, 2000000000, rnd);
	}
}

void TestsRound(std::mt19937 &rnd) {
	TestsRound_Int32(rnd);
	TestsRound_UniquePtr(rnd);
}
