#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A pointer-to-pointer map for a hot lookup in a map that stays small next to
// what it shadows: open addressing, linear probing, at most half full,
// backward-shift erase (no tombstones, so a lookup never walks further than
// the run its key hashes into). Key and value share an 8-byte slot, so a hit
// reads one cache line. An empty map answers from its count alone, without
// touching memory. Not thread safe.
class PointerMap
{
public:
	// The value stored for ptr, or null.
	void* Find(void const* ptr) const
	{
		if (!this->Count)
			return nullptr;

		auto const key = reinterpret_cast<uintptr_t>(ptr);
		for (size_t i = this->Home(key); ; i = (i + 1) & this->Mask)
		{
			auto const& slot = this->Slots[i];
			if (!slot.Key)
				return nullptr;
			if (slot.Key == key)
				return slot.Value;
		}
	}

	// Stores value for ptr (replacing any value it had). A null key or value
	// is not stored.
	void Insert(void const* ptr, void* value)
	{
		auto const key = reinterpret_cast<uintptr_t>(ptr);
		if (!key || !value)
			return;

		if ((this->Count + 1) * 2 > this->Slots.size())
			this->Grow();

		size_t i = this->Home(key);
		for (; this->Slots[i].Key; i = (i + 1) & this->Mask)
		{
			if (this->Slots[i].Key == key)
			{
				this->Slots[i].Value = value;
				return;
			}
		}

		this->Slots[i] = { key, value };
		++this->Count;
	}

	// Removes ptr if its stored value is value.
	void Erase(void const* ptr, void const* value)
	{
		auto const key = reinterpret_cast<uintptr_t>(ptr);
		if (!this->Count || !key)
			return;

		size_t hole = this->Home(key);
		for (; this->Slots[hole].Key != key; hole = (hole + 1) & this->Mask)
		{
			if (!this->Slots[hole].Key)
				return;
		}

		if (this->Slots[hole].Value != value)
			return;

		// Close the hole: each later entry of the run whose home is not in
		// (hole, its slot] moves back into it, so every entry stays reachable
		// from its home without crossing an empty slot.
		for (size_t j = (hole + 1) & this->Mask; this->Slots[j].Key; j = (j + 1) & this->Mask)
		{
			size_t const home = this->Home(this->Slots[j].Key);
			if (((j - home) & this->Mask) >= ((j - hole) & this->Mask))
			{
				this->Slots[hole] = this->Slots[j];
				hole = j;
			}
		}

		this->Slots[hole] = {};
		--this->Count;
	}

	size_t Size() const
	{
		return this->Count;
	}

private:
	struct Slot
	{
		uintptr_t Key;
		void* Value;
	};

	// Fibonacci hashing of the address without its alignment bits: the top
	// Bits bits of a 32-bit product.
	size_t Home(uintptr_t key) const
	{
		return static_cast<size_t>(static_cast<uint32_t>(static_cast<uint32_t>(key >> 2) * 2654435769u) >> this->Shift);
	}

	void Grow()
	{
		std::vector<Slot> old;
		old.swap(this->Slots);

		unsigned const bits = old.empty() ? 4 : this->Bits + 1;
		this->Bits = bits;
		this->Shift = 32 - bits;
		this->Mask = (size_t(1) << bits) - 1;
		this->Slots.assign(size_t(1) << bits, Slot {});
		this->Count = 0;

		for (auto const& slot : old)
		{
			if (!slot.Key)
				continue;

			size_t i = this->Home(slot.Key);
			while (this->Slots[i].Key)
				i = (i + 1) & this->Mask;

			this->Slots[i] = slot;
			++this->Count;
		}
	}

	std::vector<Slot> Slots;
	size_t Count = 0;
	size_t Mask = 0;
	unsigned Bits = 0;
	unsigned Shift = 32;
};
