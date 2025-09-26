#include "clang_to_graphml_impl.h"

namespace cn {
struct Args
{
    ClangToGraphMLBuilder::Job& job;
    const CXCursor& cursor;
    Symbol* semantic_parent;

    // output
    Vector<ClassSymbol::ClassReference>& inner_classes;
    Vector<ClassSymbol::FunctionReference>& member_functions;
    Vector<ClassSymbol::EnumReference>& inner_enums;
};

size_t ClassSymbol::get_num_symbols_this_references() const { return 0; }

Symbol* ClassSymbol::get_symbol_this_references(size_t /*index*/) const
{
    return nullptr;
}
namespace {
enum CXChildVisitResult visitor(CXCursor input_cursor, CXCursor /* parent */,
                                void* userdata)
{
    return CXChildVisit_Continue;
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
        .inner_classes = this->inner_classes,
        .member_functions = this->member_functions,
        .inner_enums = this->inner_enums,
    };

    clang_visitChildren(cursor, visitor, &args);

    return true;
}
} // namespace cn
