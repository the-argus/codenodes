#include "clang_to_graphml_impl.h"

namespace cn {
struct Args
{
    ClangToGraphMLBuilder::Job& job;
    const CXCursor& cursor;
    Symbol* semantic_parent;

    // output
    OrderedCollection<TypeIdentifier>& type_refs;
    OrderedCollection<TypeIdentifier>& field_types;
    OrderedCollection<TypeIdentifier>& parent_classes;
    OrderedCollection<ClassSymbol*>& inner_classes;
    OrderedCollection<FunctionSymbol*>& member_functions;
    OrderedCollection<EnumTypeSymbol*>& inner_enums;
};

size_t ClassSymbol::get_num_symbols_this_references() const { return 0; }

const Symbol* ClassSymbol::get_symbol_this_references(size_t /*index*/) const
{
    return nullptr;
}

Symbol* ClassSymbol::get_symbol_this_references(size_t /*index*/)
{
    return nullptr;
}

namespace {
enum CXChildVisitResult visitor(CXCursor cursor, CXCursor /* parent */,
                                void* userdata)
{
    auto* args = static_cast<Args*>(userdata);

    switch (cursor.kind) {
    case CXCursor_CXXBaseSpecifier: {
        CXType type = get_cannonical_type(cursor);
        args->parent_classes.emplace_back(
            clang_type_to_type_identifier(args->job, type));
        return CXChildVisit_Continue;
    }
    case CXCursor_FieldDecl: {
        // we already used a field visitor for this
        return CXChildVisit_Continue;
    }
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_CXXMethod: {
        args->member_functions.emplace_back(
            &args->job.create_or_find_symbol_with_cursor<FunctionSymbol>(
                cursor));
        return CXChildVisit_Continue;
    }
    case CXCursor_CXXAccessSpecifier:
        /// not a type, doesn't really matter, except later we will need to
        /// figure out whether symbols here are public/private/protected
    case CXCursor_NamespaceRef:
        /// we dont own the namespace so we dont try to create it, also it's not
        /// a type so we dont reference it with a TypeIdentifier handle
        return CXChildVisit_Continue;
    case CXCursor_VarDecl:
    case CXCursor_TypeRef: {
        CXType type = get_cannonical_type(cursor);
        args->type_refs.emplace_back(
            clang_type_to_type_identifier(args->job, type));
        return CXChildVisit_Continue;
    }
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_StructDecl: {
        args->inner_classes.emplace_back(
            &args->job.create_or_find_symbol_with_cursor<ClassSymbol>(cursor));
        return CXChildVisit_Continue;
    }
    case CXCursor_EnumDecl: {
        args->inner_enums.emplace_back(
            &args->job.create_or_find_symbol_with_cursor<EnumTypeSymbol>(
                cursor));
        return CXChildVisit_Continue;
    }
    default:
        break;
    }

    auto type = clang_getCursorType(cursor);
    std::ignore = std::fprintf(
        stderr,
        "Unexpected cursor type in class named %s of cursor type %s and type "
        "%s typekind %s\n",
        OwningCXString::clang_getCursorSpelling(cursor).c_str(),
        OwningCXString::clang_getCursorKindSpelling(cursor.kind).c_str(),
        OwningCXString::clang_getTypeSpelling(type).c_str(),
        OwningCXString::clang_getTypeKindSpelling(type.kind).c_str());

    return CXChildVisit_Continue;
}

enum CXVisitorResult field_visitor(CXCursor cursor, CXClientData client_data)
{
    auto* args = static_cast<Args*>(client_data);

    args->field_types.emplace_back(
        clang_type_to_type_identifier(args->job, get_cannonical_type(cursor)));

    return CXVisit_Continue;
}
} // namespace

ClassSymbol::AggregateKind
ClassSymbol::get_aggregate_kind_of_cursor(CXCursor cursor)
{
    enum CXCursorKind kind = clang_getCursorKind(cursor);

    switch (kind) {
    case CXCursorKind::CXCursor_UnionDecl:
        return AggregateKind::Union;
    case CXCursorKind::CXCursor_StructDecl:
        return AggregateKind::Struct;
    case CXCursorKind::CXCursor_ClassDecl:
        return AggregateKind::Class;
    default:
        std::ignore = std::fprintf(stderr,
                                   "Attempt to construct class symbol from "
                                   "non-aggregate type cursor with kind %d\n",
                                   kind);
        return AggregateKind::Class;
    }
}

bool ClassSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                      const CXCursor& cursor)
{
    // TODO: detect forward declaration here
    CXType class_type = get_cannonical_type(cursor);

    if (class_type.kind != CXType_Record) {
        std::ignore = std::fprintf(
            stderr, "Attempted to parse class symbol of unexpected type %d\n",
            class_type.kind);
        return false;
    }

    Args args{
        .job = job,
        .cursor = cursor,
        .semantic_parent = this,
        .type_refs = this->type_refs,
        .field_types = this->field_types,
        .parent_classes = this->parent_classes,
        .inner_classes = this->inner_classes,
        .member_functions = this->member_functions,
        .inner_enums = this->inner_enums,
    };

    clang_Type_visitFields(get_cannonical_type(cursor), field_visitor, &args);

    clang_visitChildren(cursor, visitor, &args);

    return true;
}
} // namespace cn
