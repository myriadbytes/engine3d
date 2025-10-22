from pathlib import Path
import subprocess
import shutil

# CONFIG
exe_name = "win32_game.exe"
game_dll_name = "game.dll"
defines = {"ENGINE_SLOW": "1", "ENGINE_INTERNAL": "1"}
compiler_flags = ["-g", "-fdiagnostics-absolute-paths", "-Wall", "-Wno-missing-braces"]
linker_flags = [""]
syslibs = ["user32.lib", "d3d12.lib", "dxgi.lib", "d3dcompiler.lib", "GameInput.lib"]
game_dll_exports = ["gameUpdate"]

# FILES
platform_source_files = ["src/win32_platform.cpp"]
game_source_files = ["src/game.cpp"]
common_source_files = ["src/arena.cpp", "src/maths.cpp"]

lib_directories = ["firstparty/Microsoft/lib/x64"]
assets_directories = ["shaders"]

# SETUP
project_dir = Path(__file__).parent
build_dir = project_dir / "build"

build_dir.mkdir(exist_ok=True)
for asset_dir in assets_directories:
    to_copy = project_dir / asset_dir
    where = build_dir / asset_dir
    _ = shutil.copytree(to_copy, where, dirs_exist_ok=True)

# COMMON
defines_str = " ".join([f"-D{name}={value}" for (name, value) in defines.items()])
compiler_flags_str = " ".join(compiler_flags)
linker_flags_str = " ".join(linker_flags)
common_sources_str = " ".join(
    [str(project_dir / source) for source in common_source_files]
)

# PLATFORM EXE
platform_sources_str = " ".join(
    [str(project_dir / source) for source in platform_source_files]
)
libs_directories_str = " ".join(
    [f'-L"{str(project_dir / lib_dir)}"' for lib_dir in lib_directories]
)
platform_output_str = f"-o {str(build_dir / exe_name)}"
syslibs_str = " ".join([f"-l{name}" for name in syslibs])

platform_compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{platform_sources_str} \
{common_sources_str} \
{platform_output_str} \
{linker_flags_str} \
{libs_directories_str} \
{syslibs_str} \
"

print("BUILDING PLATFORM...")
platform_result = subprocess.run(
    platform_compile_cmd, shell=True, capture_output=True, text=True
)

platform_success = True
if len(platform_result.stderr) > 0:
    platform_success = False
    # CLANG ERRORS
    print(platform_result.stderr.strip())
    # WINDOWS LINKER ERRORS
    print(platform_result.stdout.strip())

# GAME DLL
game_sources_str = " ".join([str(project_dir / source) for source in game_source_files])
game_output_str = f"-o {str(build_dir / game_dll_name)}"
game_dll_exports_str = " ".join(
    [f"-Wl,/EXPORT:{game_dll_export}" for game_dll_export in game_dll_exports]
)

game_compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{game_sources_str} \
{common_sources_str} \
{game_output_str} \
-shared \
{linker_flags_str} \
{game_dll_exports_str} \
"

print("BUILDING GAME DLL...")
game_result = subprocess.run(
    game_compile_cmd, shell=True, capture_output=True, text=True
)

game_success = True
if len(game_result.stderr) > 0:
    game_success = False
    # CLANG ERRORS
    print(game_result.stderr.strip())
    # WINDOWS LINKER ERRORS
    print(game_result.stdout.strip())

    # OUTPUT
if platform_success and game_success:
    print("BUILD SUCCESS")
    exit(0)
else:
    exit(-1)
