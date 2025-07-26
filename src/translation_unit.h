#ifndef __CODENODES_TRANSLATION_UNIT_H__
#define __CODENODES_TRANSLATION_UNIT_H__

#include <fstream>
#include <memory_resource>
#include <span>
#include <vector>

namespace cn {
class ClangToGraphMLBuilder
{
  public:
    // NOTE: this memory resource must be threadsafe
    explicit ClangToGraphMLBuilder(std::pmr::memory_resource& memory_resource);
    ClangToGraphMLBuilder(const ClangToGraphMLBuilder&) = delete;
    ClangToGraphMLBuilder& operator=(const ClangToGraphMLBuilder&) = delete;
    ClangToGraphMLBuilder(ClangToGraphMLBuilder&&) = delete;
    ClangToGraphMLBuilder& operator=(ClangToGraphMLBuilder&&) = delete;
    ~ClangToGraphMLBuilder();

    // always succeeds. only when you go to wait on all the jobs do you find the
    // results for everything
    // NOTE: the strings pointed at by filename and command_args must live until
    // after `finish_and_write` is called.
    void spawn_parse_job(const char* filename,
                         std::span<const char* const> command_args) noexcept;

    /// If there are undefined symbols, or any given job failed perhaps due to
    /// compilation errors, this returns false. Otherwise it writes GraphML XML
    /// to the output stream
    [[nodiscard]] bool finish_and_write(std::ostream& output) noexcept;

  private:
    struct ThreadContext;
    struct Job; // contains a jthread and a ThreadContext
    static void
    thread_entry(void* cindex, ThreadContext* ctx, const char* filename,
                 std::span<const char* const> command_args) noexcept;

    // clang compilation index, shared, apparently its threadsafe
    void* m_cindex;
    std::pmr::memory_resource& m_resource; // backs the other allocators
    std::pmr::polymorphic_allocator<> m_allocator;
    // owning pointers, these are all freed on destruction
    std::pmr::vector<Job*> m_jobs;

    static constexpr size_t arena_initial_size_bytes = 1000000;
};

} // namespace cn

#endif
