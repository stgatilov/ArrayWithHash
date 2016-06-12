#include <cstdint>
#include <algorithm>
#include <cassert>
#include <memory>
#include <set>
#include <limits>

// Key - some integer
//   special values: EMPTY, REMOVED
// Value - POD (bool, int, double, pointer, POD struct, etc)
//   special value: EMPTY
//   zero-byte empty? NO
// shrinking? NO

typedef int32_t Key;
typedef int32_t Value;
typedef std::make_unsigned<Key>::type Size;

static const Key EMPTY_KEY = std::numeric_limits<Key>::max();
static const Key REMOVED_KEY = std::numeric_limits<Key>::max() - 1;

static const Value EMPTY_VALUE = std::numeric_limits<Value>::max();
inline bool IsEmpty(Value value) { return value == EMPTY_VALUE; }
inline void SetEmpty(Value &value) { value = EMPTY_VALUE; }

inline Size HashFunction(Key key) {
	//Knuth's function: http://stackoverflow.com/a/665545/556899
	return Size(2654435761) * Size(key);
}

//default slow implementations
template<class Size> Size log2size(Size sz) {
	Size res = 0;
	while (res < 8 * sizeof(Size) && (Size(1) << res) <= sz)
		res++;
	return res;
}
template<class Size> Size log2up(Size sz) {
	return sz == 0 ? 0 : log2size(sz - 1);
}

//fast implementations
//TODO: MSVC check
inline uint32_t log2size(uint32_t sz) {
	unsigned long pos;
	if (!_BitScanReverse(&pos, (unsigned long)sz))		//bsr
		pos = -1;		//branchless
	return uint32_t(pos) + 1;
}
//TODO: x64 architecture check
inline uint64_t log2size(uint64_t sz) {
	unsigned long pos;
	if (!_BitScanReverse64(&pos, (unsigned __int64)sz))
		pos = -1;
	return uint64_t(pos) + 1;
}
//TODO: gcc alternative
inline uint16_t log2size(uint16_t sz) { return log2size(uint32_t(sz)); }
inline uint8_t log2size(uint8_t sz) { return log2size(uint32_t(sz)); }


static const double ARRAY_MIN_FILL = 0.45;
static const double HASH_MIN_FILL = 0.30;
static const double HASH_MAX_FILL = 0.75;
static const size_t ARRAY_MIN_SIZE = 8;
static const size_t HASH_MIN_SIZE = 8;

template<class typeA, class typeB> bool follows(typeA a, typeB b) {
	return !a || b;
}
inline Size IsHashFull(Size cfill, Size sz) {
	return cfill >= ((sz >> 2) * 3);
};


class ArrayHash {
	Size arrayCount, arraySize;
	Size hashSize, hashCount, hashFill;
	std::unique_ptr<Value[]> arrayValues, hashValues;
	std::unique_ptr<Key[]> hashKeys;


	template<class Elem, bool Init = true> static Elem* AllocateBuffer(Size elemCount) {
		if (elemCount <= 0)
			return nullptr;
		if (Init)
			return new Elem[elemCount];
		else
			return (Elem*) operator new[] (elemCount * sizeof(Elem));
	}

	inline bool InArray(Key key) const {
		return Size(key) < arraySize;
	}
	inline bool InArray(Value *ptr) const {
		size_t offset = (char*)ptr - (char*)arrayValues.get();
		return offset < arraySize * sizeof(Value);
	}

	Size FindCellEmpty(Key key) const {
		assert(hashSize);
		Size cell = HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}
	Size FindCellKeyOrEmpty(Key key) const {
		assert(hashSize);
		Size cell = HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != key)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}

	void AdaptSizes(Key newKey) {
		static const int BITS = sizeof(Size) * 8;
		Size logHisto[BITS + 1] = {0};
		Size logArraySize = log2up(arraySize);

		//count number of keys of size in [2^(t-1); 2^t - 1]
		logHisto[logArraySize] = arrayCount;
		logHisto[log2size(newKey)]++;
		for (Size i = 0; i < hashSize; i++) {
			Key key = hashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			Size keyBits = log2size((Size)key);
			assert(keyBits >= logArraySize);
			logHisto[keyBits]++;
		}

		//choose appropriate array size
		Size newArraySize = 0, newArrayCount = 0;
		Size lowerBound = std::max(arraySize, (Size)ARRAY_MIN_SIZE);
		Size prefSum = 0;
		for (Size i = logArraySize; i < BITS; i++) {
			prefSum += logHisto[i];
			Size aSize = 1 << i;
			Size required = Size(ARRAY_MIN_FILL * aSize);
			if (aSize <= lowerBound || prefSum >= required) {
				newArraySize = aSize;
				newArrayCount = prefSum;
			}
			else if (arrayCount + hashCount < required)
				break;
		}
		if (arraySize == 0 && newArrayCount == 0)
			newArraySize = 0;

		//choose hash table size
		Size newHashCount = arrayCount + hashCount - newArrayCount + 1;
		Size newHashSize = std::max(hashSize, (Size)HASH_MIN_SIZE);
		while (newHashCount >= HASH_MIN_FILL * newHashSize * 2)
			newHashSize *= 2;
		if (hashSize == 0 && newHashCount == 0)
			newHashSize = 0;

		//change sizes and relocate data
		Reallocate(newArraySize, newHashSize);
	}

	void RelocateArrayPart(Size &newArraySize) {
		std::unique_ptr<Value[]> newArrayValues(AllocateBuffer<Value, false>(newArraySize));
		//Note: trivial relocation
		memcpy(newArrayValues.get(), arrayValues.get(), arraySize * sizeof(Value));
		operator delete[](arrayValues.release());
		//std::uninitialized_copy_n(arrayValues.get(), arraySize, newArrayValues.get());
		std::uninitialized_fill_n(newArrayValues.get() + arraySize, newArraySize - arraySize, EMPTY_VALUE);

		std::swap(arrayValues, newArrayValues);
		std::swap(arraySize, newArraySize);
	}

	template<bool RELOC_ARRAY> void RelocateHashInPlace(Size newArraySize) {
		//relocate array if required
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		if (hashSize == 0)
			return;
		Size totalCount = arrayCount + hashCount;
		
		//find first empty cell
		Size firstEmpty = 0;
		while (hashKeys[firstEmpty] != EMPTY_KEY)
			firstEmpty++;

		//do a full round from it
		Size pos = firstEmpty;
		do {
			Key key = hashKeys[pos];
			hashKeys[pos] = EMPTY_KEY;

			if (key != EMPTY_KEY && key != REMOVED_KEY) {
				const Value &value = hashValues[pos];
				if (RELOC_ARRAY && InArray(key)) {
					arrayValues[key] = value;
					arrayCount++;
				}
				else {
					//insert key as usual
					Size cell = FindCellEmpty(key);
					hashKeys[cell] = key;
					//move value if necessary
					if (cell != pos)
						hashValues[cell] = value;
				}
			}

			pos = (pos + 1) & (hashSize - 1);
		} while (pos != firstEmpty);

		//update hash count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//forget about removed entries
		hashFill = hashCount;
	}

	template<bool RELOC_ARRAY> void RelocateHashToNew(Size newHashSize, Size newArraySize) {
		//relocate array if required
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//create new hash table and swap with it
		std::unique_ptr<Key[]> newHashKeys(AllocateBuffer<Key, false>(newHashSize));
		std::uninitialized_fill_n(newHashKeys.get(), newHashSize, EMPTY_KEY);
		std::unique_ptr<Value[]> newHashValues(AllocateBuffer<Value>(newHashSize));

		std::swap(hashKeys, newHashKeys);
		std::swap(hashValues, newHashValues);
		std::swap(hashSize, newHashSize);
		//Note: new* are now actually old values

		Size totalCount = arrayCount + hashCount;

		for (Size i = 0; i < newHashSize; i++) {
			Key key = newHashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			const Value &value = newHashValues[i];
			if (RELOC_ARRAY && InArray(key)) {
				arrayValues[key] = value;
				arrayCount++;
			}
			else {
				Size cell = FindCellEmpty(key);
				hashKeys[cell] = key;
				hashValues[cell] = value;
			}
		}

		//update hash count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//forget about removed entries
		hashFill = hashCount;
	}

	void Reallocate(Size newArraySize, Size newHashSize) {
		assert(newArraySize >= arraySize && newHashSize >= hashSize);

		if (newHashSize == hashSize) {
			if (newArraySize == arraySize)
				RelocateHashInPlace<false>(newArraySize);
			else
				RelocateHashInPlace<true>(newArraySize);
		}
		else {
			if (newArraySize == arraySize)
				RelocateHashToNew<false>(newHashSize, newArraySize);
			else
				RelocateHashToNew<true>(newHashSize, newArraySize);
		}
	}


	Value HashGet(Key key) const {
		if (hashSize == 0)
			return EMPTY_VALUE;
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? EMPTY_VALUE : hashValues[cell];
	}

	Value *HashGetPtr(Key key) const {
		if (hashSize == 0)
			return nullptr;
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? nullptr : &hashValues[cell];
	}

	Value *HashSet(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			AdaptSizes(key);
			return Set(key, value);
		}
		Size cell = FindCellKeyOrEmpty(key);
		hashFill += (hashKeys[cell] == EMPTY_KEY);
		hashCount += (hashKeys[cell] == EMPTY_KEY);
		hashKeys[cell] = key;
		hashValues[cell] = value;
		return &hashValues[cell];
	}

	Value *HashSetIfNew(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			AdaptSizes(key);
			return SetIfNew(key, value);
		}
		Size cell = FindCellKeyOrEmpty(key);
		if (hashKeys[cell] != EMPTY_KEY)
			return &hashValues[cell];
		hashFill++;
		hashCount++;
		hashKeys[cell] = key;
		hashValues[cell] = value;
		return nullptr;
	}

	void HashRemove(Key key) {
		if (hashSize == 0)
			return;
		Size cell = FindCellKeyOrEmpty(key);
		if (hashKeys[cell] != EMPTY_KEY) {
			hashKeys[cell] = REMOVED_KEY;
			hashCount--;
		}
	}

	void HashRemovePtr(Value *ptr) {
		Size cell = ptr - &hashValues[0];
		assert(hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != REMOVED_KEY);
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
	}


public:

	ArrayHash() : arraySize(0), hashSize(0), arrayCount(0), hashCount(0), hashFill(0) {}

	void Swap(ArrayHash &other) {
		std::swap(arraySize, other.arraySize);
		std::swap(arrayCount, other.arrayCount);
		std::swap(hashSize, other.hashSize);
		std::swap(hashCount, other.hashCount);
		std::swap(hashFill, other.hashFill);
		std::swap(arrayValues, other.arrayValues);
		std::swap(hashValues, other.hashValues);
		std::swap(hashKeys, other.hashKeys);
	}

	//remove all elements without shrinking
	void Clear() {
		if (arraySize && arrayCount)
			std::fill_n(arrayValues.get(), arraySize, EMPTY_VALUE);
		if (hashSize && hashFill)
			std::fill_n(hashKeys.get(), hashSize, EMPTY_KEY);
		arrayCount = hashCount = hashFill = 0;
	}

	inline Size GetSize() const {
		return arrayCount + hashCount;
	}

	//returns value for given key
	//or EMPTY_VALUE if not present
	inline Value Get(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key))
			return arrayValues[key];
		else
			return HashGet(key);
	}

	//returns pointer to the value for given key
	//or NULL if not present
	inline Value *GetPtr(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			return IsEmpty(val) ? nullptr : &val;	//branchless
		}
		else
			return HashGetPtr(key);
	}

	//sets value associated with given key
	//key is inserted if not present before
	//returns pointer to the updated value
	inline Value *Set(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			arrayCount += IsEmpty(oldVal);	//branchless
			oldVal = value;
			return &oldVal;
		}
		else
			return HashSet(key, value);
	}

	//if key is present, then returns pointer to it
	//otherwise adds a new key with associated value, returns NULL
	inline Value *SetIfNew(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			if (IsEmpty(oldVal)) {					//real branch
				oldVal = value;
				arrayCount++;
				return nullptr;
			}
			else
				return &oldVal;
/*			Value *pOldVal = &arrayValues[key];		//branchless version worth it?
			Value stored = *pOldVal;
			bool empty = IsEmpty(stored);
			stored = (empty ? value : stored);
			arrayCount += empty;
			*pOldVal = stored;
			return empty ? nullptr : pOldVal;*/
		}
		else
			return HashSetIfNew(key, value);
	}

	//removes given key (if present)
	inline void Remove(Key key) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			arrayCount -= !IsEmpty(val);	//branchless
			val = EMPTY_VALUE;
		}
		else
			HashRemove(key);
	}

	//removes key by pointer to its value
	inline void RemovePtr(Value *ptr) {
		assert(ptr);
		assert(!IsEmpty(*ptr));
		if (InArray(ptr)) {
			arrayCount--;
			*ptr = EMPTY_VALUE;
		}
		else
			HashRemovePtr(ptr);
	}

	//returns key for the given value
	inline Key KeyOf(Value *ptr) const {
		assert(ptr);
		if (InArray(ptr))
			return Key(ptr - arrayValues.get());
		else {
			Size cell = ptr - hashValues.get();
			return hashKeys[cell];
		}
	}

	//force to allocate memory for at least given amount of elements (separately for array and hash parts)
	void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
		if (arraySizeLB || arraySize)
			arraySizeLB = std::max(Size(1) << log2up(arraySizeLB), std::max(arraySize, (Size)ARRAY_MIN_SIZE));
		if (hashSizeLB  ||  hashSize)
			hashSizeLB  = std::max(Size(1) << log2up( hashSizeLB), std::max( hashSize, (Size) HASH_MIN_SIZE));
		if (arraySizeLB == arraySize && hashSizeLB == hashSize && !alwaysCleanHash)
			return;
		Reallocate(arraySizeLB, hashSizeLB);
	}

	//perform given action for all the elements
	//callback is specified in format:
	//  bool action(Key key, Value &value);
	//it must return false to continue iteration, true to stop
	template<class Action> void ForEach(Action action) const {
		for (Size i = 0; i < arraySize; i++)
			if (!IsEmpty(arrayValues[i]))
				if (action(Key(i), arrayValues[i]))
					return;
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				if (action(Key(hashKeys[i]), hashValues[i]))
					return;
	}

	bool AssertCorrectness(int verbosity = 2) const {
		if (verbosity >= 0) {
			//check array/hash sizes
			assert(arraySize == 0 || arraySize >= ARRAY_MIN_SIZE);
			assert( hashSize == 0 ||  hashSize >=  HASH_MIN_SIZE);
			assert((arraySize & (arraySize - 1)) == 0);
			assert(( hashSize & ( hashSize - 1)) == 0);
			//check buffers
			assert(follows(arraySize == 0, !arrayValues.get()));
			assert(follows(arraySize != 0,  arrayValues.get()));
			assert(follows( hashSize == 0,  !hashValues.get() && !hashKeys.get()));
			assert(follows( hashSize != 0,   hashValues.get() &&  hashKeys.get()));
			//check hash fill ratio
			assert(hashFill <= HASH_MAX_FILL * hashSize);
		}
		if (verbosity >= 1) {
			//iterate over array
			Size trueArrayCount = 0;
			for (Size i = 0; i < arraySize; i++) {
				if (IsEmpty(arrayValues[i]))
					continue;
				trueArrayCount++;
			}
			assert(arrayCount == trueArrayCount);
			//iterate over hash table
			Size trueHashCount = 0, trueHashFill = 0;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				const Value &value = hashValues[i];
				assert(Size(key) >= arraySize);
				if (key != EMPTY_KEY)
					trueHashFill++;
				if (key != EMPTY_KEY && key != REMOVED_KEY) {
					trueHashCount++;
					assert(!IsEmpty(value));
				}
			}
			assert(hashCount == trueHashCount && hashFill == trueHashFill);
		}
		if (verbosity >= 2) {
			//hash verbose check: keys are unique
			std::set<Key> keys;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				assert(keys.count(key) == 0);
				keys.insert(key);
			}
			//hash verbose check: key placed near hash function value
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				Size cell = FindCellKeyOrEmpty(key);
				assert(hashKeys[cell] == key);
			}
		}
		return true;
	}

};

namespace std {
	void swap(ArrayHash &a, ArrayHash &b) {
		a.Swap(b);
	}
};

//========================================================================================

#include <map>
#include <unordered_map>

class StdMapWrapper {
//	typedef std::map<Key, Value> Map;
	typedef std::unordered_map<Key, Value> Map;
	typedef Map::iterator Iter;

	Map dict;
public:
	struct Ptr {
		bool null;
		Iter it;
		inline Ptr() : null(true) {}
		inline Ptr(Iter it) : null(false), it(it) {}
		inline operator bool() const { return !null; }
		inline bool operator!() const { return null; }
		inline Value &operator* () const { return it->second; }
		inline Value &operator-> () const { return it->second; }
	};

	inline void Swap(StdMapWrapper &other) {
		std::swap(dict, other.dict);
	}
	inline void Clear() {
		dict.clear();
	}
	inline Size GetSize() const {
		return dict.size();
	}
	inline Value Get(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? EMPTY_VALUE : it->second;
	}
	inline Ptr GetPtr(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? Ptr() : Ptr(it);
	}
	inline Ptr Set(Key key, Value value) {
		dict[key] = value;
		return Ptr(dict.find(key));
	}
	inline Ptr SetIfNew(Key key, Value value) {
		std::pair<Iter, bool> pib = dict.insert(std::make_pair(key, value));
		return pib.second ? Ptr() : Ptr(pib.first);
	}
	inline void Remove(Key key) {
		dict.erase(key);
	}
	inline void RemovePtr(Ptr ptr) {
		dict.erase(ptr.it);
	}
	inline Key KeyOf(Ptr ptr) const {
		return ptr.it->first;
	}
	void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {}

	template<class Action> void ForEach(Action action) const {
		for (Iter it = const_cast<Map&>(dict).begin(); it != const_cast<Map&>(dict).end(); it++)
			if (action(Key(it->first), it->second));
				return;
	}

	//for testing only
	template<class Rnd> Key SomeKey(Rnd &rnd) const {
		int idx = std::uniform_int_distribution<int>(0, dict.size() - 1)(rnd);
		Map::const_iterator iter = dict.begin();
		std::advance(iter, idx);
		return iter->first;
	}
};

//========================================================================================

#include <iostream>

class TestContainer {
	ArrayHash obj;
	StdMapWrapper check;

	static inline bool Same(Value a, Value b) {
		return a == b;
	}
	static inline bool Same(Value *a, StdMapWrapper::Ptr b) {
		if (!a == !b)
			return true;
		return Same(*a, *b);
	}

public:
	int assertLevel;
	bool printCommands;
	TestContainer() {
		//change in order to trade speed for internal checks verbosity
		assertLevel = 2;
		//chenge to true if you want to see a problematic test
		printCommands = false;
#ifdef NDEBUG
		printf("asserts must be enabled for test!");
		throw "asserts must be enabled for test!";
#endif
	}

	Size GetSize() const {
		if (printCommands) std::cout << "GetSize" << std::endl;
		Size a = obj.GetSize();
		Size b = check.GetSize();
		assert(a == b);
		return a;
	}
	Value Get(Key key) const {
		if (printCommands) std::cout << "Get " << key << std::endl;
		Value a = obj.Get(key);
		Value b = check.Get(key);
		assert(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *GetPtr(Key key) const {
		if (printCommands) std::cout << "GetPtr " << key << std::endl;
		Value *a = obj.GetPtr(key);
		StdMapWrapper::Ptr b = check.GetPtr(key);
		assert(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *Set(Key key, Value value) {
		if (printCommands) std::cout << "Set " << key << " " << value << std::endl;
		Value *a = obj.Set(key, value);
		StdMapWrapper::Ptr b = check.Set(key, value);
		assert(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *SetIfNew(Key key, Value value) {
		if (printCommands) std::cout << "SetIfNew " << key << " " << value << std::endl;
		Value *a = obj.SetIfNew(key, value);
		StdMapWrapper::Ptr b = check.SetIfNew(key, value);
		assert(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	void Remove(Key key) {
		if (printCommands) std::cout << "Remove " << key << std::endl;
		obj.Remove(key);
		check.Remove(key);
		obj.AssertCorrectness(assertLevel);
	}
	void RemovePtr(Value *ptr) {
		if (printCommands) std::cout << "RemovePtr " << ptr << std::endl;
		Key key = obj.KeyOf(ptr);
		obj.RemovePtr(ptr);
		check.RemovePtr(check.GetPtr(key));
		obj.AssertCorrectness(assertLevel);
	}
	Key KeyOf(Value *ptr) const {
		if (printCommands) std::cout << "KeyOf " << ptr << std::endl;
		Key a = obj.KeyOf(ptr);
		Key b = check.KeyOf(check.GetPtr(a));
		assert(a == b);
	}
	void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
		if (printCommands) std::cout << "Reserve " << arraySizeLB << " " << hashSizeLB << " " << alwaysCleanHash << std::endl;
		obj.Reserve(arraySizeLB, hashSizeLB, alwaysCleanHash);
		check.Reserve(arraySizeLB, hashSizeLB, alwaysCleanHash);
		obj.AssertCorrectness(assertLevel);
	}
	void Swap(TestContainer &other) {
		if (printCommands) std::cout << "Swap" << std::endl;
		obj.Swap(other.obj);
		check.Swap(other.check);
		obj.AssertCorrectness(assertLevel);
		other.obj.AssertCorrectness(assertLevel);
	}
	void Clear() {
		if (printCommands) std::cout << "Clear" << std::endl;
		obj.Clear();
		check.Clear();
		obj.AssertCorrectness(assertLevel);
	}
	Key CalcCheckSum() const {
		Key sum;
		auto Add = [&sum](Key key, Value &value) -> bool {
			sum += key * 10 + Key(value);
			return false;
		};
		sum = 0;
		obj.ForEach(Add);
		Key a = sum;
		sum = 0;
		check.ForEach(Add);
		Key b = sum;
		assert(a == b);
		return a;
	}

	//for testing only
	template<class Rnd> Value *SomePtr(Rnd &rnd) const {
		return obj.GetPtr(check.SomeKey(rnd));
	}
};

//========================================================================================

#include <vector>
#include <numeric>
#include <ctime>
#include <random>

#include "timer.h"
inline double my_clock() {
	static bool initialized = false;
	if (!initialized) {
		timer_lib_initialize();
		initialized = true;
	}
	return timer_current() * (1000.0 / double(timer_ticks_per_second()));
}

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

	printf("TestRandom: %d opers, keys in [%d, %d]\n", operationsCount, minKey, maxKey);
	printf("     probs: %s\n", signature.c_str());
	fflush(stdout);

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

void TestsRound(std::mt19937 &rnd) {
	{
		TestContainer dict;
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1000, -100, 100, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01, 0.01, 0.01, 0.01}, 1000, -10, 10, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 2000, -100, 100, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 0.01}, 1000, 0, 100, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {1, 1, 1, 1, 1, 1, 1, 1}, 1000, -50, 50, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {1, 50, 50, 1, 1, 1, 1, 1}, 1000, -10, 10, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, -100, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, -120, 120, rnd);
	}
	{
		TestContainer dict;
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 0, 100, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 0.1, 0.01, 0}, 1000, 100, 300, rnd);
		TestRandom(dict, {0, 1, 1, 1, 1, 1, 1, 0}, 1000, 0, 500, rnd);
	}
	{
		TestContainer dict;
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
			sum &= (size_t)cont.GetPtr(keys[j]);
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

int main() {
	SpeedAll();

	std::mt19937 rnd;
	while (1) TestsRound(rnd);

	while (1) {
		double q = Speed_GrowthHashRandom<ArrayHash>(100000, 100);
		std::cout << q;
	}

	return 0;
}

