const std = @import("std");
const builtin = @import("builtin");
const zcc = @import("compile_commands");

const release_flags = &[_][]const u8{
    "-DNDEBUG",
    "-std=c++20",
};

const debug_flags = &[_][]const u8{
    "-g",
    "-std=c++20",
};

const headers = &[_][]const u8{};

const src_files = &[_][]const u8{
    "src/main.cpp",
};

pub fn build(b: *std.Build) !void {
    // options
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    var flags = std.ArrayList([]const u8).init(b.allocator);
    defer flags.deinit();
    try flags.appendSlice(if (optimize == .Debug) debug_flags else release_flags);

    const raylib = b.dependency("raylib", .{
        .target = target,
        .optimize = optimize,
        .linux_display_backend = .X11,
    });

    const fmt = b.dependency("fmt", .{});
    const fmt_include_path = b.pathJoin(&.{ fmt.builder.install_path, "include" });
    try flags.append(b.fmt("-I{s}", .{fmt_include_path}));

    // this is just for compile_commands.json. if you dont define
    // LIBCLANG_INCLUDE_DIR thats fine, you just will get  lots of intellisense
    // problems
    {
        var env_map = try std.process.getEnvMap(b.allocator);
        defer env_map.deinit();

        var iter = env_map.iterator();
        while (iter.next()) |entry| {
            if (std.mem.eql(u8, entry.key_ptr.*, "LIBCLANG_INCLUDE_DIR")) {
                try flags.append(b.fmt("-I{s}", .{entry.value_ptr.*}));
                break;
            }
        }
    }

    var executables = std.ArrayList(*std.Build.Step.Compile).init(b.allocator);
    defer executables.deinit();

    const flags_owned = flags.toOwnedSlice() catch @panic("OOM");

    const codenodes = b.addExecutable(.{
        .target = target,
        .optimize = optimize,
        .name = "codenodes",
    });
    codenodes.linkLibrary(raylib.artifact("raylib"));
    try executables.append(codenodes);

    codenodes.addCSourceFiles(.{
        .files = src_files,
        .flags = flags_owned,
    });
    codenodes.linkLibCpp();
    codenodes.linkSystemLibrary2("clang", .{
        .use_pkg_config = .yes, // doesnt succeed with nix
        .preferred_link_mode = .static,
    });

    const install = b.getInstallStep();
    for (executables.items) |exe| {
        const exe_install = b.addInstallArtifact(exe, .{});
        install.dependOn(&exe_install.step);
    }

    const cdbstep = zcc.createStep(b, "cdb", try executables.toOwnedSlice());
    b.getInstallStep().dependOn(cdbstep);
}
