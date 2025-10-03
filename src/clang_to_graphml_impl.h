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

    /// For data which lives throughout the whole parse
    std::pmr::polymorphic_allocator<> allocator;
    std::pmr::monotonic_buffer_resource temp_resource;
    std::pmr::polymorphic_allocator<> temp_allocator;
    Vector<OwningPointer<Job>> finished_jobs{allocator};
    // all symbols by their unique id
    Map<std::string_view, Symbol*> symbols_by_usr{allocator};
    // forest of definitions
    NamespaceSymbol global_namespace{
        allocator, nullptr, String{}, {}, String{}};
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

    ///  Try to find a cursor with an unknown type. May fail if the cursor is
    ///  not of a type which can be represented by a Symbol
    Symbol*
    create_or_find_symbol_with_cursor_runtime_known_type(CXCursor cursor)
    {
        switch (cursor.kind) {
        case CXCursor_UnionDecl:
        case CXCursor_ClassDecl:
        case CXCursor_StructDecl:
            return &create_or_find_symbol_with_cursor<ClassSymbol>(cursor);
            break;
        case CXCursor_Namespace:
            return &create_or_find_symbol_with_cursor<NamespaceSymbol>(cursor);
            break;
        case CXCursor_EnumDecl:
            return &create_or_find_symbol_with_cursor<EnumTypeSymbol>(cursor);
            break;
        case CXCursor_FunctionDecl:
            return &create_or_find_symbol_with_cursor<FunctionSymbol>(cursor);
            break;
        case CXCursor_TranslationUnit:
        case CXCursor_NoDeclFound:
            break;
        default:
            std::ignore = std::fprintf(
                stderr,
                "WARNING: unexpected semantic parent cursor %s with kind %s\n",
                OwningCXString::clang_getCursorSpelling(cursor).c_str(),
                OwningCXString::clang_getCursorKindSpelling(cursor.kind)
                    .c_str());
            break;
        }
        return nullptr;
    }

    /// Creates or finds a symbol for a given cursor and returns a reference to
    /// it. Also will try to visit its children, if they have not already been
    /// visited.
    template <typename T>
        requires(!std::is_same_v<T, Symbol> && std::is_base_of_v<Symbol, T>)
    T& create_or_find_symbol_with_cursor(CXCursor cursor)
    {
        auto usr = OwningCXString::clang_getCursorUSR(cursor).copy_to_string(
            allocator);

        if (shared_data->symbols_by_usr.contains(usr)) {
            Symbol* out = shared_data->symbols_by_usr[std::move(usr)];
            out->try_visit_children(*this, cursor);
            assert(out->symbol_kind == T::kind);
            T* upcasted = out->upcast<T>();
            if (!upcasted) {
                std::abort(); // release mode safety
            }
            return *upcasted;
        }

        CXCursor semantic_parent_cursor = clang_getCursorSemanticParent(cursor);
        // skip linkage specs, we want namespaces or translation units
        while (semantic_parent_cursor.kind == CXCursor_LinkageSpec) {
            semantic_parent_cursor =
                clang_getCursorSemanticParent(semantic_parent_cursor);
        }
        Symbol* semantic_parent = nullptr;

        semantic_parent = create_or_find_symbol_with_cursor_runtime_known_type(
            semantic_parent_cursor);

        String display_name{allocator};
        if (semantic_parent && !semantic_parent->display_name.empty()) {
            constexpr std::string_view delimiter = "::";
            const std::string_view parent_display_name =
                semantic_parent->display_name;
            auto our_name = OwningCXString::clang_getCursorDisplayName(cursor);
            const size_t out_name_length = strlen(our_name.c_str());
            display_name.reserve(out_name_length + delimiter.size() +
                                 parent_display_name.size());
            display_name.append(parent_display_name);
            display_name.append(delimiter);
            display_name.append(our_name.c_str(), out_name_length);
        } else {
            display_name = OwningCXString::clang_getCursorDisplayName(cursor)
                               .copy_to_string(allocator);
        }

        T* out = this->allocator.new_object<T>(allocator, semantic_parent,
                                               std::move(usr), cursor,
                                               std::move(display_name));

        if (semantic_parent == nullptr) {
            this->shared_data->global_namespace.symbols.push_back(out);
        }

        // NOTE: insert beforehand so that way children can find us when looking
        // for their semantic parent
        shared_data->symbols_by_usr[std::string_view(out->usr)] = out;

        out->try_visit_children(*this, cursor);

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
    case CXType_SChar: // TODO: what is the difference between schar and char_s
    case CXType_Char_S:
        return PrimitiveTypeType::Int8;
    case CXType_Short:
        return PrimitiveTypeType::Int16;
    case CXType_Int:
        return PrimitiveTypeType::Int32;
    case CXType_Long:
    case CXType_LongLong:
        return PrimitiveTypeType::Int64;
    case CXType_UChar:
    case CXType_Char_U:
        return PrimitiveTypeType::UInt8;
    case CXType_UShort:
        return PrimitiveTypeType::UInt16;
    case CXType_UInt:
        return PrimitiveTypeType::UInt32;
    case CXType_ULong:
    case CXType_ULongLong:
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

constexpr TypeIdentifier
clang_type_to_type_identifier(ClangToGraphMLBuilder::Job& job,
                              const CXType& type);

constexpr std::optional<UserDefinedTypeIdentifier>
clang_type_to_user_defined_type(ClangToGraphMLBuilder::Job& job,
                                const CXType& type)
{
    CXCursor decl = clang_getTypeDeclaration(get_cannonical_type(type));

    if (clang_Cursor_isNull(decl) != 0) {
        return {};
    }

    Symbol* user_defined =
        job.create_or_find_symbol_with_cursor_runtime_known_type(decl);

    if (user_defined == nullptr) {
        return {};
    }

    return UserDefinedTypeIdentifier{.symbol = user_defined};
}

constexpr std::optional<PointerTypeIdentifier>
clang_type_to_pointer_type_identifier(ClangToGraphMLBuilder::Job& job,
                                      const CXType& type);

constexpr std::optional<CArrayTypeIdentifier>
clang_type_to_c_array_type_identifier(ClangToGraphMLBuilder::Job& job,
                                      const CXType& type)
{
    if (auto element_type =
            get_cannonical_type(clang_getArrayElementType(type));
        element_type.kind != CXType_Invalid) {

        auto size = static_cast<size_t>(clang_getArraySize(type));

        if (auto primitive = clang_type_to_primitive_type(element_type);
            primitive) {
            return CArrayTypeIdentifier{
                .contents_type = primitive.value(),
                .size = size,
            };
        }

        if (auto user_defined =
                clang_type_to_user_defined_type(job, element_type);
            user_defined) {
            return CArrayTypeIdentifier{
                .contents_type = user_defined.value(),
                .size = size,
            };
        }

        if (auto pointer_type =
                clang_type_to_pointer_type_identifier(job, element_type);
            pointer_type) {
            return CArrayTypeIdentifier{
                .contents_type = std::allocate_shared<PointerTypeIdentifier>(
                    job.shared_data->allocator,
                    std::move((pointer_type.value()))),
                .size = size,
            };
        }

        if (type.kind == CXType_ConstantArray ||
            type.kind == CXType_IncompleteArray ||
            type.kind == CXType_VariableArray ||
            type.kind == CXType_DependentSizedArray) {
            if (auto nested_array = clang_type_to_c_array_type_identifier(
                    job, clang_getElementType(element_type));
                nested_array) {
                return CArrayTypeIdentifier{
                    .contents_type = std::allocate_shared<CArrayTypeIdentifier>(
                        job.shared_data->allocator,
                        std::move(nested_array.value())),
                    .size = size,
                };
            }
        }

        std::ignore =
            fprintf(stderr,
                    "Unknown type %s { kind: %s } in array, saying that "
                    "it is an array of integers\n",
                    OwningCXString::clang_getTypeSpelling(element_type).c_str(),
                    OwningCXString::clang_getTypeKindSpelling(element_type.kind)
                        .c_str());
        return CArrayTypeIdentifier{
            .contents_type = PrimitiveTypeType::Int32,
            .size = size,
        };
    }
    return {};
}

constexpr std::optional<ConcreteTypeIdentifier>
clang_type_to_concrete_type_identifier(ClangToGraphMLBuilder::Job& job,
                                       const CXType& type)
{
    if (const auto primitive = clang_type_to_primitive_type(type); primitive) {
        return ConcreteTypeIdentifier{primitive.value()};
    }

    if (const auto carray = clang_type_to_c_array_type_identifier(job, type);
        carray) {
        return ConcreteTypeIdentifier{carray.value()};
    }

    if (auto user_defined = clang_type_to_user_defined_type(job, type);
        user_defined) {
        return ConcreteTypeIdentifier{user_defined.value()};
    }
    return {};
}

constexpr std::shared_ptr<PointerTypeIdentifier>
_clang_type_to_pointer_type_identifier_recursive_allocating(
    ClangToGraphMLBuilder::Job& job, const CXType& type)
{
    if (CXType pointee = get_cannonical_type(clang_getPointeeType(type));
        pointee.kind != CXType_Invalid) {

        // allocate the new pointer object in chain
        auto out = std::allocate_shared<PointerTypeIdentifier>(
            job.shared_data->allocator);

        if (auto ptr =
                _clang_type_to_pointer_type_identifier_recursive_allocating(
                    job, pointee);
            ptr) {
            // this is also a pointer to a pointer type
            out->pointee_type = std::move(ptr);
            return out;
        }

        if (auto concrete =
                clang_type_to_concrete_type_identifier(job, pointee);
            concrete) {
            out->pointee_type = std::move(concrete.value());
            return out;
        }

        std::ignore = std::fprintf(
            stderr, "Unknown pointee type { kind: %s } for pointer\n",
            OwningCXString::clang_getTypeKindSpelling(pointee.kind).c_str());
    }
    return {};
}

constexpr std::optional<PointerTypeIdentifier>
clang_type_to_pointer_type_identifier(ClangToGraphMLBuilder::Job& job,
                                      const CXType& type)
{
    if (CXType pointee = get_cannonical_type(clang_getPointeeType(type));
        pointee.kind != CXType_Invalid) {

        if (auto ptr =
                _clang_type_to_pointer_type_identifier_recursive_allocating(
                    job, pointee);
            ptr) {
            // this is a pointer to a pointer type
            return PointerTypeIdentifier{std::move(ptr)};
        }

        if (auto concrete =
                clang_type_to_concrete_type_identifier(job, pointee);
            concrete) {
            return PointerTypeIdentifier{concrete.value()};
        }

        if (pointee.kind == CXType_FunctionProto) {
            int num_args = clang_getNumArgTypes(pointee);
            Vector<std::shared_ptr<TypeIdentifier>> arg_types{
                job.shared_data->allocator};
            arg_types.reserve(num_args);
            for (int i = 0; i < num_args; ++i) {
                TypeIdentifier iden = clang_type_to_type_identifier(
                    job, clang_getArgType(pointee, i));
                arg_types.push_back(std::allocate_shared<TypeIdentifier>(
                    job.shared_data->allocator, std::move(iden)));
            }

            TypeIdentifier result_iden = clang_type_to_type_identifier(
                job, clang_getResultType(pointee));
            arg_types.push_back(std::allocate_shared<TypeIdentifier>(
                job.shared_data->allocator, std::move(result_iden)));
            return PointerTypeIdentifier{
                FunctionProtoTypeIdentifier{std::move(arg_types)}};
        }

        std::ignore = std::fprintf(
            stderr, "Unknown pointee type { kind: %s } for pointer\n",
            OwningCXString::clang_getTypeKindSpelling(pointee.kind).c_str());
    }
    return {};
}

constexpr std::optional<NonReferenceTypeIdentifier>
clang_type_to_nonreference_type_identifier(ClangToGraphMLBuilder::Job& job,
                                           const CXType& type)
{
    if (const auto concrete = clang_type_to_concrete_type_identifier(job, type);
        concrete) {
        return NonReferenceTypeIdentifier{concrete.value()};
    }
    if (auto pointer = clang_type_to_pointer_type_identifier(job, type);
        pointer) {
        return NonReferenceTypeIdentifier{std::move(pointer.value())};
    }
    return {};
}

constexpr std::optional<ReferenceTypeIdentifier>
clang_type_to_reference_type_identifier(ClangToGraphMLBuilder::Job& job,
                                        const CXType& type)
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
            clang_type_to_nonreference_type_identifier(job, pointee_type);
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
clang_type_to_type_identifier(ClangToGraphMLBuilder::Job& job,
                              const CXType& type)
{
    if (auto nonref = clang_type_to_nonreference_type_identifier(job, type);
        nonref) {
        return TypeIdentifier{std::move(nonref.value())};
    }
    if (auto ref = clang_type_to_reference_type_identifier(job, type); ref) {
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
