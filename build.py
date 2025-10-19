from pathlib import Path
import subprocess

# CONFIG
exe_name = "win32_game.exe"
defines = {"ENGINE_SLOW": "1", "ENGINE_INTERNAL": "1"}
compiler_flags = ["-g", "-fdiagnostics-absolute-paths"]
syslibs = ["user32.lib", "d3d12.lib", "dxgi.lib", "d3dcompiler.lib"]

# FILES
source_files = ["win32_platform.cpp"]


# SCRIPT
def sourceNameToAbsPath(source: str) -> Path:
    return src_dir / source


project_dir = Path(__file__).parent
src_dir = project_dir / "src"
build_dir = project_dir / "build"

build_dir.mkdir(exist_ok=True)
sources_str = " ".join([str(sourceNameToAbsPath(source)) for source in source_files])
defines_str = " ".join([f"-D{name}={value}" for (name, value) in defines.items()])
compiler_flags_str = " ".join(compiler_flags)
output_str = f"-o {str(build_dir / exe_name)}"
syslibs_str = " ".join([f"-l{name}" for name in syslibs])

compile_cmd = f"clang \
{defines_str} \
{compiler_flags_str} \
{sources_str} \
{output_str} \
{syslibs_str} \
"

result = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True)
if len(result.stdout) > 0:
    print("BUILD ERROR")
    print("")
    print(result.stdout.strip())
    exit(-1)
else:
    print("BUILD SUCCESS")
    exit(0)
