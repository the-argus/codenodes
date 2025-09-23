#include "clang_to_graphml_impl.h"

namespace cn {
struct Args
{
    ClangToGraphMLBuilder::Job& job;
    const CXCursor& cursor;
    Symbol* semantic_parent;
    Vector<Symbol*>& symbols; // output of visitor
};

namespace {
enum CXChildVisitResult visitor(CXCursor input_cursor, CXCursor /* parent */,
                                void* userdata)
{
    Args* args = reinterpret_cast<Args*>(userdata);
    CXCursor cursor = clang_getCanonicalCursor(input_cursor);
    const enum CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind >= CXCursor_FirstAttr && kind <= CXCursor_LastAttr) {
        return CXChildVisit_Recurse;
    }
    if (kind == CXCursor_LinkageSpec) {
        return CXChildVisit_Recurse;
    }

    switch (kind) {
    case CXCursorKind::CXCursor_FunctionDecl: {
        auto& function =
            args->job.create_or_find_symbol_with_cursor<FunctionSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&function);
        break;
    }
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_StructDecl: {
        auto& class_symbol =
            args->job.create_or_find_symbol_with_cursor<ClassSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&class_symbol);
        break;
    }
    case CXCursor_EnumDecl: {
        auto& enum_symbol =
            args->job.create_or_find_symbol_with_cursor<EnumTypeSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&enum_symbol);
        break;
    }
    case CXCursor_Namespace: {
        auto& namespace_symbol =
            args->job.create_or_find_symbol_with_cursor<NamespaceSymbol>(
                args->semantic_parent, cursor);
        args->symbols.push_back(&namespace_symbol);
        break;
    }
    case CXCursor_ClassTemplate:
    case CXCursor_FunctionTemplate:
        // we dont handle templates yet
    case CXCursor_VarDecl:
        // we ignore variable declarations for now
    case CXCursor_TypedefDecl:
        // we dont keep track of who aliases what and pretend that everybody
        // accessed a type without any typedefs or usings
        break;
    case CXCursor_UnexposedDecl:
        std::ignore =
            fprintf(stderr, "unexposed decl %s found in namespace %s\n",
                    OwningCXString::clang_getCursorSpelling(cursor).c_str(),
                    args->semantic_parent->usr.c_str());
        break;
    default:
        std::ignore = fprintf(
            stderr, "unexpected cursor kind %d in namespace\n", int(kind));
        break;
    }

    return CXChildVisit_Continue;
}
} // namespace

bool NamespaceSymbol::visit_children_impl(ClangToGraphMLBuilder::Job& job,
                                          const CXCursor& input_cursor)
{
    // TODO: check if this is a forward decl?

    Args args{
        .job = job,
        .cursor = input_cursor,
        .semantic_parent = this,
        .symbols = this->symbols,
    };

    clang_visitChildren(input_cursor, visitor, &args);

    return true;
}
} // namespace cn
