#include <argz/argz.hpp>
#include <fstream>
#include <ranges>

#include "compile_command_entry.h"
#include "translation_unit.h"

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
    std::string output_file_path{};
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
        auto&& _unused =
            fprintf(stderr, "Bad command line arguments: %s\n", e.what());
        return EXIT_FAILURE;
    }

    std::ofstream output_file(output_file_path);

    if (!output_file) {
        auto&& _unused =
            fprintf(stderr, "Unable to open output file %s for writing.\n",
                    output_file_path.c_str());
        return EXIT_FAILURE;
    }

    const std::string_view cc_path =
        compile_commands_path
            .transform([](auto& str) { return std::string_view{str}; })
            .value_or("compile_commands.json");

    auto maybe_ccs = cn::parse_compile_commands_json_file(cc_path);
    if (!maybe_ccs) {
        return 1;
    }
    auto& ccs = maybe_ccs.value();

    std::vector<const char*> args; // clang wants null term strings, no sviews

    std::pmr::synchronized_pool_resource resource = {};
    cn::ClangToGraphMLBuilder graph_builder(resource);
    for (const auto& entry : ccs) {
        args.clear();
        for (auto split : entry.command | std::views::split(' ')) {
            args.emplace_back(split.data());
        }

        graph_builder.spawn_parse_job(entry.file.c_str(), args);
    }

    if (!graph_builder.finish_and_write(output_file)) {
        // no need to print anything, stderr should happen from failing threads
        return EXIT_FAILURE;
    }
}
