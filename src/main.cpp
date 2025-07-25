#include <argz/argz.hpp>
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
    argz::options opts{
        {.ids = {.id = "compile_commands", .alias = 'c'},
         .value = compile_commands_path,
         .help =
             "path to a compile_commands.json file which describes the source "
             "files and headers which should be included in visualization"},
    };

    try {
        argz::parse(about, opts, argc, argv);
    } catch (const std::exception& e) {
        auto&& _unused =
            fprintf(stderr, "Bad command line arguments: %s", e.what());
        return 1;
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

    std::vector<std::string_view> args;
    for (const auto& entry : ccs) {
        args.clear();
        for (auto split : entry.command | std::views::split(' ')) {
            args.emplace_back(split);
        }

        if (auto maybeTranslationUnit =
                cn::TranslationUnit::parse(entry.file.c_str(), args)) {
            auto& translation_unit = maybeTranslationUnit.value();
        }
    }
}
