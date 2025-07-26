#include <cassert>
#include <clang-c/Index.h>
#include <cstdio>
#include <unordered_map>

#include "clang_to_graphml.h"

template <typename T> using vector = std::pmr::vector<T>;
using string = std::pmr::string;
template <typename K, typename V> using map = std::pmr::unordered_map<K, V>;

// wrapper around a regular point to mark that, if this algorithm wasnt written
// with arenas, that pointer would be responsible for freeing
template <typename T> struct owning_ptr
{
    constexpr T* operator->() { return m_inner; }
    constexpr T& operator*() { return *m_inner; }

    explicit constexpr owning_ptr(T* other) : m_inner(other) {}
    explicit operator T*() { return m_inner; }

  private:
    T* m_inner;
};

namespace {
template <typename T, typename... args_t>
owning_ptr<T> make_owning_with(std::pmr::polymorphic_allocator<>& allocator,
                               args_t&&... args)
{
    T* ptr = allocator.allocate_object<T>();
    std::construct_at(ptr, std::forward<args_t>(args)...);
    return owning_ptr(ptr);
}
} // namespace

namespace cn {

enum class SymbolType : uint8_t
{
    Class,
    Function,
    Enum,
    Union,
    Variable,
};

struct Symbol
{
    Symbol() = delete;
    constexpr Symbol(SymbolType _type, string&& _name, bool _defined)
        : type(_type), name(std::move(_name)), defined(_defined)
    {
    }

    SymbolType type;
    string name;
    bool defined = false; // this symbol may be incomplete
};

struct ClassInfo;
struct FunctionInfo;
struct EnumInfo;

struct FunctionInfo : public Symbol
{
    constexpr FunctionInfo(SymbolType _type, string&& _name, bool _defined)
        : Symbol(_type, std::move(_name), _defined),
          was_forward_declared(!_defined)
    {
    }

    ClassInfo* parent = nullptr; // defined if this is a method
    bool has_overloads{};
    bool was_forward_declared{};
};

struct ClassInfo : public Symbol
{
    vector<FunctionInfo*> memberFunctionSymbols;
};

struct ClangToGraphMLBuilder::Job
{
    explicit Job(size_t initial_size_bytes, std::pmr::memory_resource* res)
        : memory_resource(initial_size_bytes, res), allocator(&memory_resource),
          functions(allocator), classes(allocator)
    {
    }

    void do_file(const char* filename,
                 std::span<const char* const> command_args) noexcept;

    std::pmr::monotonic_buffer_resource memory_resource;
    std::pmr::polymorphic_allocator<> allocator;
    map<std::string_view, owning_ptr<FunctionInfo>> functions;
    map<std::string_view, owning_ptr<ClassInfo>> classes;
};

ClangToGraphMLBuilder::ClangToGraphMLBuilder(
    std::pmr::memory_resource& memory_resource)
    : m_resource(memory_resource), m_allocator(&memory_resource)
{
}

void ClangToGraphMLBuilder::add_parse_job(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    m_jobs.push_back(static_cast<Job*>(make_owning_with<Job>(
        m_allocator, arena_initial_size_bytes, &m_resource)));
    m_jobs.back()->do_file(filename, command_args);
}

void ClangToGraphMLBuilder::Job::do_file(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit =
        clang_parseTranslationUnit(index, filename, command_args.data(),
                                   static_cast<int>(command_args.size()),
                                   nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {

        auto&& _unused =
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

        CXString string = clang_formatDiagnostic(
            diagnostic,
            CXDiagnosticDisplayOptions::CXDiagnostic_DisplaySourceLocation);
        auto&& _unused =
            fprintf(stderr, "DIAGNOSTIC - Encountered while parsing %s: %s\n",
                    filename, clang_getCString(string));
        clang_disposeString(string);
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);

    clang_visitChildren(
        cursor, // Root cursor
        [](CXCursor current_cursor, CXCursor parent,
           void* userdata) -> enum CXChildVisitResult {
            // clang_getCursorUSR();

            enum CXCursorKind kind = clang_getCursorKind(current_cursor);

            switch (kind) {
            case CXCursorKind::CXCursor_FunctionDecl: {
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

                auto is_function = kind == CXCursor_CXXMethod ||
                                   kind == CXCursor_FunctionDecl ||
                                   kind == CXCursor_Constructor ||
                                   kind == CXCursor_Destructor ||
                                   kind == CXCursor_ConversionFunction;

                if (!is_function) {
                    auto name = [&]() -> std::optional<const char*> {
                        switch (kind) {
                        case CXCursor_FirstInvalid:
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

                    // TODO: removeme? maybe good to print these
                    if (name) {
                        break;
                    }

                    auto callsym = clang_getCursorSpelling(current_cursor);
                    auto defsym = clang_getCursorSpelling(function);
                    auto&& _unused = fprintf(
                        stderr,
                        "Found callexpr which does not reference a function: "
                        "callsym is %s and defsym is %s with kind %d named "
                        "%s\n",
                        clang_getCString(callsym), clang_getCString(defsym),
                        static_cast<int>(kind), name.value_or("unknown"));
                    clang_disposeString(callsym);
                    clang_disposeString(defsym);
                    break;
                }

                // this is a function call and we have the cursor to the called
                // function definition

                break;
            }
            default:
                // do nothing
                break;
            }

            // CXString current_display_name =
            //     clang_getCursorDisplayName(current_cursor);
            // printf("Visiting element %s\n",
            //        current_display_name);
            // clang_disposeString(current_display_name);
            return CXChildVisit_Recurse;
        },
        nullptr // userdata
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

bool ClangToGraphMLBuilder::finish_and_write(std::ostream& output) noexcept
{
    return false;
}

} // namespace cn
