Import("env")
from pathlib import Path

root = Path(env.subst("$PROJECT_DIR"))
runtime = root.parent / "runtime"
generated = Path(env.subst("$BUILD_DIR")) / "generated"
generated.mkdir(parents=True, exist_ok=True)
(generated / "zconf.h").write_bytes((runtime / "deps/zlib/zconf.h.in").read_bytes())
env.Append(CPPPATH=[str(generated)])
core = ["pd_graphics.c", "pd_display.c", "pd_input.c", "pd_system.c", "pd_file.c",
        "pd_import.c", "pd_geometry.c", "pd_sprite.c", "pd_modules.c", "pd_sdkmenu.c", "pd_menu.c"]
env.BuildSources("$BUILD_DIR/shared-core", str(runtime / "src"),
                 src_filter=["-<*>"] + [f"+<{name}>" for name in core])
env.BuildSources("$BUILD_DIR/shared-lua", str(runtime / "vendor/lua"),
                 src_filter=["+<*.c>", "-<lua.c>", "-<luac.c>"])
env.BuildSources("$BUILD_DIR/shared-zlib", str(runtime / "deps/zlib"),
                 src_filter=["-<*>"] + [f"+<{name}>" for name in
                 ["adler32.c", "crc32.c", "inflate.c", "inftrees.c", "inffast.c", "uncompr.c", "zutil.c"]])
