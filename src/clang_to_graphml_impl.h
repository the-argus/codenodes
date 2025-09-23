#ifndef __CODENODES_CLANG_TO_GRAPHML_IMPL_H__
#define __CODENODES_CLANG_TO_GRAPHML_IMPL_H__

#include "clang_wrapper.h"
#include "symbol.h"
#include <cassert>

namespace cn {
struct ClangToGraphMLBuilder::PersistentData
{
    explicit constexpr PersistentData(std::pmr::memory_resource* resource)
        : allocator(resource), temp_resource(resource),
          temp_allocator(&temp_resource)
    {
    }

    PersistentData(const PersistentData&) = delete;
    PersistentData& operator=(const PersistentData&) = delete;
    PersistentData(PersistentData&&) = delete;
    PersistentData& operator=(PersistentData&&) = delete;
    ~PersistentData() = default;

    std::vector<OwningPointer<Job>> finished_jobs;
    // all symbols by their unique id
    Map<std::string_view, Symbol*> symbols_by_usr;
    // forest of definitions
    NamespaceSymbol global_namespace{nullptr, String{}, {}};

    /// For data which lives throughout the whole parse
    std::pmr::polymorphic_allocator<> allocator;
    std::pmr::monotonic_buffer_resource temp_resource;
    std::pmr::polymorphic_allocator<> temp_allocator;

    [[nodiscard]] Symbol* try_get_symbol(const String& usr)
    {
        if (usr.empty()) {
            return nullptr;
        }
        if (symbols_by_usr.contains(usr)) {
            auto* out = symbols_by_usr[usr];
            assert(out);
            return out;
        }
        return nullptr;
    }
};

struct ClangToGraphMLBuilder::Job
{
    explicit Job(MemoryResource* res, PersistentData* data)
        : shared_data(data), resource(res), allocator(&resource)
    {
    }

    void run(const char* filename,
             std::span<const char* const> command_args) noexcept;

    static enum CXChildVisitResult
    top_level_cursor_visitor(CXCursor current_cursor, CXCursor parent,
                             void* userdata);

    // for use with clang_getInclusions... could be useful for eliminating
    // symbols after a certain inclusion depth
    // static void inclusion_visitor(CXFile included_file,
    //                               CXSourceLocation* inclusion_stack,
    //                               unsigned include_len,
    //                               CXClientData client_data);

    /// Creates or finds a symbol for a given cursor and returns a reference to
    /// it. Also will try to visit its children, if they have not already been
    /// visited.
    template <typename T>
        requires(!std::is_same_v<T, Symbol> && std::is_base_of_v<Symbol, T>)
    T& create_or_find_symbol_with_cursor(Symbol* semantic_parent,
                                         CXCursor cursor)
    {
        auto usr = OwningCXString::clang_getCursorUSR(cursor).copy_to_string(
            allocator);

        if (shared_data->symbols_by_usr.contains(usr)) {
            Symbol* out = shared_data->symbols_by_usr[std::move(usr)];
            out->try_visit_children(*this, cursor);
            assert(out->symbol_kind == T::kind);
            assert(out->semantic_parent == semantic_parent);
            T* upcasted = out->upcast<T>();
            if (!upcasted) {
                std::abort(); // release mode safety
            }
            return *upcasted;
        }

        T* out = this->allocator.new_object<T>(semantic_parent, std::move(usr),
                                               cursor);

        if (semantic_parent == &this->shared_data->global_namespace) {
            this->shared_data->global_namespace.symbols.push_back(out);
        }

        out->try_visit_children(*this, cursor);

        shared_data->symbols_by_usr[std::string_view(out->usr)] = out;

        return *out;
    }

    PersistentData* shared_data;
    Allocator allocator;
    JobResource resource;
};

constexpr std::optional<PrimitiveTypeType>
clang_type_to_primitive_type(const CXType& type)
{
    switch (type.kind) {
    case CXType_Char_S:
        return PrimitiveTypeType::Int8;
    case CXType_Short:
        return PrimitiveTypeType::Int16;
    case CXType_Int:
        return PrimitiveTypeType::Int32;
    case CXType_Long:
        return PrimitiveTypeType::Int64;
    case CXType_Char_U:
        return PrimitiveTypeType::UInt8;
    case CXType_UShort:
        return PrimitiveTypeType::UInt16;
    case CXType_UInt:
        return PrimitiveTypeType::UInt32;
    case CXType_ULong:
        return PrimitiveTypeType::UInt64;
    case CXType_Bool:
        return PrimitiveTypeType::Bool;
    case CXType_NullPtr:
        return PrimitiveTypeType::Nullptr;
    case CXType_Float:
        return PrimitiveTypeType::Float;
    case CXType_Double:
        return PrimitiveTypeType::Double;
    case CXType_Void:
        return PrimitiveTypeType::Void;
    default:
        return {};
    }
}

constexpr std::optional<UserDefinedTypeIdentifier>
clang_type_to_user_defined_type(ClangToGraphMLBuilder::PersistentData& data,
                                const CXType& type)
{
    auto decl = clang_getTypeDeclaration(get_cannonical_type(type));
    Symbol* user_defined = data.try_get_symbol(
        OwningCXString::clang_getCursorUSR(decl).copy_to_string(
            data.temp_allocator));
    data.temp_resource.release();

    if (user_defined == nullptr) {
        return {};
    }

    return UserDefinedTypeIdentifier{.symbol = user_defined};
}

constexpr std::optional<CArrayTypeIdentifier>
clang_type_to_c_array_type_identifier(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    if (auto element_type =
            get_cannonical_type(clang_getArrayElementType(type));
        element_type.kind != CXType_Invalid) {

        auto size = static_cast<size_t>(clang_getArraySize(type));

        if (auto primitive = clang_type_to_primitive_type(type); primitive) {
            return CArrayTypeIdentifier{.contents_type = primitive.value(),
                                        .size = size};
        }

        if (auto user_defined = clang_type_to_user_defined_type(data, type);
            user_defined) {
            return CArrayTypeIdentifier{.contents_type = user_defined.value(),
                                        .size = size};
        }

        std::ignore = fprintf(stderr,
                              "Unknown type { kind: %d } in array, saying that "
                              "it is an array of integers\n",
                              type.kind);
        return CArrayTypeIdentifier{.contents_type = PrimitiveTypeType::Int32,
                                    .size = size};
    }
    return {};
}

constexpr std::optional<ConcreteTypeIdentifier>
clang_type_to_concrete_type_identifier(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    if (const auto primitive = clang_type_to_primitive_type(type); primitive) {
        return ConcreteTypeIdentifier{primitive.value()};
    }

    if (const auto carray = clang_type_to_c_array_type_identifier(data, type);
        carray) {
        return ConcreteTypeIdentifier{carray.value()};
    }

    if (const auto carray = clang_type_to_c_array_type_identifier(data, type);
        carray) {
        return ConcreteTypeIdentifier{carray.value()};
    }
    return {};
}

constexpr PointerTypeIdentifier::PointerToPointerTypeIdentifier
_clang_type_to_pointer_type_identifier_recursive_allocating(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    using Out = PointerTypeIdentifier::PointerToPointerTypeIdentifier;
    if (CXType pointee = get_cannonical_type(clang_getPointeeType(type));
        pointee.kind != CXType_Invalid) {

        // allocate the new pointer object in chain
        auto* iden = data.allocator.new_object<PointerTypeIdentifier>();
        auto deleter =
            [allocator = data.allocator](PointerTypeIdentifier* ptr) mutable {
                allocator.delete_object(ptr);
            };
        Out out(iden, std::move(deleter));

        if (auto ptr =
                _clang_type_to_pointer_type_identifier_recursive_allocating(
                    data, pointee);
            ptr) {
            // this is also a pointer to a pointer type
            out->pointee_type = std::move(ptr);
            return out;
        }

        if (auto concrete =
                clang_type_to_concrete_type_identifier(data, pointee);
            concrete) {
            out->pointee_type = concrete.value();
            return out;
        }

        std::ignore = std::fprintf(
            stderr, "Unknown pointee type { kind: %s } for pointer\n",
            OwningCXString::clang_getTypeKindSpelling(pointee.kind).c_str());
    }
    return {};
}

constexpr std::optional<PointerTypeIdentifier>
clang_type_to_pointer_type_identifier(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    if (CXType pointee = get_cannonical_type(clang_getPointeeType(type));
        pointee.kind != CXType_Invalid) {

        if (auto ptr =
                _clang_type_to_pointer_type_identifier_recursive_allocating(
                    data, pointee);
            ptr) {
            // this is a pointer to a pointer type
            return PointerTypeIdentifier{std::move(ptr)};
        }

        if (auto concrete =
                clang_type_to_concrete_type_identifier(data, pointee);
            concrete) {
            return PointerTypeIdentifier{concrete.value()};
        }

        std::ignore = std::fprintf(
            stderr, "Unknown pointee type { kind: %s } for pointer\n",
            OwningCXString::clang_getTypeKindSpelling(pointee.kind).c_str());
    }
    return {};
}

constexpr std::optional<NonReferenceTypeIdentifier>
clang_type_to_nonreference_type_identifier(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    if (const auto concrete =
            clang_type_to_concrete_type_identifier(data, type);
        concrete) {
        return NonReferenceTypeIdentifier{concrete.value()};
    }
    if (auto pointer = clang_type_to_pointer_type_identifier(data, type);
        pointer) {
        return NonReferenceTypeIdentifier{std::move(pointer.value())};
    }
    return {};
}

constexpr std::optional<ReferenceTypeIdentifier>
clang_type_to_reference_type_identifier(
    ClangToGraphMLBuilder::PersistentData& data, const CXType& type)
{
    ReferenceKind kind{};
    switch (type.kind) {
    case CXType_LValueReference:
        kind = ReferenceKind::LValue;
        break;
    case CXType_RValueReference:
        kind = ReferenceKind::RValue;
        break;
    default:
        return {};
    }

    const bool is_const = clang_isConstQualifiedType(type) > 0;

    const CXType pointee_type = get_cannonical_type(clang_getPointeeType(type));

    if (auto nonref =
            clang_type_to_nonreference_type_identifier(data, pointee_type);
        nonref) {
        return ReferenceTypeIdentifier{
            .is_const = is_const,
            .kind = kind,
            .referenced_type = std::move(nonref.value()),
        };
    }

    std::ignore =
        fprintf(stderr,
                "Unknown pointee type { kind: %s } for reference "
                "type, pretending it is a `const int&`\n",
                OwningCXString::clang_getTypeKindSpelling(type.kind).c_str());
    return ReferenceTypeIdentifier{
        .is_const = true,
        .kind = kind,
        .referenced_type = NonReferenceTypeIdentifier{ConcreteTypeIdentifier{
            PrimitiveTypeType::Int32}},
    };
}

constexpr TypeIdentifier
clang_type_to_type_identifier(ClangToGraphMLBuilder::PersistentData& data,
                              const CXType& type)
{
    if (auto nonref = clang_type_to_nonreference_type_identifier(data, type);
        nonref) {
        return TypeIdentifier{std::move(nonref.value())};
    }
    if (auto ref = clang_type_to_reference_type_identifier(data, type); ref) {
        return TypeIdentifier{std::move(ref.value())};
    }

    std::ignore =
        fprintf(stderr,
                "Attempted to convert unknown type { kind: %s } to "
                "TypeIdentifier, pretending it is an int\n",
                OwningCXString::clang_getTypeKindSpelling(type.kind).c_str());

    return TypeIdentifier{NonReferenceTypeIdentifier{
        ConcreteTypeIdentifier{PrimitiveTypeType::Int32}}};
}

} // namespace cn

#endif
