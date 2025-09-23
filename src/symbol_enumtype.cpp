#include "clang_to_graphml_impl.h"

namespace cn {
bool EnumTypeSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                         const CXCursor& cursor)
{
    assert(!job.shared_data->symbols_by_usr.contains(
        OwningCXString::clang_getCursorUSR(cursor).c_str()));

    return true; // enums never reference other stuff
}
} // namespace cn
