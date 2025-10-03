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

    std::pmr::synchronized_pool_resource resource{};

    cn::ClangToGraphMLBuilder graph_builder(resource);

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

    return graph_builder.finish(output_file) ? EXIT_SUCCESS : EXIT_FAILURE;
}
