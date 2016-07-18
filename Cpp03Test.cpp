#define AWH_NO_CPP11
#include "ArrayWithHash.h"
#include "StdMapWrapper.h"

struct KeyTraits {
	typedef int32_t Key;
	typedef uint32_t Size;
	static const Key EMPTY_KEY = 0x7FFFFFFF;
	static const Key REMOVED_KEY = EMPTY_KEY - 1;
	static inline Size HashFunction(Key key) {
		Size a = key;
		a = (a+0x7ed55d16) + (a<<12);
		a = (a^0xc761c23c) ^ (a>>19);
		a = (a+0x165667b1) + (a<<5);
		a = (a+0xd3a2646c) ^ (a<<9);
		a = (a+0xfd7046c5) + (a<<3);
		a = (a^0xb55a4f09) ^ (a>>16);
		return a;
	}
};

struct ValueTraits {
	typedef int32_t Value;
	static const Value EMPTY_VALUE = 0x7FFFFFFF;
	static inline bool IsEmpty(const int32_t &value) { return value == EMPTY_VALUE; }
	static inline Value GetEmpty() { return EMPTY_VALUE; }
	static const bool RELOCATE_WITH_MEMCPY = true;
};

typedef Awh::ArrayWithHash<int32_t, int32_t, KeyTraits, ValueTraits> TArrayWithHash;
typedef Awh::StdMapWrapper<int32_t, int32_t, KeyTraits, ValueTraits> TStdMapWrapper;

struct SummatorAction {
	int32_t sum;
	int32_t mask;
	SummatorAction() : sum(0), mask(0) {}
	inline bool operator() (int32_t key, int32_t value) {
		printf("FE: %d %d\n", key, value);
		sum += value;
		mask |= 1<<key;
		return false;
	}
};

template<class Container> void RunTest() {
	typedef typename Container::Ptr Ptr;
	Container dict;
	for (int i = 0; i < 10; i++)
		dict.Set(i, 11*i);

	int sum = 0;
	for (int i = 5; i < 10; i++)
		sum += dict.Get(i);
	printf("%d\n", sum);
	dict.Reserve(32, 0);

	dict.SetIfNew(9, 0);
	dict.SetIfNew(12, 0);
	for (int i = 8; i<=12; i++) {
		Ptr val = dict.GetPtr(i);
		if (!val) continue;
		if (*val == 0)
			printf("%d\n", i);
	}
	printf("%d\n", dict.GetSize());

	SummatorAction action;
	dict.ForEach(action);
	printf("%d %08X\n", action.sum, action.mask);

	Container other;
	other.Set(0, 2);
	other.Set(1, 3);
	other.Set(2, 5);
	other.Set(3, 7);
	other.Set(4, 11);
	other.Set(5, 13);

	std::swap(dict, other);
	dict.Clear();
	std::swap(dict, other);
	printf("%d %d\n", dict.GetSize(), other.GetSize());

    for (int i = 0; i < 5; i++)
    	dict.Remove(i);
	printf("%d\n", dict.GetSize());
    for (int i = 0; i < 15; i++) {
    	Ptr val = dict.GetPtr(i);
    	if (val && *val > 0)
    		dict.RemovePtr(val);
    }
	printf("%d\n", dict.GetSize());
}

int main() {
	RunTest<TArrayWithHash>();
	RunTest<TStdMapWrapper>();
	return 0;
}
