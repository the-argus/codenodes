#include "clang_to_graphml_impl.h"

namespace cn {
size_t ClassSymbol::get_num_symbols_this_references() const { return 0; }

Symbol* ClassSymbol::get_symbol_this_references(size_t /*index*/) const
{
    return nullptr;
}

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
    assert(!job.shared_data->symbols_by_usr.contains(
        OwningCXString::clang_getCursorUSR(cursor).c_str()));

    return false; // pretend everything is a forward declaration
}
} // namespace cn
