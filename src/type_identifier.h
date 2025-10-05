#ifndef __TYPE_IDENTIFIER_H__
#define __TYPE_IDENTIFIER_H__

#include "aliases.h"
#include <cstddef>
#include <cstdint>
#include <variant>

namespace cn {
enum class PrimitiveTypeType : uint8_t
{
    Void,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    Bool,
    Nullptr,
    Unknown, // some weird implementation defined long double or something?
};

enum class ReferenceKind : uint8_t
{
    RValue,
    LValue,
};

struct Symbol;
struct TypeIdentifier;
struct PointerTypeIdentifier;

struct SymbolInfo
{
    Symbol* symbol_queried;
    size_t total_symbols;
};

/// not a primitive type or pointer or reference or array or type alias
struct UserDefinedTypeIdentifier
{
    Symbol* symbol;

    [[nodiscard]] SymbolInfo try_get_symbol_info(size_t index) const
    {
        return {
            .symbol_queried = index == 0 ? symbol : nullptr,
            .total_symbols = symbol == nullptr ? 0UL : 1UL,
        };
    }
};

struct FunctionProtoTypeIdentifier
{
    OrderedCollection<TypeIdentifier> types;

    [[nodiscard]] constexpr SymbolInfo try_get_symbol_info(size_t index) const;
};

struct CArrayTypeIdentifier
{
    std::variant<FunctionProtoTypeIdentifier, UserDefinedTypeIdentifier,
                 PrimitiveTypeType, CArrayTypeIdentifier*,
                 PointerTypeIdentifier*>
        contents_type;
    size_t size{};

    [[nodiscard]] constexpr SymbolInfo try_get_symbol_info(size_t index) const;
};

struct ConcreteTypeIdentifier
{
    std::variant<PrimitiveTypeType, UserDefinedTypeIdentifier,
                 CArrayTypeIdentifier>
        variant;

    [[nodiscard]] SymbolInfo try_get_symbol_info(size_t index) const
    {
        return std::visit(
            [index](const auto& iden) {
                if constexpr (std::is_same_v<decltype(iden),
                                             const PrimitiveTypeType&>) {
                    return SymbolInfo{};
                } else {
                    return iden.try_get_symbol_info(index);
                }
            },
            variant);
    }
};

struct PointerTypeIdentifier
{
    // either a pointer to another pointer, or a pointer to a concrete thing
    std::variant<ConcreteTypeIdentifier, PointerTypeIdentifier*,
                 FunctionProtoTypeIdentifier>
        pointee_type;

    [[nodiscard]] SymbolInfo try_get_symbol_info(size_t index) const
    {
        return std::visit(
            [index](const auto& iden) {
                using T = std::remove_cvref_t<decltype(iden)>;
                if constexpr (std::is_pointer_v<T>) {
                    return iden->try_get_symbol_info(index);
                } else {
                    return iden.try_get_symbol_info(index);
                }
            },
            pointee_type);
    }
};

struct NonReferenceTypeIdentifier
{
    std::variant<PointerTypeIdentifier, ConcreteTypeIdentifier> variant;

    [[nodiscard]] SymbolInfo try_get_symbol_info(size_t index) const
    {
        return std::visit(
            [index](const auto& iden) {
                return iden.try_get_symbol_info(index);
            },
            variant);
    }
};

struct ReferenceTypeIdentifier
{
    bool is_const;
    ReferenceKind kind;
    NonReferenceTypeIdentifier referenced_type;

    [[nodiscard]] SymbolInfo try_get_symbol_info(size_t index) const
    {
        return referenced_type.try_get_symbol_info(index);
    }
};

struct TypeIdentifier
{
    [[nodiscard]] Symbol* try_get_symbol(size_t index) const
    {
        return std::visit(
                   [index](const auto& iden) {
                       return iden.try_get_symbol_info(index);
                   },
                   variant)
            .symbol_queried;
    }

    [[nodiscard]] size_t get_num_symbols() const
    {
        return std::visit(
                   [](const auto& iden) { return iden.try_get_symbol_info(0); },
                   variant)
            .total_symbols;
    }

    // the sum of all human knowledge
    std::variant<ReferenceTypeIdentifier, NonReferenceTypeIdentifier> variant;
};

[[nodiscard]] constexpr SymbolInfo
FunctionProtoTypeIdentifier::try_get_symbol_info(size_t index) const
{
    size_t num_symbols = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        num_symbols += types.at(i).get_num_symbols();
    }
    return {
        .symbol_queried = index < types.size()
                              ? types.at(index).try_get_symbol(index)
                              : nullptr,
        .total_symbols = num_symbols,
    };
}

[[nodiscard]] constexpr SymbolInfo
CArrayTypeIdentifier::try_get_symbol_info(size_t index) const
{
    return std::visit(
        [index](const auto& iden) {
            if constexpr (std::is_same_v<decltype(iden),
                                         const PrimitiveTypeType&>) {
                return SymbolInfo{};
            } else if constexpr (std::is_pointer_v<
                                     std::remove_cvref_t<decltype(iden)>>) {
                return iden->try_get_symbol_info(index);
            } else {
                return iden.try_get_symbol_info(index);
            }
        },
        contents_type);
}
} // namespace cn

#endif
