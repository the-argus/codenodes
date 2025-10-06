#include <algorithm>
#include <argz/argz.hpp>
#include <cstring>
#include <fstream>
#include <print>
#include <ranges>

#include "clang_to_graphml.h"
#include "compile_command_entry.h"

namespace {
template <typename LHS, typename RHS>
constexpr bool string_view_compare(LHS a, RHS b)
{
    if (a.size() != b.size()) {
        return false;
    }

    return ::memcmp(a.data(), b.data(), std::min(a.size(), b.size())) == 0;
}
} // namespace

class DebugMonotonicBuffer : public std::pmr::monotonic_buffer_resource
{
  public:
    [[nodiscard]] size_t amount_reported_freed() const
    {
        return bytes_theoretically_freed;
    };

    [[nodiscard]] size_t amount_reported_allocated() const
    {
        return bytes_theoretically_allocated;
    };

  protected:
    void do_deallocate(void* mem, size_t bytes, size_t alignment) final
    {
        bytes_theoretically_freed += bytes;

        // std::ignore = std::fprintf(stderr, "Leaked %zu bytes\n", bytes);

        std::pmr::monotonic_buffer_resource::do_deallocate(mem, bytes,
                                                           alignment);
    }

    void* do_allocate(size_t bytes, size_t alignment) final
    {
        bytes_theoretically_allocated += bytes;
        return std::pmr::monotonic_buffer_resource::do_allocate(bytes,
                                                                alignment);
    }

  private:
    size_t bytes_theoretically_allocated = 0;
    size_t bytes_theoretically_freed = 0;
};

int main(int argc, const char* argv[])
{
    constexpr std::string_view version = "0.0.1";
    argz::about about{
        .description = "A program to parse a large c++ codebase and "
                       "visualize it as a graph of connected nodes.",
        .version = version,
        .print_help_when_no_options = false,
    };

    std::optional<std::string> compile_commands_path{};
    std::optional<std::string> output_file_path{};
    argz::options opts{
        {
            .ids = {.id = "compile_commands", .alias = 'c'},
            .value = compile_commands_path,
            .help = "path to a compile_commands.json file which describes the "
                    "source files and headers which should be included in "
                    "visualization",
        },
        {
            .ids = {.id = "output", .alias = 'o'},
            .value = output_file_path,
            .help = "path to the output GraphML file",
        },
    };

    try {
        argz::parse(about, opts, argc, argv);
    } catch (const std::exception& e) {
        std::ignore =
            fprintf(stderr, "Bad command line arguments: %s\n", e.what());
        return EXIT_FAILURE;
    }

    if (!output_file_path.has_value()) {
        std::ignore = fprintf(stderr, "Provide an output _file\n");
        return EXIT_FAILURE;
    }

    std::ofstream output_file(output_file_path.value());

    if (!output_file) {
        std::ignore =
            fprintf(stderr, "Unable to open output file %s for writing.\n",
                    output_file_path.value().c_str());
        return EXIT_FAILURE;
    }

    const std::string_view cc_path =
        compile_commands_path
            .transform([](auto& str) { return std::string_view{str}; })
            .value_or("compile_commands.json");

    auto maybe_ccs = cn::parse_compile_commands_json_file(cc_path);
    if (!maybe_ccs) {
        maybe_ccs =
            cn::parse_compile_commands_json_file_separated_args(cc_path);
    }
    if (!maybe_ccs) {
        return 1;
    }
    auto& ccs = maybe_ccs.value();

    // all memory is leaked, we do not free anything throughout the whole
    // program, though we can free it all at the end of this function
    DebugMonotonicBuffer memory_resource{};

    cn::ClangToGraphMLBuilder graph_builder(memory_resource);

    for (const auto& entry : ccs) {

        const auto split_on_spaces = std::views::split(' ');

        const auto to_string_view =
            std::views::transform([](auto a) { return std::string_view{a}; });

        const auto remove_empty_strings =
            std::views::filter([](auto sv) { return !sv.empty(); });

        auto view = entry.command | split_on_spaces | to_string_view |
                    remove_empty_strings | std::views::common;

        std::vector<std::string> args_storage = {view.begin(), view.end()};

        auto to_ptr = args_storage | std::views::transform(&std::string::c_str);

        // clang wants null term strings, no sviews, so we have to copy out of
        // the single `command` string to make them all null terminated
        std::vector<const char*> args(to_ptr.begin(), to_ptr.end());

        graph_builder.parse(entry.file.c_str(), args);
    }

    auto return_code =
        graph_builder.finish(output_file) ? EXIT_SUCCESS : EXIT_FAILURE;

    int64_t in_use = int64_t(memory_resource.amount_reported_allocated()) -
                     int64_t(memory_resource.amount_reported_freed());
    double percentage_leaked =
        double(memory_resource.amount_reported_freed()) /
        double(memory_resource.amount_reported_allocated());

    std::printf(
        "Stats:\n\t- %zu bytes supposedly freed\n\t- %zu supposedly allocated"
        "\n\t- %ld bytes supposedly in use at program end\n",
        memory_resource.amount_reported_freed(),
        memory_resource.amount_reported_allocated(), in_use);
    std::printf("Percentage leaked: %f %%\n", (percentage_leaked * 100));

    return return_code;
}
