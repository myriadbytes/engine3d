import shutil
import subprocess
from pathlib import Path

# CONFIG
exe_name = "win32_game.exe"
game_dll_name = "game.dll"
defines = {"ENGINE_SLOW": "1", "ENGINE_INTERNAL": "1"}
compiler_flags = [
    "-g",
    # "-O3",
    "-fdiagnostics-absolute-paths",
    "-Wall",
    "-Wno-missing-braces",
    "-std=c++20",
    "-DNOMINMAX=1",  # including <Windows.h> name-squats common identifiers if this is not defined
]
linker_flags = [
    "-Wl,/LTCG",  # windows linker flag to remove indirection in dll calls, makes debugging easier
]

vk_sdk_root = Path("C:/VulkanSDK/")
vk_sdks = list(vk_sdk_root.glob("*"))
if len(vk_sdks) == 0:
    raise RuntimeError("No Vulkan SDK found in C:/VulkanSDK")
vk_sdks.sort()
vk_sdk_directory = vk_sdks[-1]

syslibs = ["user32.lib", "vulkan-1.lib", "gameinput.lib"]
syslibs_directories = [str(vk_sdk_directory / "Lib")]
include_directories = [str(vk_sdk_directory / "Include")]

game_dll_exports = ["gameUpdate"]
generate_compile_commands = True

# FILES
platform_source_files = ["src/win32_platform.cpp"]
game_source_files = [
    "src/game.cpp",
    "src/allocators.cpp",
    "src/maths.cpp",
    "src/noise.cpp",
    "src/world.cpp",
    "src/img.cpp",
    "src/gpu.cpp",
]
common_source_files = []

assets_directories = ["assets"]
shader_directories = ["shaders"]

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
syslibs_str = " ".join([f"-l{name}" for name in syslibs])
syslibs_directories_str = " ".join([f"-L{dir}" for dir in syslibs_directories])
include_directories_str = " ".join([f"-I{dir}" for dir in include_directories])

# PLATFORM EXE
platform_sources_str = " ".join(
    [str(project_dir / source) for source in platform_source_files]
)
platform_output_str = f"-o {str(build_dir / exe_name)}"

platform_compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{platform_sources_str} \
{common_sources_str} \
{platform_output_str} \
{linker_flags_str} \
{syslibs_str} \
{syslibs_directories_str} \
{include_directories_str} \
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
{syslibs_str} \
{syslibs_directories_str} \
{include_directories_str} \
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

# COMPILE_COMMANDS.JSON
if generate_compile_commands:
    compile_commands = ""
    compile_commands += "[\n"

    all_source_files = platform_source_files + game_source_files + common_source_files
    for i, file in enumerate(all_source_files):
        compile_commands += "{\n"

        compile_commands += f'"directory": "{str(project_dir).replace("\\", "/")}",\n'
        compile_commands += f'"command": "clang {defines_str} {compiler_flags_str} -c {str(project_dir / file).replace("\\", "/")} {include_directories_str.replace("\\", "/")}"\n,'
        compile_commands += f'"file": "{file}"\n'

        compile_commands += "}"
        if i != len(all_source_files) - 1:
            compile_commands += ","
        compile_commands += "\n"

    compile_commands += "]"

    _ = Path(project_dir / "compile_commands.json").write_text(compile_commands)

# SHADER COMPILATION
shader_success = True
for shader_dir in shader_directories:
    shader_out_dir = build_dir / shader_dir
    shader_out_dir.mkdir(exist_ok=True)

    shader_sources: list[Path] = []
    for vertex_shader_source in (project_dir / shader_dir).glob("*.vert"):
        shader_sources.append(vertex_shader_source)
    for fragment_shader_source in (project_dir / shader_dir).glob("*.frag"):
        shader_sources.append(fragment_shader_source)

    for shader_source in shader_sources:
        source_file_str = str(shader_source)
        output_file_str = str(shader_out_dir / (shader_source.name + ".spv"))
        bin_str = str(project_dir / "tools" / "glslc.exe")
        shader_cmd = f"{bin_str} {source_file_str} -o {output_file_str}"
        shader_result = subprocess.run(
            shader_cmd, shell=True, capture_output=True, text=True
        )
        if len(shader_result.stderr) > 0:
            shader_success = False
            print(shader_result.stderr.strip())

# OUTPUT
if platform_success and game_success and shader_success:
    print("BUILD SUCCESS")
    exit(0)
else:
    exit(-1)
