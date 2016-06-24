#pragma once

#include <map>
#include <unordered_map>
#include <algorithm>

//Wrapper aroung std::unordered_map (or std::map) with interface of ArrayHash.
//Used mainly for comparison of ArrayHash with STL containers.
template<class Key, class Value, class KeyTraits = DefaultKeyTraits<Key>, class ValueTraits = DefaultValueTraits<Value>>
class StdMapWrapper {
//	typedef std::map<Key, Value> Map;
	typedef std::unordered_map<Key, Value> Map;
	typedef typename Map::iterator Iter;
	typedef typename KeyTraits::Size Size;

	Map dict;
public:
	typedef Key Key;
	typedef Value Value;
	typedef KeyTraits KeyTraits;
	typedef ValueTraits ValueTraits;

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
		return (Size)dict.size();
	}
	inline Value Get(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? ValueTraits::GetEmpty() : it->second;
	}
	inline Ptr GetPtr(Key key) const {
		Iter it = const_cast<Map&>(dict).find(key);
		return it == dict.end() ? Ptr() : Ptr(it);
	}
	inline Ptr Set(Key key, Value value) {
		dict[key] = AWH_MOVE(value);
		return Ptr(dict.find(key));
	}
	inline Ptr SetIfNew(Key key, Value value) {
		std::pair<Iter, bool> pib = dict.insert(std::make_pair(key, AWH_MOVE(value)));
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
			if (action(Key(it->first), it->second))
				return;
	}

	//for testing only
	template<class Rnd> Key SomeKey(Rnd &rnd) const {
		size_t idx = std::uniform_int_distribution<size_t>(0, dict.size() - 1)(rnd);
		typename Map::const_iterator iter = dict.begin();
		std::advance(iter, idx);
		return iter->first;
	}
};
