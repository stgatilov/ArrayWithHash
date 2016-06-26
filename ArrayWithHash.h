#pragma once

#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include <memory>
#ifdef AWH_TESTING
#include <set>   //used only in AssertCorrectness
#endif

#include "ArrayWithHash_Utils.h"
#ifndef AWH_NO_CPP11
#include "ArrayWithHash_Traits.h"
#endif


//minimal allowed fill ratio of array part on automatic reallocation
static const double ARRAY_MIN_FILL = 0.45;
//minimal allowed fill ratiu of hash table part on automatic reallocation
static const double HASH_MIN_FILL = 0.30;
//maximal allowed fill ratio of hash table part ever (next insert -> reallocation)
static const double HASH_MAX_FILL = 0.75;
//minimal size of non-empty array part
static const size_t ARRAY_MIN_SIZE = 8;
//minimal size of non-empty hash part
static const size_t HASH_MIN_SIZE = 8;
//fast check for reaching HASH_MAX_FILL ratio (without float arithmetics)
template<class Size> static AWH_INLINE bool IsHashFull(Size cfill, Size sz) {
	return cfill >= ((sz >> 2) * 3);
}

//array with hash table backup = hash table with array optimization
//Key type must be an integer (32-bit or 64-bit are advised).
//Value type can be anything: integer, real, pointer, smart pointer, string, ...
template<
	class TKey, class TValue,
#ifndef AWH_NO_CPP11
	class TKeyTraits = DefaultKeyTraits<TKey>, class TValueTraits = DefaultValueTraits<TValue>
#else
	class TKeyTraits, class TValueTraits	//note: without C++11, user has to always specify traits
#endif
>
class ArrayWithHash {
	//accessing template arguments from outside
	typedef TKey Key;
	typedef TValue Value;
	typedef TKeyTraits KeyTraits;
	typedef TValueTraits ValueTraits;
	//default unsigned integer type
	typedef typename KeyTraits::Size Size;

private:
	//pseudonyms for making code more readable
	static const Key EMPTY_KEY = KeyTraits::EMPTY_KEY;
	static const Key REMOVED_KEY = KeyTraits::REMOVED_KEY;

	//array part: total size = maximal number of elements (power of two or zero)
	Size arraySize;
	//array part: number of valid elements
	Size arrayCount;
	//hash part: total number of cells (power of two or zero)
	Size hashSize;
	//hash part: number of valid elements
	Size hashCount;
	//hash part: number of cells used (including those tagged as REMOVED)
	Size hashFill;
	//array part: pointer to buffer
	Value *arrayValues;
	//hash part: pointer to buffer with values only
	Value *hashValues;
	//hash part: pointer to buffer with keys only
	Key *hashKeys;
	//Note: i-th cell of hash table is (hashKeys[i], hashValues[i])


	//routines used for memory allocation/deallocation
	//Note: malloc/free are used in order to take benefits of realloc
	template<class Elem> static Elem* AllocateBuffer(Size elemCount) {
		if (elemCount == 0)
			return NULL;
		return (Elem*) malloc (size_t(elemCount) * sizeof(Elem));
	}
	template<class Elem> static void DeallocateBuffer(Elem *buffer) {
		free(buffer);
	}

	//relocate single value from alive src to dead dst
	//after relocation: src is dead, dst is alive
	static AWH_INLINE void RelocateOne(Value &dst, Value &src) {
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			memcpy(&dst, &src, sizeof(Value));
		else {
			new (&dst) Value(AWH_MOVE(src));
			src.~Value();
		}
	}
	//relocate array of values
	static void RelocateMany(Value *dst, Value *src, Size cnt) {
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			memcpy(dst, src, size_t(cnt) * sizeof(Value));
		else {
			for (Size i = 0; i < cnt; i++) {
				new (&dst[i]) Value(AWH_MOVE(src[i]));
				src[i].~Value();
			}
		}
	}

	//checks whether given key belongs to the array part
	AWH_INLINE bool InArray(Key key) const {
		//one comparison is used for signed numbers too:
		//  http://stackoverflow.com/a/17095534/556899
		return Size(key) < arraySize;
	}
	//checks whether given value belongs to the array part
	AWH_INLINE bool InArray(Value *ptr) const {
		size_t offset = (char*)ptr - (char*)arrayValues;
		return offset < size_t(arraySize) * sizeof(Value);
	}

	//calculate hash function for given key and resolve collision by linear probing
	//returns the first EMPTY cell found (caller must ensure that key is not yet present)
	//used only in internal relocation methods
	AWH_INLINE Size FindCellEmpty(Key key) const {
		assert(hashSize);
		Size cell = KeyTraits::HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}
	//returns the first cell which is EMPTY of contains specified key
	//used in all user-called methods
	AWH_INLINE Size FindCellKeyOrEmpty(Key key) const {
		assert(hashSize);
		Size cell = KeyTraits::HashFunction(key) & (hashSize - 1);
		while (hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != key)
			cell = (cell + 1) & (hashSize - 1);
		return cell;
	}

	//resize array and hash parts due to hash table fill ratio maximized
	//newKey parameter is the new key to be inserted right after resizing
	//the new sizes are chosen so that both the old keys and the new one fit
	AWH_NOINLINE void AdaptSizes(Key newKey) {
		static const int BITS = sizeof(Size) * 8;
		//logHisto[t] = number of keys in range [2^(t-1); 2^t - 1]
		Size logHisto[BITS + 1] = {0};
		Size logArraySize = log2up(arraySize);

		//=== populate logHisto histogram with all the valid elements ===
		logHisto[logArraySize] = arrayCount;	//elements in array part
		logHisto[log2size(newKey)]++;			//to-be-inserted element
		for (Size i = 0; i < hashSize; i++) {
			//Note: only elements in hash table part are processed
			Key key = hashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			//valid element: increment histogram count
			Size keyBits = log2size((Size)key);
			assert(keyBits >= logArraySize);
			logHisto[keyBits]++;
		}

		//=== choose appropriate size for the array part ===
		Size newArraySize = 0, newArrayCount = 0;
		//note: array cannot be shrink, and it cannot be too small
		Size lowerBound = std::max(arraySize, (Size)ARRAY_MIN_SIZE);
		Size prefSum = 0;
		for (Size i = logArraySize; i < BITS; i++) {
			//prefSum is number of elements less than 2^i
			prefSum += logHisto[i];
			Size aSize = 1 << i;
			//array must have enough fill ratio for any viable size
			Size required = Size(ARRAY_MIN_FILL * aSize);
			if (aSize <= lowerBound || prefSum >= required) {
				//maximal array size is chosen among viable options
				newArraySize = aSize;
				newArrayCount = prefSum;
			}
			else if (arrayCount + hashCount + 1 < required)
				break;	//this size and greater are surely not viable
		}
		//if still no element is in the array part, then do not create it
		if (arraySize == 0 && newArrayCount == 0)
			newArraySize = 0;

		//=== choose appropriate size for the hash table part ===
		Size newHashCount = arrayCount + hashCount - newArrayCount + 1;
		//hash table part cannot shrink, and it cannot be too small
		Size newHashSize = std::max(hashSize, (Size)HASH_MIN_SIZE);
		//increase hash size as long as hash fill ratio does not drop too small
		while (newHashCount >= HASH_MIN_FILL * newHashSize * 2)
			newHashSize *= 2;
		//if still no element is in the hash table part, then do not create it
		if (hashSize == 0 && newHashCount == 0)
			newHashSize = 0;

		//physically relocate all the data
		Reallocate(newArraySize, newHashSize);
	}

	//reallocate the array part of the data structure
	//newArraySize is the desired new size of the array
	AWH_NOINLINE void RelocateArrayPart(Size newArraySize) {
		Value *newArrayValues;
		if (ValueTraits::RELOCATE_WITH_MEMCPY)
			//values are marked as trivially relocatable: realloc can be used
			newArrayValues = (Value*) realloc(arrayValues, size_t(newArraySize) * sizeof(Value));
		else {
			//allocate new buffer
			newArrayValues = AllocateBuffer<Value>(newArraySize);
			//relocate all elements to it
			RelocateMany(newArrayValues, arrayValues, arraySize);
			//free old buffer
			DeallocateBuffer<Value>(arrayValues);
		}
		//upper part of the array is still dead (i.e. raw, not constructed)
		//we construct all these elements with EMPTY value
		for (Value *ptr = newArrayValues + arraySize; ptr < newArrayValues + newArraySize; ptr++)
			new (ptr) Value(ValueTraits::GetEmpty());

		//save the new array
		arrayValues = newArrayValues;
		arraySize = newArraySize;
	}

	//perform a single pass over hash table in order to:
	// 1. clean, i.e. eliminate all REMOVED entries
	// 2. move some elements into array part (if RELOC_ARRAY is true)
	template<bool RELOC_ARRAY> AWH_NOINLINE void RelocateHashInPlace(Size newArraySize) {
		//reallocate the array part (if required)
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//hash is empty, no action required
		if (hashSize == 0)
			return;
		Size totalCount = arrayCount + hashCount;
		
		//find first empty cell
		//it must be present because hash fill ratio is bounded
		Size firstEmpty = 0;
		while (hashKeys[firstEmpty] != EMPTY_KEY)
			firstEmpty++;

		//do a full round over the hash table
		//note: iteration is started from the first empty cell
		//as a result, each group of non-empty elements is enumerated in order
		Size pos = firstEmpty;
		do {
			Key key = hashKeys[pos];
			//mark key as empty, even if it is valid,
			//allowing it to be found by FindCellEmpty later
			hashKeys[pos] = EMPTY_KEY;

			if (key != EMPTY_KEY && key != REMOVED_KEY) {
				//valid element: must be relocated
				Value &value = hashValues[pos];
				if (RELOC_ARRAY && InArray(key)) {
					//fits into expanded array part
					//note: destination must be killed before relocation
					arrayValues[key].~Value();
					RelocateOne(arrayValues[key], value);
					arrayCount++;
				}
				else {
					//must be retained in the hash table part
					//in order to find its new place, insert it as usual
					Size cell = FindCellEmpty(key);
					hashKeys[cell] = key;
					//relocate element's value (only if its cell has changed)
					if (cell != pos)
						RelocateOne(hashValues[cell], value);
				}
			}

			//go to the next cell (cyclically)
			pos = (pos + 1) & (hashSize - 1);
		} while (pos != firstEmpty);

		//if necessary, update hash table count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//all the REMOVED entries were dropped, forget them
		hashFill = hashCount;
	}

	//reallocate the hash table part, doing the following in process:
	// 1. clean, i.e. eliminate all REMOVED entries
	// 2. move some elements into array part (if RELOC_ARRAY is true)
	template<bool RELOC_ARRAY> AWH_NOINLINE void RelocateHashToNew(Size newHashSize, Size newArraySize) {
		//reallocate the array part (if required)
		if (RELOC_ARRAY)
			RelocateArrayPart(newArraySize);

		//create new buffers for the hash table
		Key *newHashKeys = AllocateBuffer<Key>(newHashSize);
		//note: fill keys buffer with EMPTY key
		std::uninitialized_fill_n(newHashKeys, newHashSize, EMPTY_KEY);
		Value *newHashValues = AllocateBuffer<Value>(newHashSize);
		//note: leave values buffer raw (i.e. no elements constructed)

		//install the new buffers into "this" object
		std::swap(hashKeys, newHashKeys);
		std::swap(hashValues, newHashValues);
		std::swap(hashSize, newHashSize);
		//Note: newXXX are now actually old values

		Size totalCount = arrayCount + hashCount;
		//iterate over all elements in the old hash table (and relocate them)
		for (Size i = 0; i < newHashSize; i++) {
			Key key = newHashKeys[i];
			if (key == EMPTY_KEY || key == REMOVED_KEY)
				continue;
			//valid key found, must be inserted into the new hash table
			Value &value = newHashValues[i];
			if (RELOC_ARRAY && InArray(key)) {
				//fits into expanded array part
				//note: destination must be killed before relocation
				arrayValues[key].~Value();
				RelocateOne(arrayValues[key], value);
				arrayCount++;
			}
			else {
				//must be inserted into the new hash table
				Size cell = FindCellEmpty(key);
				hashKeys[cell] = key;
				RelocateOne(hashValues[cell], value);
			}
		}

		//free the old hash table buffers
		//note: the keys are integers, so no destruction is necessary for them
		DeallocateBuffer<Key>(newHashKeys);
		//note: values buffer is not raw, since all the valid values have been relocated
		DeallocateBuffer<Value>(newHashValues);

		//if necessary, update hash table count
		if (RELOC_ARRAY)
			hashCount = totalCount - arrayCount;
		//all the REMOVED entries were dropped, forget them
		hashFill = hashCount;
	}

	//reallocate array and hash table parts with given sizes
	//called internally in two cases: automatic reallocation, Reserve method
	AWH_NOINLINE void Reallocate(Size newArraySize, Size newHashSize) {
		assert(newArraySize >= arraySize && newHashSize >= hashSize);

		if (newHashSize == hashSize) {
			if (newArraySize == arraySize)
				//both sizes has not changed: just clean the hash
				RelocateHashInPlace<false>(newArraySize);
			else
				//only array size has changed: clean the hash and filter new array elements
				RelocateHashInPlace<true>(newArraySize);
		}
		else {
			if (newArraySize == arraySize)
				//only hash size has changed: reallocate and clean it
				RelocateHashToNew<false>(newHashSize, newArraySize);
			else
				//both sizes have changed: reallocate them, clean the hash and filter new array elements
				RelocateHashToNew<true>(newHashSize, newArraySize);
		}
	}

	//======================================================================
	//Each simple public method can operate either on array or on hash table.
	//Here the methods are implemented for the case they operate on hash part.
	//
	//Since operations on hash part are not so critical for performance,
	//they are marked as NOINLINE (when AWH_CONTROL_INLINING is enabled).

	AWH_NOINLINE Value HashGet(Key key) const {
		//check for null required: FindCellXXX hangs otherwise
		if (hashSize == 0)
			return ValueTraits::GetEmpty();
		//find cell with the key (or first empty cell if not present)
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? ValueTraits::GetEmpty() : hashValues[cell];
	}

	//(almost the same as HashGet)
	AWH_NOINLINE Value *HashGetPtr(Key key) const {
		if (hashSize == 0)
			return NULL;
		Size cell = FindCellKeyOrEmpty(key);
		return hashKeys[cell] == EMPTY_KEY ? NULL : &hashValues[cell];
	}

	AWH_NOINLINE Value *HashSet(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			//fill ratio of hash part is at its allowed maximum
			//reallocation may be necessary to finish the operation
			AdaptSizes(key);
			//perform the operation after reallocation
			//note: we cannot just do HashGet here,
			//because the element may now go into the array part
			return Set(key, AWH_MOVE(value));
		}
		//find cell with the key (or first empty cell if not present)
		//note: hash table cannot be null, since IsHashFull returns true in such case
		Size cell = FindCellKeyOrEmpty(key);
		//check if the key is new
		bool newElement = (hashKeys[cell] == EMPTY_KEY);
		//update fill/count counters
		hashFill += newElement;
		hashCount += newElement;
		//save the key
		hashKeys[cell] = key;
		//if the element is already present, we have to destroy its value
		//otherwise destination is already dead (i.e. not constructed)
		if (!newElement)
			hashValues[cell].~Value();
		//move-construct the value in hash table from parameter
		new (&hashValues[cell]) Value(AWH_MOVE(value));
		//return pointer to the updated value
		return &hashValues[cell];
	}

	//(very similar to HashSet)
	AWH_NOINLINE Value *HashSetIfNew(Key key, Value value) {
		if (IsHashFull(hashFill, hashSize)) {
			//fill ratio is capped: reallocate and proceed as usual
			AdaptSizes(key);
			return SetIfNew(key, AWH_MOVE(value));
		}
		Size cell = FindCellKeyOrEmpty(key);
		//if the element is not new, then simply return pointer to it
		if (hashKeys[cell] != EMPTY_KEY)
			return &hashValues[cell];
		//the element is new: insert as usual
		hashFill++;
		hashCount++;
		hashKeys[cell] = key;
		new (&hashValues[cell]) Value(AWH_MOVE(value));
		return NULL;
	}

	AWH_NOINLINE void HashRemove(Key key) {
		//check for null required: FindCellXXX hangs otherwise
		if (hashSize == 0)
			return;
		//find cell with the key (or first empty cell if not present)
		Size cell = FindCellKeyOrEmpty(key);
		//if key was not found, then do nothing
		if (hashKeys[cell] == EMPTY_KEY)
			return;
		//mark cell of hash table as REMOVED
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		//destroy value of the removed element
		hashValues[cell].~Value();
	}

	AWH_NOINLINE void HashRemovePtr(Value *ptr) {
		//determine cell index
		size_t cell = ptr - &hashValues[0];
		assert(hashKeys[cell] != EMPTY_KEY && hashKeys[cell] != REMOVED_KEY);
		//mark cell of hash table as REMOVED
		hashKeys[cell] = REMOVED_KEY;
		hashCount--;
		//destroy value of the removed element
		hashValues[cell].~Value();
	}

	//======================================================================

	//initialize all members of this object to empty state
	AWH_INLINE void Flush() {
		arraySize = 0;
		hashSize = 0;
		arrayCount = 0;
		hashCount = 0;
		hashFill = 0;
		arrayValues = NULL;
		hashValues = NULL;
		hashKeys = NULL;
	}
	//copy all members of this object from source object
	AWH_INLINE void RelocateFrom(const ArrayWithHash &iSource) {
		arraySize = iSource.arraySize;
		hashSize = iSource.hashSize;
		arrayCount = iSource.arrayCount;
		hashCount = iSource.hashCount;
		hashFill = iSource.hashFill;
		arrayValues = iSource.arrayValues;
		hashValues = iSource.hashValues;
		hashKeys = iSource.hashKeys;
	}
	//call destructor for all the values still alive in hash table part
	//used for whole-object clearing
	AWH_INLINE void DestroyAllHashValues() {
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				hashValues[i].~Value();
	}

	//note: ArrayWithHash is non-copyable
	//on the other hand, it is movable and swappable (both in O(1))
	ArrayWithHash (const ArrayWithHash &iSource);
	void operator= (const ArrayWithHash &iSource);

public:
	ArrayWithHash() {
		//set this to empty state
		Flush();
	}
	AWH_NOINLINE ~ArrayWithHash() {
		//call destructor for all values in array part
		for (Size i = 0; i < arraySize; i++)
			arrayValues[i].~Value();
		//call destructor for all alive values in hash table part
		DestroyAllHashValues();
		//free all buffers
		DeallocateBuffer<Value>(arrayValues);
		DeallocateBuffer<Value>(hashValues);
		DeallocateBuffer<Key>(hashKeys);
	}

#ifndef AWH_NO_CPP11
	//container is easily movable in C++11
	//note: source object is reset to empty state
	ArrayWithHash(ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}
	void operator= (ArrayWithHash &&iSource) {
		RelocateFrom(iSource);
		iSource.Flush();
	}
#endif

	//fast O(1) swap of this object and another one
	void Swap(ArrayWithHash &other) {
		//implemented memberwise
		std::swap(arraySize, other.arraySize);
		std::swap(arrayCount, other.arrayCount);
		std::swap(hashSize, other.hashSize);
		std::swap(hashCount, other.hashCount);
		std::swap(hashFill, other.hashFill);
		std::swap(arrayValues, other.arrayValues);
		std::swap(hashValues, other.hashValues);
		std::swap(hashKeys, other.hashKeys);
	}

	//remove all elements from container without shrinking
	//note: if you want to free resources, use the well-known swap hack:
	//  container.Swap(ArrayWithHash<...>());
	AWH_NOINLINE void Clear() {
		//note: if array is already empty, no action is required
		if (arraySize && arrayCount) {
			//make all values EMPTY
			for (Size i = 0; i < arraySize; i++)
				arrayValues[i] = AWH_MOVE(ValueTraits::GetEmpty());
		}
		//note: if hash table is already empty, no action is required
		if (hashSize && hashFill) {
			//destroy all the alive values of valid elements
			DestroyAllHashValues();
			//make all cells empty
			std::fill_n(hashKeys, hashSize, EMPTY_KEY);
		}
		//reset all element counters
		arrayCount = hashCount = hashFill = 0;
	}

	//return number of elements currently inside
	AWH_INLINE Size GetSize() const {
		return arrayCount + hashCount;
	}

	//return value for given key, or EMPTY value if the key is not present
	//note: Value must be copyable, otherwise this method won't compile 
	//you can use GetPtr in case of non-copyable values
	AWH_INLINE Value Get(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key))
			//note: non-present values are already in EMPTY state in the array part
			return arrayValues[key];
		else
			return HashGet(key);
	}

	//return pointer to the value for a given key, or NULL if key is not present
	AWH_INLINE Value *GetPtr(Key key) const {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			return ValueTraits::IsEmpty(val) ? NULL : &val;	//branchless
		}
		else
			return HashGetPtr(key);
	}

	//set the value associated with the given key
	//the key is inserted if not present before
	//returns pointer to the updated/inserted value
	AWH_INLINE Value *Set(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!ValueTraits::IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			arrayCount += ValueTraits::IsEmpty(oldVal);	//branchless
			oldVal = AWH_MOVE(value);
			return &oldVal;
		}
		else
			return HashSet(key, AWH_MOVE(value));
	}

	//if key is present, then returns pointer to it
	//otherwise inserts a new key with associated value, and returns NULL
	AWH_INLINE Value *SetIfNew(Key key, Value value) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		assert(!ValueTraits::IsEmpty(value));
		if (InArray(key)) {
			Value &oldVal = arrayValues[key];
			if (ValueTraits::IsEmpty(oldVal)) {					//real branch
				oldVal = AWH_MOVE(value);
				arrayCount++;
				return NULL;
			}
			else
				return &oldVal;
			//is branchless version worth it?
/*			Value *pOldVal = &arrayValues[key];		
			Value stored = *pOldVal;
			bool empty = ValueTraits::IsEmpty(stored);
			stored = (empty ? value : stored);
			arrayCount += empty;
			*pOldVal = stored;
			return empty ? NULL : pOldVal;*/
		}
		else
			return HashSetIfNew(key, AWH_MOVE(value));
	}

	//remove element with the given key (if present)
	AWH_INLINE void Remove(Key key) {
		assert(key != EMPTY_KEY && key != REMOVED_KEY);
		if (InArray(key)) {
			Value &val = arrayValues[key];
			arrayCount -= !ValueTraits::IsEmpty(val);	//branchless
			//note: value is reset to EMPTY state in the array part
			val = ValueTraits::GetEmpty();
		}
		else
			HashRemove(key);
	}

	//remove element specified by pointer to its value
	AWH_INLINE void RemovePtr(Value *ptr) {
		assert(ptr);
		assert(!ValueTraits::IsEmpty(*ptr));
		if (InArray(ptr)) {
			arrayCount--;
			//reset value to EMPTY state
			*ptr = ValueTraits::GetEmpty();
		}
		else
			HashRemovePtr(ptr);
	}

	//get key for the given value pointer
	//this method allows to use value pointers as iterators
	AWH_INLINE Key KeyOf(Value *ptr) const {
		assert(ptr);
		if (InArray(ptr))
			return Key(ptr - arrayValues);
		else {
			size_t cell = ptr - hashValues;
			return hashKeys[cell];
		}
	}

	//force to reserve some memory for both array and hash table parts
	//  arraySizeLB: lower bound on number of elements in the array part
	//   hashSizeLB: lower bound on number of cells in the hash table part
	//note: maximal number of elements that can fit into hash table is smaller than number of cells
	//note: array and hash table must have power-of-two sizes, so you values would be rounded up
	//if alwaysCleanHash is true, then hash table would be cleaned even if no reallocation is necessary
	AWH_NOINLINE void Reserve(Size arraySizeLB, Size hashSizeLB, bool alwaysCleanHash = false) {
		//note: both parts remain of zero size if possible
		if (arraySizeLB || arraySize)
			arraySizeLB = std::max(Size(Size(1) << log2up(arraySizeLB)), std::max(arraySize, (Size)ARRAY_MIN_SIZE));
		if (hashSizeLB  ||  hashSize)
			hashSizeLB  = std::max(Size(Size(1) << log2up( hashSizeLB)), std::max( hashSize, (Size) HASH_MIN_SIZE));
		//not necessary to run reallocation of sizes do not change (unless forced to clean hash)
		if (arraySizeLB == arraySize && hashSizeLB == hashSize && !alwaysCleanHash)
			return;
		//perform reallocation and related tasks
		Reallocate(arraySizeLB, hashSizeLB);
	}

	//perform given action for all the elements in this container
	//callback is specified as a functor with signature:
	//  bool action(Key key, Value &value);
	//it must return: false to continue iteration, true to stop it
	//order of traversal may be arbitrary in general
	template<class Action> void ForEach(Action &action) const {
		//note: array part is traversed first since it is faster
		for (Size i = 0; i < arraySize; i++)
			if (!ValueTraits::IsEmpty(arrayValues[i]))
				if (action(Key(i), arrayValues[i]))
					return;
		for (Size i = 0; i < hashSize; i++)
			if (hashKeys[i] != EMPTY_KEY && hashKeys[i] != REMOVED_KEY)
				if (action(Key(hashKeys[i]), hashValues[i]))
					return;
	}

#ifdef AWH_TESTING
	//internal method: checks all the invariants of the container
	//it is not called from anywhere (except tests), and you should not call it too
	//
	//verbosity allows to regulate how slow and detailed the check is:
	//  0: perform only O(1) checks
	//  1: also perform O(N) fast checks
	//  2: perform all checks, including creating a temporary std::set
	AWH_NOINLINE bool AssertCorrectness(int verbosity = 2) const {
		if (verbosity >= 0) {
			//each part either is null (size = 0) or has size capped from below
			AWH_ASSERT_ALWAYS(arraySize == 0 || arraySize >= ARRAY_MIN_SIZE);
			AWH_ASSERT_ALWAYS( hashSize == 0 ||  hashSize >=  HASH_MIN_SIZE);
			//each size is always power of two
			AWH_ASSERT_ALWAYS((arraySize & (arraySize - 1)) == 0);
			AWH_ASSERT_ALWAYS(( hashSize & ( hashSize - 1)) == 0);
			//if part has zero size, then its buffers must be null
			//otherwise, buffers must be non-null
			AWH_ASSERT_ALWAYS(follows(arraySize == 0, !arrayValues));
			AWH_ASSERT_ALWAYS(follows(arraySize != 0,  arrayValues));
			AWH_ASSERT_ALWAYS(follows( hashSize == 0,  !hashValues && !hashKeys));
			AWH_ASSERT_ALWAYS(follows( hashSize != 0,   hashValues &&  hashKeys));
			//fill ratio in the hash table part must never exceed hard-coded cap
			AWH_ASSERT_ALWAYS(hashFill <= HASH_MAX_FILL * hashSize);
		}

		if (verbosity >= 1) {
			//count real number of elements in the array part
			Size trueArrayCount = 0;
			for (Size i = 0; i < arraySize; i++) {
				if (ValueTraits::IsEmpty(arrayValues[i]))
					continue;
				trueArrayCount++;
			}
			//check array count value
			AWH_ASSERT_ALWAYS(arrayCount == trueArrayCount);

			//count number of valid elements and number of non-empty cells in hash
			Size trueHashCount = 0, trueHashFill = 0;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				//small keys must always be located in the array part
				AWH_ASSERT_ALWAYS(Size(key) >= arraySize);

				if (key != EMPTY_KEY)
					trueHashFill++;
				if (key != EMPTY_KEY && key != REMOVED_KEY) {
					//cell is occupied by a valid element
					trueHashCount++;
					const Value &value = hashValues[i];
					//its value must be alive and non-empty
					AWH_ASSERT_ALWAYS(!ValueTraits::IsEmpty(value));
				}
				else {
					//hashValues[i] must be dead (i.e. not constructed) in this case
				}
			}
			//check the evaluated counters
			AWH_ASSERT_ALWAYS(hashCount == trueHashCount && hashFill == trueHashFill);
		}

		if (verbosity >= 2) {
			//use STL container to check that all keys in the hash table are unique
			std::set<Key> keys;
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				AWH_ASSERT_ALWAYS(keys.count(key) == 0);
				keys.insert(key);
			}
			//check that each element in hash table is reachable
			//from its base cell by linear probing
			for (Size i = 0; i < hashSize; i++) {
				Key key = hashKeys[i];
				if (key == EMPTY_KEY || key == REMOVED_KEY)
					continue;
				//valid element, run a search of it in the hash table	
				Size cell = FindCellKeyOrEmpty(key);
				//the search must have sucessfully found the element
				AWH_ASSERT_ALWAYS(hashKeys[cell] == key);
			}
		}

		//return value only for convenience
		return true;
	}
#endif
};

//make sure std::swap works via Swap method
//theoretically, it is also ok to swap by default with three moves (unless C++11 is disabled)
namespace std {
	template<class Key, class Value, class KeyTraits, class ValueTraits>
	AWH_INLINE void swap(
		ArrayWithHash<Key, Value, KeyTraits, ValueTraits> &a,
		ArrayWithHash<Key, Value, KeyTraits, ValueTraits> &b
	) {
		a.Swap(b);
	}
};
