#include <cassert>
#include <cstring>
#include <pugixml.hpp>

#include "clang_to_graphml_impl.h"

namespace cn {

ClangToGraphMLBuilder::ClangToGraphMLBuilder(MemoryResource& memory_resource)
    : m_resource(memory_resource), m_allocator(&memory_resource),
      m_data(m_allocator.new_object<PersistentData>(&memory_resource))
{
}

ClangToGraphMLBuilder::~ClangToGraphMLBuilder()
{
    m_allocator.delete_object(m_data);
}

void ClangToGraphMLBuilder::parse(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    OwningPointer job = make_owning<Job>(m_allocator, &m_resource, m_data);
    job->run(filename, command_args);
    m_data->finished_jobs.emplace_back(std::move(job));
}

enum CXChildVisitResult ClangToGraphMLBuilder::Job::top_level_cursor_visitor(
    CXCursor current_cursor, CXCursor /*parent*/, void* userdata)
{
    const CXCursor old_cursor = current_cursor;
    current_cursor = clang_getCanonicalCursor(current_cursor);

    // skip forward declarations altogether
    // if (0 != memcmp(&old_cursor, &current_cursor, sizeof(CXCursor))) {
    //     return CXChildVisit_Continue;
    // }

    auto* job = static_cast<Job*>(userdata);
    PersistentData* data = job->shared_data;

    enum CXCursorKind kind = clang_getCursorKind(current_cursor);

    switch (kind) {
    case CXCursorKind::CXCursor_Namespace: {
        auto& namespace_symbol =
            job->create_or_find_symbol_with_cursor<NamespaceSymbol>(
                current_cursor);
        break;
    }
    case CXCursorKind::CXCursor_FunctionDecl: {
        auto& function_symbol =
            job->create_or_find_symbol_with_cursor<FunctionSymbol>(
                current_cursor);
        break;
    }
    case CXCursorKind::CXCursor_EnumDecl: {
        auto& enum_symbol =
            job->create_or_find_symbol_with_cursor<EnumTypeSymbol>(
                current_cursor);
        break;
    }
    case CXCursorKind::CXCursor_StructDecl:
    case CXCursorKind::CXCursor_UnionDecl:
    case CXCursorKind::CXCursor_ClassDecl: {
        auto& class_symbol =
            job->create_or_find_symbol_with_cursor<ClassSymbol>(current_cursor);
        break;
    }
    case CXCursorKind::CXCursor_CXXMethod: {
        // clang_Cursor_getNumArguments
        // clang_Cursor_isDynamicCall()
        break;
    }
    case CXCursorKind::CXCursor_CallExpr: {
        CXCursor function = clang_getCursorReferenced(current_cursor);
        enum CXCursorKind kind = clang_getCursorKind(function);

        auto is_function =
            kind == CXCursor_CXXMethod || kind == CXCursor_FunctionDecl ||
            kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
            kind == CXCursor_ConversionFunction;

        if (!is_function) {
            auto name = [&]() -> std::optional<const char*> {
                switch (kind) {
                case CXCursor_FirstInvalid:
                    return {};
                    return "CXCursor_FirstInvalid";
                case CXCursor_VarDecl:
                    return "CXCursor_VarDecl";
                case CXCursor_ParmDecl:
                    return "CXCursor_ParmDecl";
                case CXCursor_FieldDecl:
                    return "CXCursor_FieldDecl";
                default:
                    return {};
                }
            }();

            // this should never happen
            if (!name) {
                break;
                auto callsym =
                    OwningCXString::clang_getCursorSpelling(current_cursor);
                auto defsym = OwningCXString::clang_getCursorSpelling(function);
                std::ignore = fprintf(
                    stderr,
                    "Found callexpr which does not reference a function: "
                    "callsym is %s and defsym is %s with kind %d named "
                    "%s\n",
                    callsym.c_str(), defsym.c_str(), static_cast<int>(kind),
                    name.value_or("unknown"));
                break;
            }

            // CXSourceLocation location =
            // clang_getCursorLocation(current_cursor);

            // CXFile file{};
            // uint32_t line{};
            // clang_getSpellingLocation(location, &file, &line, nullptr,
            // nullptr);

            // if (clang_Location_isInSystemHeader(location) != 0) {
            //     break;
            // }

            // // to get containing class or namespace:
            // clang_getCursorSemanticParent(current_cursor);

            // printf("callexpr %s is of type %s in file %s line %u\n",
            //        OwningCXString::clang_getCursorDisplayName(current_cursor)
            //            .c_str(),
            //        name.value(),
            //        OwningCXString::clang_getFileName(file).c_str(), line);

            break;
        }

        break;
    }
    default:
        // do nothing
        break;
    }

    return CXChildVisit_Continue;
}

void ClangToGraphMLBuilder::Job::run(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit =
        clang_parseTranslationUnit(index, filename, command_args.data(),
                                   static_cast<int>(command_args.size()),
                                   nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {
        std::ignore =
            fprintf(stderr, "Unable to parse translation unit %s, aborting.\n",
                    filename);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);
        return;
    }

    // warn for diagnostics
    CXDiagnosticSet diagnostics = clang_getDiagnosticSetFromTU(unit);
    const size_t num_diagnostic = clang_getNumDiagnostics(unit);
    for (size_t i = 0; i < num_diagnostic; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnosticInSet(diagnostic, i);

        std::ignore = fprintf(
            stderr, "DIAGNOSTIC - Encountered while parsing %s: %s\n", filename,
            OwningCXString::clang_formatDiagnostic(
                diagnostic,
                CXDiagnosticDisplayOptions::CXDiagnostic_DisplaySourceLocation)
                .c_str());
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);

    clang_visitChildren(cursor, // Root cursor
                        Job::top_level_cursor_visitor,
                        this // userdata
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

namespace {
/// Recurse through symbols, adding <edge source="" target=""/> entries and
/// <node id= ""/> entries for each depth first
void symbol_recursive_visitor(Symbol* symbol, pugi::xml_node& graph_node)
{
    if (symbol->serialized) {
        return;
    }
    symbol->serialized = true;

    graph_node.append_child("node").append_attribute("id").set_value(
        symbol->display_name);

    const size_t num_children = symbol->get_num_symbols_this_references();
    for (size_t i = 0; i < num_children; ++i) {
        Symbol* target = symbol->get_symbol_this_references(i);
        assert(target != symbol);
        auto node = graph_node.append_child("edge");
        node.append_attribute("source").set_value(symbol->display_name);
        symbol_recursive_visitor(target, graph_node);
        node.append_attribute("target").set_value(target->display_name);
    }
}
} // namespace

bool ClangToGraphMLBuilder::finish(std::ostream& output) noexcept
{
    // function created mostly by following
    // http://graphml.graphdrawing.org/primer/graphml-primer.html
    pugi::xml_document doc;

    pugi::xml_node root = doc.append_child();

    /// rootmost element looks like this:
    /// <graphml xmlns="http://graphml.graphdrawing.org/xmlns"
    /// xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    /// xsi:schemaLocation="http://graphml.graphdrawing.org/xmlns
    /// http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd">
    ///</graphml>

    root.set_name("graphml");
    root.append_attribute("xmlns").set_value(
        "http://graphml.graphdrawing.org/xmlns");
    root.append_attribute("xmlns:xsi")
        .set_value("http://www.w3.org/2001/XMLSchema-instance");
    root.append_attribute("xsi:schemaLocation")
        .set_value("http://graphml.graphdrawing.org/xmlns "
                   "http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd");

    pugi::xml_node graph = root.append_child("graph");
    graph.append_attribute("id").set_value("G");
    graph.append_attribute("edgedefault").set_value("directed");

    // for display purposes, also i think an empty id is invalid
    this->m_data->global_namespace.display_name = "GLOBAL_NAMESPACE";

    // add all nodes and edges
    symbol_recursive_visitor(&this->m_data->global_namespace, graph);

    doc.save(output);
    return true;
}

} // namespace cn
