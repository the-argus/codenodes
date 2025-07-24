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

const src_files = &[_][]const u8{};

pub fn build(b: *std.Build) !void {
    // options
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    var flags = std.ArrayList([]const u8).init(b.allocator);
    defer flags.deinit();
    try flags.appendSlice(if (optimize == .Debug) debug_flags else release_flags);

    const fmt = b.dependency("fmt", .{});
    const fmt_include_path = b.pathJoin(&.{ fmt.builder.install_path, "include" });
    try flags.append(b.fmt("-I{s}", .{fmt_include_path}));

    var executables = std.ArrayList(*std.Build.Step.Compile).init(b.allocator);
    defer executables.deinit();

    const flags_owned = flags.toOwnedSlice() catch @panic("OOM");

    const codenodes = b.addExecutable(.{
        .target = target,
        .optimize = optimize,
        .name = "codenodes",
    });
    executables.append(codenodes);

    codenodes.addCSourceFiles(.{
        .files = src_files,
        .flags = flags_owned,
    });
    codenodes.linkLibCpp();
    codenodes.linkSystemLibrary2("unwind", .{
        .use_pkg_config = true,
        .preferred_link_mode = .static,
    });

    const install = b.getInstallStep();
    for (executables.items) |exe| {
        const exe_install = b.addInstallArtifact(exe, .{});
        install.dependOn(&exe_install.step);
    }

    const cdbstep = zcc.createStep(b, "cdb", try codenodes.toOwnedSlice());
    b.getInstallStep().dependOn(&cdbstep.step);
}
