#ifndef __TYPE_IDENTIFIER_H__
#define __TYPE_IDENTIFIER_H__

#include <cstddef>
#include <cstdint>
#include <variant>

namespace cn {
enum class PrimitiveTypeType : uint8_t
{
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
    Char, // distinct from int8?
    Nullptr,
    Unknown, // some weird implementation defined long double or something?
};

enum class ReferenceKind : uint8_t
{
    RValue,
    LValue,
};

struct Symbol;

/// not a primitive type or pointer or reference or array or type alias
struct UserDefinedTypeIdentifier
{
    Symbol* symbol;
};

struct CArrayTypeIdentifier
{
    std::variant<UserDefinedTypeIdentifier, PrimitiveTypeType> contents_type;
    size_t size{};
};

using ConcreteTypeIdentifier =
    std::variant<PrimitiveTypeType, UserDefinedTypeIdentifier,
                 CArrayTypeIdentifier>;

struct PointerTypeIdentifier
{
    // either a pointer to another pointer, or a pointer to a concrete thing
    std::variant<ConcreteTypeIdentifier, PointerTypeIdentifier*> pointee_type;
};

using NonReferenceTypeIdentifier =
    std::variant<PointerTypeIdentifier, ConcreteTypeIdentifier>;

struct ReferenceTypeIdentifier
{
    bool is_const;
    ReferenceKind kind;
    NonReferenceTypeIdentifier referenced_type;
};

struct TypeIdentifier
{
    [[nodiscard]] Symbol* try_get_symbol() const
    {
        return std::visit(
            [](const auto& iden) { return ref_or_nonref_visitor(iden); },
            variant);
    }

    // the sum of all human knowledge
    std::variant<ReferenceTypeIdentifier, NonReferenceTypeIdentifier> variant;

  private:
    static Symbol*
    user_def_or_primitive_recursive_visitor(const PrimitiveTypeType& iden)
    {
        return primitive_or_user_def_or_carray_recursive_visitor(iden);
    }

    static Symbol* user_def_or_primitive_recursive_visitor(
        const UserDefinedTypeIdentifier& iden)
    {
        return primitive_or_user_def_or_carray_recursive_visitor(iden);
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const PrimitiveTypeType& /*type*/)
    {
        return nullptr;
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const UserDefinedTypeIdentifier& iden)
    {
        return iden.symbol;
    }

    static Symbol* primitive_or_user_def_or_carray_recursive_visitor(
        const CArrayTypeIdentifier& iden)
    {
        return std::visit(
            [](const auto& iden) {
                return user_def_or_primitive_recursive_visitor(iden);
            },
            iden.contents_type);
    }

    static Symbol*
    pointee_type_recursive_visitor(PointerTypeIdentifier* pointer_iden)
    {
        return concrete_or_pointer_recursive_visitor(*pointer_iden);
    }

    static Symbol*
    pointee_type_recursive_visitor(const ConcreteTypeIdentifier& concrete_iden)
    {
        return concrete_or_pointer_recursive_visitor(concrete_iden);
    }

    static Symbol* concrete_or_pointer_recursive_visitor(
        const PointerTypeIdentifier& pointer_iden)
    {
        return std::visit(
            [](const auto& iden) {
                return pointee_type_recursive_visitor(iden);
            },
            pointer_iden.pointee_type);
    }

    static Symbol* concrete_or_pointer_recursive_visitor(
        const ConcreteTypeIdentifier& concrete_iden)
    {
        return std::visit(
            [](const auto& iden) {
                return primitive_or_user_def_or_carray_recursive_visitor(iden);
            },
            concrete_iden);
    }

    static Symbol* ref_or_nonref_visitor(const ReferenceTypeIdentifier& ref)
    {
        return ref_or_nonref_visitor(ref.referenced_type);
    }

    static Symbol*
    ref_or_nonref_visitor(const NonReferenceTypeIdentifier& nonref)
    {
        return std::visit(
            [](const auto& iden) {
                return concrete_or_pointer_recursive_visitor(iden);
            },
            nonref);
    }
};

} // namespace cn

#endif
