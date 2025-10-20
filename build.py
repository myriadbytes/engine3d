from pathlib import Path
import subprocess

# CONFIG
exe_name = "win32_game.exe"
defines = {"ENGINE_SLOW": "1", "ENGINE_INTERNAL": "1"}
compiler_flags = ["-g", "-fdiagnostics-absolute-paths", "-Wall", "-Wno-missing-braces"]
linker_flags = [""]
syslibs = ["user32.lib", "d3d12.lib", "dxgi.lib", "d3dcompiler.lib", "GameInput.lib"]

# FILES
source_files = ["win32_platform.cpp"]
lib_directories = ["src/Microsoft/lib/x64"]


# SCRIPT
def sourceNameToAbsPath(source: str) -> Path:
    return src_dir / source


def projectPathToAbsPath(path: str) -> Path:
    return project_dir / path


project_dir = Path(__file__).parent
src_dir = project_dir / "src"
build_dir = project_dir / "build"

build_dir.mkdir(exist_ok=True)
sources_str = " ".join([str(sourceNameToAbsPath(source)) for source in source_files])
defines_str = " ".join([f"-D{name}={value}" for (name, value) in defines.items()])
compiler_flags_str = " ".join(compiler_flags)
linker_flags_str = " ".join(linker_flags)
libs_directories_str = " ".join(
    [f'-L"{str(projectPathToAbsPath(lib_dir))}"' for lib_dir in lib_directories]
)
output_str = f"-o {str(build_dir / exe_name)}"
syslibs_str = " ".join([f"-l{name}" for name in syslibs])

compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{sources_str} \
{output_str} \
{linker_flags_str} \
{libs_directories_str} \
{syslibs_str} \
"

print("BUILDING...")
result = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True)

if len(result.stderr) > 0:
    # CLANG ERRORS
    print(result.stderr.strip())
    # WINDOWS LINKER ERRORS
    print(result.stdout.strip())
    exit(-1)
else:
    print("BUILD SUCCESS")
    exit(0)
