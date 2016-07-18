#pragma once

#include <iostream>
#include "ArrayWithHash.h"
#include "StdMapWrapper.h"
#include <memory>
#include <string>

using namespace Awh;

template<class Value> typename std::enable_if<std::is_integral<Value>::value, Value>::type
UniDistrRandom(std::mt19937 &rnd) {
  if (std::is_signed<Value>::value)
	  return (Value)std::uniform_int_distribution<int>(-10000, 10000)(rnd);
  else
	  return (Value)std::uniform_int_distribution<int>(0, 20000)(rnd);
}
template<class Value> typename std::enable_if<std::is_floating_point<Value>::value, Value>::type
UniDistrRandom(std::mt19937 &rnd) {
	return std::uniform_real_distribution<Value>(Value(-1e+5), Value(1e+5))(rnd);
}

template<class Value> struct BaseValueTestingUtils {
	static Value Generate(std::mt19937 &rnd) {
		return UniDistrRandom<Value>(rnd);
	}
	static Value Clone(const Value &src) {
		return Value(src);
	}
	static bool AreEqual(const Value &a, const Value &b) {
		return a == b;
	}
	static const Value &Content(const Value &a) {
		return a;
	}
	static int64_t CheckSum(const Value &a) {
		return (int64_t)Content(a);
	}
};

template<class Value> struct ValueTestingUtils : public BaseValueTestingUtils<Value> {};

template<class Value> struct ValueTestingUtils<std::unique_ptr<Value>> {
	std::unique_ptr<Value> operator() (std::mt19937 &rnd) const {
	}
	static std::unique_ptr<Value> Generate(std::mt19937 &rnd) {
		return std::unique_ptr<Value>(new Value(ValueTestingUtils<Value>::Generate(rnd)));
	}
	static std::unique_ptr<Value> Clone(const std::unique_ptr<Value> &src) {
		return std::unique_ptr<Value>(new Value(ValueTestingUtils<Value>::Clone(*src)));
	}
	static bool AreEqual(const std::unique_ptr<Value> &a, const std::unique_ptr<Value> &b) {
		return *a == *b;
	}
	static const Value &Content(const std::unique_ptr<Value> &a) {
		return *a;
	}
	static int64_t CheckSum(const std::unique_ptr<Value> &a) {
		return (int64_t)Content(a);
	}
};

template<class Value> struct ValueTestingUtils<std::shared_ptr<Value>> : public BaseValueTestingUtils<std::shared_ptr<Value>> {
	static std::shared_ptr<Value> Generate(std::mt19937 &rnd) {
		if (std::uniform_int_distribution<int>(0, 1)(rnd))
			return std::shared_ptr<Value>(new Value(ValueTestingUtils<Value>::Generate(rnd)));
		else
			return std::make_shared<Value>(ValueTestingUtils<Value>::Generate(rnd));
	}
	static size_t Content(const std::shared_ptr<Value> &a) {
		return size_t(a.get());
	}
	static int64_t CheckSum(const std::shared_ptr<Value> &a) {
		return size_t(a.get()) + size_t(*a);
	}
};

template<> struct ValueTestingUtils<std::string> : public BaseValueTestingUtils<std::string> {
	static std::string Generate(std::mt19937 &rnd) {
		int len = std::uniform_int_distribution<int>(1, 30)(rnd);
		std::string res(len, 0);
		for (int i = 0; i < len; i++)
			res[i] = (char)std::uniform_int_distribution<int>(32, 126)(rnd);
		return res;
	}
	static std::string Content(const std::string &a) {
		return a;
	}
	static int64_t CheckSum(const std::string &a) {
		return std::hash<std::string>()(a);
	}
};

template<class Value> struct ValueTestingUtils<Value*> : public BaseValueTestingUtils<Value*> {
	typedef typename std::conditional<std::is_same<Value, void>::value, char, Value>::type Elem;
	static Elem *GetBuffer() {
		static Elem tmp_buffer[10000];
		return tmp_buffer;
	}
	static Value *Generate(std::mt19937 &rnd) {
		return &GetBuffer()[std::uniform_int_distribution<int>(0, 9999)(rnd)];
	}
};

//Testing wrapper around both ArrayHash and StdMapWrapper.
//It checks that all the outputs of all method calls are the same.
//Used only for testing purposes
template<class TKey, class TValue, class TKeyTraits = DefaultKeyTraits<TKey>, class TValueTraits = DefaultValueTraits<TValue>>
class TestContainer {
public:
	typedef TKey Key;
	typedef TValue Value;
	typedef TKeyTraits KeyTraits;
	typedef TValueTraits ValueTraits;

private:
	typedef ArrayWithHash<Key, Value, KeyTraits, ValueTraits> TArrayWithHash;
	typedef StdMapWrapper<Key, Value, KeyTraits, ValueTraits> TStdMapWrapper;
	typedef typename TStdMapWrapper::Ptr TPtr;
	typedef typename KeyTraits::Size Size;
	typedef ValueTestingUtils<Value> TestUtils;

	TArrayWithHash obj;
	TStdMapWrapper check;

	static inline bool Same(const Value &a, const Value &b) {
		if (ValueTraits::IsEmpty(a) && ValueTraits::IsEmpty(b))
			return true;
		return TestUtils::AreEqual(a, b);
	}
	static inline bool Same(Value *a, TPtr b) {
		if (!a == !b)
			return true;
		return Same(*a, *b);
	}

public:
	int assertLevel;
	bool printCommands;
	char label[256];
	TestContainer() {
		//change in order to trade speed for internal checks verbosity
		assertLevel = 2;
		//chenge to true if you want to see a problematic test
		printCommands = false;
	}

	Size GetSize() const {
		if (printCommands) std::cout << "GetSize" << std::endl;
		Size a = obj.GetSize();
		Size b = check.GetSize();
		AWH_ASSERT_ALWAYS(a == b);
		return a;
	}
	Value Get(Key key) const {
		if (printCommands) std::cout << "Get " << key << std::endl;
		Value a = obj.Get(key);
		Value b = check.Get(key);
		AWH_ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *GetPtr(Key key) const {
		if (printCommands) std::cout << "GetPtr " << key << std::endl;
		Value *a = obj.GetPtr(key);
		TPtr b = check.GetPtr(key);
		AWH_ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *Set(Key key, Value value) {
		if (printCommands) std::cout << "Set " << key << " " << TestUtils::Content(value) << std::endl;
		Value *a = obj.Set(key, TestUtils::Clone(value));
		TPtr b = check.Set(key, TestUtils::Clone(value));
		AWH_ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *SetIfNew(Key key, Value value) {
		if (printCommands) std::cout << "SetIfNew " << key << " " << TestUtils::Content(value) << std::endl;
		Value *a = obj.SetIfNew(key, TestUtils::Clone(value));
		TPtr b = check.SetIfNew(key, TestUtils::Clone(value));
		AWH_ASSERT_ALWAYS(Same(a, b));
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
		AWH_ASSERT_ALWAYS(a == b);
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
	int64_t CalcCheckSum() const {
		int64_t sum;
		auto Add = [&sum](Key key, Value &value) -> bool {
			sum += key * 10 + int64_t(TestUtils::CheckSum(value));
			return false;
		};
		sum = 0;
		obj.ForEach(Add);
		int64_t a = sum;
		sum = 0;
		check.ForEach(Add);
		int64_t b = sum;
		AWH_ASSERT_ALWAYS(a == b);
		return a;
	}

	//for testing only
	template<class Rnd> Value *SomePtr(Rnd &rnd) const {
		return obj.GetPtr(check.SomeKey(rnd));
	}
};
