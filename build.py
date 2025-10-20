from pathlib import Path
import subprocess

# CONFIG
exe_name = "win32_game.exe"
game_dll_name = "game.dll"
defines = {"ENGINE_SLOW": "1", "ENGINE_INTERNAL": "1"}
compiler_flags = ["-g", "-fdiagnostics-absolute-paths", "-Wall", "-Wno-missing-braces"]
linker_flags = [""]
syslibs = ["user32.lib", "d3d12.lib", "dxgi.lib", "d3dcompiler.lib", "GameInput.lib"]
game_dll_exports = ["gameUpdate"]

# FILES
platform_source_files = ["win32_platform.cpp"]
game_source_files = ["game.cpp"]
lib_directories = ["src/Microsoft/lib/x64"]


# SETUP
def sourceNameToAbsPath(source: str) -> Path:
    return src_dir / source


def projectPathToAbsPath(path: str) -> Path:
    return project_dir / path


project_dir = Path(__file__).parent
src_dir = project_dir / "src"
build_dir = project_dir / "build"

build_dir.mkdir(exist_ok=True)

# COMMON
defines_str = " ".join([f"-D{name}={value}" for (name, value) in defines.items()])
compiler_flags_str = " ".join(compiler_flags)
linker_flags_str = " ".join(linker_flags)

# PLATFORM EXE
platform_sources_str = " ".join(
    [str(sourceNameToAbsPath(source)) for source in platform_source_files]
)
libs_directories_str = " ".join(
    [f'-L"{str(projectPathToAbsPath(lib_dir))}"' for lib_dir in lib_directories]
)
platform_output_str = f"-o {str(build_dir / exe_name)}"
syslibs_str = " ".join([f"-l{name}" for name in syslibs])

platform_compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{platform_sources_str} \
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
game_sources_str = " ".join(
    [str(sourceNameToAbsPath(source)) for source in game_source_files]
)
game_output_str = f"-o {str(build_dir / game_dll_name)}"
game_dll_exports_str = " ".join(
    [f"-Wl,/EXPORT:{game_dll_export}" for game_dll_export in game_dll_exports]
)

game_compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{game_sources_str} \
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
if platform_success:
    print("BUILD SUCCESS")
    exit(0)
else:
    exit(-1)
