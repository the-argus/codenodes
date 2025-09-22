#include <cassert>
#include <cstring>

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
                nullptr, current_cursor);
        break;
    }
    case CXCursorKind::CXCursor_FunctionDecl: {
        auto& function_symbol =
            job->create_or_find_symbol_with_cursor<FunctionSymbol>(
                nullptr, current_cursor);
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

bool ClangToGraphMLBuilder::finish(std::ostream& output) noexcept
{
    return false;
}

} // namespace cn
