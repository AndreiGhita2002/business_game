//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_POOL_HPP
#define BUSINESS_GAME_SIM_POOL_HPP
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "sim/Handle.hpp"
#include "sim/Serial.hpp"

namespace sim {

/**
 * Where the simulation keeps a kind of object: a vector of slots plus a list
 * of the free ones, named from outside by Handle<Tag>.
 *
 * Everything about it is deterministic, which is the point. Slots are reused
 * last freed first, and for_each() walks them in index order, so two machines
 * that did the same things in the same order agree on which slot every object
 * is in and on the order systems visit them. Never iterate the simulation's
 * state through an unordered container instead.
 *
 * This is also the seam for struct-of-arrays storage, should profiling ever ask
 * for it: the handles and the iteration order are the contract, not the vector.
 */
template <typename T, typename Tag>
class Pool {
public:
    using Id = Handle<Tag>;

    Id insert(T value) {
        uint32_t index;
        if (!free_list.empty()) {
            index = free_list.back();
            free_list.pop_back();
        } else {
            index = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }
        Slot& slot = slots[index];
        // Bumped on every occupant, so a handle to the previous one is refused
        slot.generation++;
        slot.value = std::move(value);
        alive++;
        return Id{index, slot.generation};
    }

    bool erase(const Id id) {
        if (!contains(id)) return false;
        slots[id.index].value.reset();
        free_list.push_back(id.index);
        alive--;
        return true;
    }

    bool contains(const Id id) const {
        return id.index < slots.size()
            && slots[id.index].generation == id.generation
            && slots[id.index].value.has_value();
    }

    T* get(const Id id) { return contains(id) ? &*slots[id.index].value : nullptr; }
    const T* get(const Id id) const { return contains(id) ? &*slots[id.index].value : nullptr; }

    /** How many objects are in the pool. */
    size_t size() const { return alive; }

    /** Calls f(Id, T&) for every object, in slot order. */
    template <typename F>
    void for_each(F&& f) {
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].value.has_value()) f(Id{i, slots[i].generation}, *slots[i].value);
        }
    }

    template <typename F>
    void for_each(F&& f) const {
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].value.has_value()) f(Id{i, slots[i].generation}, *slots[i].value);
        }
    }

    /**
     * Writes the pool's bookkeeping - every slot's generation, which slots are
     * taken and the free list - with `write_value(out, T)` writing each object.
     * The bookkeeping is written too because it decides which slot the next
     * insert lands in, so two pools that differ only there would still drift
     * apart later.
     */
    template <typename F>
    void write(ByteWriter& out, F&& write_value) const {
        out.write_u32(static_cast<uint32_t>(slots.size()));
        for (const Slot& slot : slots) {
            out.write_u32(slot.generation);
            out.write_u8(slot.value.has_value() ? 1 : 0);
            if (slot.value.has_value()) write_value(out, *slot.value);
        }
        out.write_u32(static_cast<uint32_t>(free_list.size()));
        for (const uint32_t index : free_list) out.write_u32(index);
    }

    /**
     * Replaces the pool with what write() wrote, `read_value(in, T*)` reading
     * each object. Every handle that was valid when it was written is valid
     * again, and the next insert lands in the same slot it would have.
     *
     * Refuses a free list naming a slot that is out of range, taken, or listed
     * twice, as inserting into one would overwrite an object or hand the same
     * slot out twice. On a refusal the pool is left empty.
     */
    template <typename F>
    bool read(ByteReader& in, F&& read_value) {
        clear();

        uint32_t slot_count = 0;
        // Every slot takes at least five bytes, so a count beyond that is
        // corruption, and refusing it here stops it asking for gigabytes
        if (!in.read_u32(&slot_count) || slot_count > in.remaining() / 5) return fail();
        slots.resize(slot_count);

        for (Slot& slot : slots) {
            uint8_t taken = 0;
            if (!in.read_u32(&slot.generation) || !in.read_u8(&taken) || taken > 1) return fail();
            if (taken == 1) {
                T value{};
                if (!read_value(in, &value)) return fail();
                slot.value = std::move(value);
                alive++;
            }
        }

        uint32_t free_count = 0;
        if (!in.read_u32(&free_count) || free_count > slot_count) return fail();
        std::vector<bool> listed(slot_count, false);
        free_list.reserve(free_count);
        for (uint32_t i = 0; i < free_count; ++i) {
            uint32_t index = 0;
            if (!in.read_u32(&index) || index >= slot_count) return fail();
            if (slots[index].value.has_value() || listed[index]) return fail();
            listed[index] = true;
            free_list.push_back(index);
        }
        return true;
    }

    void clear() {
        slots.clear();
        free_list.clear();
        alive = 0;
    }

private:
    struct Slot {
        uint32_t generation = 0;
        std::optional<T> value;
    };

    std::vector<Slot> slots;
    std::vector<uint32_t> free_list;
    size_t alive = 0;

    bool fail() {
        clear();
        return false;
    }
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_POOL_HPP
