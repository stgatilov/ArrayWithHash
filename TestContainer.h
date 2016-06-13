#pragma once

#include <iostream>
#include "ArrayHash.h"
#include "StdMapWrapper.h"

//Testing wrapper around both ArrayHash and StdMapWrapper.
//It checks that all the outputs of all method calls are the same.
//Used only for testing purposes
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
	TestContainer(int lvl = 2) {
		//change in order to trade speed for internal checks verbosity
		assertLevel = lvl;
		//chenge to true if you want to see a problematic test
		printCommands = false;
	}

	Size GetSize() const {
		if (printCommands) std::cout << "GetSize" << std::endl;
		Size a = obj.GetSize();
		Size b = check.GetSize();
		ASSERT_ALWAYS(a == b);
		return a;
	}
	Value Get(Key key) const {
		if (printCommands) std::cout << "Get " << key << std::endl;
		Value a = obj.Get(key);
		Value b = check.Get(key);
		ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *GetPtr(Key key) const {
		if (printCommands) std::cout << "GetPtr " << key << std::endl;
		Value *a = obj.GetPtr(key);
		StdMapWrapper::Ptr b = check.GetPtr(key);
		ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *Set(Key key, Value value) {
		if (printCommands) std::cout << "Set " << key << " " << value << std::endl;
		Value *a = obj.Set(key, value);
		StdMapWrapper::Ptr b = check.Set(key, value);
		ASSERT_ALWAYS(Same(a, b));
		obj.AssertCorrectness(assertLevel);
		return a;
	}
	Value *SetIfNew(Key key, Value value) {
		if (printCommands) std::cout << "SetIfNew " << key << " " << value << std::endl;
		Value *a = obj.SetIfNew(key, value);
		StdMapWrapper::Ptr b = check.SetIfNew(key, value);
		ASSERT_ALWAYS(Same(a, b));
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
		ASSERT_ALWAYS(a == b);
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
		ASSERT_ALWAYS(a == b);
		return a;
	}

	//for testing only
	template<class Rnd> Value *SomePtr(Rnd &rnd) const {
		return obj.GetPtr(check.SomeKey(rnd));
	}
};
