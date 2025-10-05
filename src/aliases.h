#ifndef __ALIASES_H__
#define __ALIASES_H__

#include <deque>
#include <memory_resource>
#include <string>
#include <unordered_map>

// NOTE: this type does not guarantee that its elements can have stable pointers
// in between calls to emplace_back. this program doesn't really use that
// feature though as there's already indirection for polymorphism with the
// Symbol class, and anything thats by-value is just copied rather than having
// its address taken
template <typename T> class OrderedCollection
{
  public:
    OrderedCollection() = delete;

    OrderedCollection(const OrderedCollection& other) = delete;
    OrderedCollection& operator=(const OrderedCollection& other) = delete;

    OrderedCollection(OrderedCollection&& other) = default;
    OrderedCollection& operator=(OrderedCollection&& other) = default;

    ~OrderedCollection() = default;

    constexpr explicit OrderedCollection(
        std::pmr::polymorphic_allocator<> allocator)
        : elements(allocator)
    {
    }

    [[nodiscard]] constexpr T& at(size_t index) & { return elements.at(index); }
    [[nodiscard]] constexpr const T& at(size_t index) const
    {
        return elements.at(index);
    }
    [[nodiscard]] constexpr T&& at(size_t index) &&
    {
        return elements.at(index);
    }

    [[nodiscard]] constexpr T& back() & { return elements.back(); }
    [[nodiscard]] constexpr const T& back() const { return elements.back(); }
    [[nodiscard]] constexpr T&& back() && { return elements.back(); }

    [[nodiscard]] constexpr size_t size() const { return elements.size(); }

    constexpr void reserve(size_t num_elements)
    {
        if constexpr (std::is_same_v<decltype(elements), std::pmr::vector<T>>) {
            elements.reserve(num_elements);
        } else {
            static_assert(
                std::is_same_v<decltype(elements), std::pmr::deque<T>>);
        }
    }

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr void emplace_back(Args&&... args)
    {
        elements.emplace_back(std::forward<Args>(args)...);
    }

  private:
    std::pmr::deque<T> elements;
};

using String = std::pmr::string;

template <typename K, typename V> using Map = std::pmr::unordered_map<K, V>;

#endif
