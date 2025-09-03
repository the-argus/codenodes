#include <cassert>
#include <clang-c/Index.h>
#include <cstdio>
#include <span>
#include <unordered_map>
#include <utility>

#include "clang_to_graphml.h"

// using std::pmr but these should all be backed by monotonic buffers
template <typename T> using Vector = std::pmr::vector<T>;
using String = std::pmr::string;
template <typename K, typename V> using Map = std::pmr::unordered_map<K, V>;
using Allocator = std::pmr::polymorphic_allocator<>;
using MemoryResource = std::pmr::memory_resource;
// resource user per parse job
using JobResource = std::pmr::unsynchronized_pool_resource;

template <typename T> class OwningPointer
{
  public:
    constexpr T* operator->() noexcept { return ptr; }
    constexpr T& operator*() noexcept { return *ptr; }
    constexpr explicit operator bool() noexcept { return ptr; }

    template <typename... ConstructorArgs>
        requires std::is_constructible_v<T, ConstructorArgs...>
    explicit constexpr OwningPointer(Allocator& _allocator,
                                     ConstructorArgs&&... args)
        : allocator(std::addressof(_allocator)),
          ptr(_allocator.new_object<T>(std::forward<ConstructorArgs>(args)...))
    {
    }

    constexpr OwningPointer(OwningPointer&& other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)), allocator(other.allocator)
    {
    }

    constexpr OwningPointer& operator=(OwningPointer&& other) noexcept
    {
        allocator->delete_object(ptr);
        ptr = std::exchange(other.ptr, nullptr);
        allocator = other.allocator;
    }

    OwningPointer(const OwningPointer&) = delete;
    OwningPointer& operator=(const OwningPointer&) = delete;

    constexpr ~OwningPointer()
    {
        if (ptr) {
            allocator->delete_object(ptr);
        }
    }

  private:
    T* ptr;
    Allocator* allocator;
};

struct OwningCXString : private CXString
{
    constexpr static OwningCXString clang_getCursorUSR(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorUSR(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorSpelling(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorSpelling(cursor));
    }

    constexpr static OwningCXString
    clang_getCursorDisplayName(const CXCursor& cursor)
    {
        return OwningCXString(::clang_getCursorDisplayName(cursor));
    }

    constexpr static OwningCXString
    clang_formatDiagnostic(CXDiagnostic diagnostic,
                           CXDiagnosticDisplayOptions display_options)
    {
        return OwningCXString(
            ::clang_formatDiagnostic(diagnostic, display_options));
    }

    constexpr String copy_to_string(Allocator& allocator)
    {
        return {c_str(), allocator};
    }

    OwningCXString() = delete;
    constexpr explicit OwningCXString(CXString nonowning) : CXString(nonowning)
    {
    }

    constexpr OwningCXString(OwningCXString&& other) noexcept : CXString(other)
    {
        other.data = nullptr;
        other.private_flags = 0;
    }

    constexpr OwningCXString& operator=(OwningCXString&& other) noexcept
    {
        data = std::exchange(other.data, nullptr);
        private_flags = std::exchange(other.private_flags, 0);
        return *this;
    }

    OwningCXString(const OwningCXString& other) = delete;
    OwningCXString& operator=(const OwningCXString& other) = delete;

    const char* c_str() noexcept { return clang_getCString(*this); }
    constexpr std::string_view view() noexcept
    {
        return {clang_getCString(*this)};
    }

    constexpr ~OwningCXString() { clang_disposeString(*this); }
};

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
    constexpr Symbol(SymbolType _type, String&& _name, bool _defined)
        : type(_type), name(std::move(_name)), defined(_defined)
    {
    }

    SymbolType type;
    String name;
    bool defined = false; // this symbol may be incomplete
};

struct ClassInfo;
struct FunctionInfo;
struct EnumInfo;

struct FunctionInfo : public Symbol
{
    constexpr FunctionInfo(SymbolType _type, String&& _name, bool _defined)
        : Symbol(_type, std::move(_name), _defined),
          was_forward_declared(!_defined)
    {
    }

    ClassInfo* parent = nullptr; // defined if this is a method
    bool has_overloads{};
    const bool was_forward_declared;
};

struct ClassInfo : public Symbol
{
    Vector<FunctionInfo*> member_function_symbols;
};

struct ClangToGraphMLBuilder::Data
{
    // data that persists between calls to parse
    std::vector<OwningPointer<Job>> finished_jobs;
};

struct ClangToGraphMLBuilder::Job
{
    explicit Job(MemoryResource* res)
        : resource(res), allocator(&resource), functions(allocator),
          classes(allocator)
    {
    }

    void run(const char* filename,
             std::span<const char* const> command_args) noexcept;

    Map<String, FunctionInfo*> functions;
    Map<String, ClassInfo*> classes;
    Allocator allocator;
    JobResource resource;
};

ClangToGraphMLBuilder::ClangToGraphMLBuilder(MemoryResource& memory_resource)
    : m_resource(memory_resource), m_allocator(&memory_resource),
      m_data(m_allocator.new_object<Data>())
{
}

ClangToGraphMLBuilder::~ClangToGraphMLBuilder()
{
    m_allocator.delete_object(m_data);
}

void ClangToGraphMLBuilder::parse(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    OwningPointer<Job> job(m_allocator, &m_resource);
    job->run(filename, command_args);
    m_data->finished_jobs.emplace_back(std::move(job));
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

    clang_visitChildren(
        cursor, // Root cursor
        [](CXCursor current_cursor, CXCursor parent,
           void* userdata) -> enum CXChildVisitResult {
            auto usr = OwningCXString::clang_getCursorUSR(current_cursor);
            auto display =
                OwningCXString::clang_getCursorDisplayName(current_cursor);
            if (usr.view().length() > 0) {
                std::ignore = fprintf(
                    stdout,
                    "found cursor with USR: %s \nand display name: %s\n",
                    usr.c_str(), display.c_str());
            } else {
                return CXChildVisit_Recurse;
            }

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

                    auto callsym =
                        OwningCXString::clang_getCursorSpelling(current_cursor);
                    auto defsym =
                        OwningCXString::clang_getCursorSpelling(function);
                    std::ignore = fprintf(
                        stderr,
                        "Found callexpr which does not reference a function: "
                        "callsym is %s and defsym is %s with kind %d named "
                        "%s\n",
                        callsym.c_str(), defsym.c_str(), static_cast<int>(kind),
                        name.value_or("unknown"));
                    break;
                }

                break;
            }
            default:
                // do nothing
                break;
            }

            return CXChildVisit_Recurse;
        },
        nullptr // userdata
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

bool ClangToGraphMLBuilder::finish(std::ostream& output) noexcept
{
    return false;
}

} // namespace cn
