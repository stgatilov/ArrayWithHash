#pragma once

#include <map>
#include <unordered_map>

//wrapper aroung std::unordered_map (or std::map) with interface of ArrayHash
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
