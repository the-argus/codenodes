#include <clang-c/Index.h>
#include <cstdio>
#include <thread>
#include <unordered_map>

#include "translation_unit.h"

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
    return owning_ptr(
        allocator.allocate_object<T>(std::forward<args_t>(args)...));
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

struct ClangToGraphMLBuilder::ThreadContext
{
    std::pmr::polymorphic_allocator<> allocator;
    map<std::string_view, owning_ptr<FunctionInfo>> functions;
    map<std::string_view, owning_ptr<ClassInfo>> classes;
};

struct ClangToGraphMLBuilder::Job
{
    template <typename... args_t>
    explicit Job(args_t&&... args)
        : memory_resource{std::forward<args_t>(args)...}
    {
    }

    std::jthread thread;
    std::pmr::monotonic_buffer_resource memory_resource;
    std::optional<ThreadContext> ctx; // deferred initialize
};

ClangToGraphMLBuilder::ClangToGraphMLBuilder(
    std::pmr::memory_resource& memory_resource)
    : m_cindex(clang_createIndex(0, 0)), m_resource(memory_resource),
      m_allocator(&memory_resource)
{
}

ClangToGraphMLBuilder::~ClangToGraphMLBuilder()
{
    clang_disposeIndex(m_cindex);
    for (auto& job : m_jobs) {
        job->thread.join();
        m_allocator.deallocate_object(job);
        job = nullptr;
    }
}

void ClangToGraphMLBuilder::spawn_parse_job(
    const char* filename, std::span<const char* const> command_args) noexcept
{
    Job* job = std::construct_at(m_allocator.allocate_object<Job>(),
                                 arena_initial_size_bytes, &m_resource);

    job->ctx.emplace(std::pmr::polymorphic_allocator<>(&job->memory_resource));

    // actually start execution
    job->thread = std::jthread(ClangToGraphMLBuilder::thread_entry, m_cindex,
                               &job->ctx.value(), filename, command_args);

    m_jobs.push_back(job);
}

void ClangToGraphMLBuilder::thread_entry(
    void* cindex, ThreadContext* ctx, const char* filename,
    std::span<const char* const> command_args) noexcept
{
    CXTranslationUnit unit =
        clang_parseTranslationUnit(cindex, filename, command_args.data(),
                                   static_cast<int>(command_args.size()),
                                   nullptr, 0, CXTranslationUnit_None);

    if (unit == nullptr) {
        auto&& _unused =
            fprintf(stderr, "Unable to parse translation unit %s, aborting.\n",
                    filename);
        clang_disposeTranslationUnit(unit);
        return;
    }

    // warn for diagnostics
    CXDiagnosticSet diagnostics = clang_getDiagnosticSetFromTU(unit);
    for (size_t i = 0; i < clang_getNumDiagnostics(unit); ++i) {
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

            CXString current_display_name =
                clang_getCursorDisplayName(current_cursor);
            printf("Visiting element %s\n",
                   clang_getCString(current_display_name));
            clang_disposeString(current_display_name);
            return CXChildVisit_Recurse;
        },
        nullptr // userdata
    );

    clang_disposeTranslationUnit(unit);
}

bool ClangToGraphMLBuilder::finish_and_write(std::ostream& output) noexcept
{
    return false;
}

} // namespace cn
