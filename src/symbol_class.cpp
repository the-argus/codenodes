#include "clang_to_graphml_impl.h"

namespace cn {
size_t ClassSymbol::get_num_symbols_this_references() const { return 0; }

Symbol* ClassSymbol::get_symbol_this_references(size_t /*index*/) const
{
    return nullptr;
}

ClassSymbol*
ClassSymbol::create_and_visit_children(Symbol* semantic_parent,
                                       ClangToGraphMLBuilder::Job& job,
                                       const CXCursor& cursor)
{
    // assert(!job.shared_data->symbols_by_usr.contains(OwningCXString::clang_getCursorUSR(cursor).c_str()))

    // return &job.create_or_find_symbol_with_cursor<ClassSymbol>(semantic_parent,
    //                                                            cursor);
    return nullptr;
}
} // namespace cn
