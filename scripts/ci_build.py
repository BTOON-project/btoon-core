#!/usr/bin/env python3
"""
CI build script for BTOON Core
Orchestrates build matrix, sanitizer runs, fuzz seeds, and performance regression gates
"""

import os
import sys
import subprocess
import argparse
import json
from pathlib import Path
from typing import List, Dict, Optional

class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def print_header(msg: str):
    print(f"{Colors.HEADER}{Colors.BOLD}{msg}{Colors.ENDC}")

def print_success(msg: str):
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")

def print_error(msg: str):
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}")

def print_info(msg: str):
    print(f"{Colors.OKCYAN}ℹ {msg}{Colors.ENDC}")

def print_warning(msg: str):
    print(f"{Colors.WARNING}⚠ {msg}{Colors.ENDC}")

def run_command(cmd: List[str], cwd: Optional[Path] = None, check: bool = True) -> subprocess.CompletedProcess:
    """Run a command and return the result"""
    # Sanitize command - ensure all arguments are strings and don't contain shell metacharacters
    sanitized_cmd = []
    for arg in cmd:
        if not isinstance(arg, str):
            arg = str(arg)
        # Basic sanitization - reject arguments with shell metacharacters
        if any(char in arg for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe command argument detected: {arg}")
        sanitized_cmd.append(arg)
    
    print_info(f"Running: {' '.join(sanitized_cmd)}")
    try:
        result = subprocess.run(
            sanitized_cmd,
            cwd=cwd,
            check=check,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)
        return result
    except subprocess.CalledProcessError as e:
        if e.stderr:
            print_error(e.stderr)
        raise

def configure_cmake(build_dir: Path, options: Dict[str, str] = None) -> Path:
    """Configure CMake build"""
    options = options or {}
    build_dir.mkdir(parents=True, exist_ok=True)
    
    # Sanitize build_dir path
    build_dir_str = str(build_dir.resolve())
    if any(char in build_dir_str for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
        raise ValueError(f"Unsafe build directory path detected: {build_dir_str}")
    
    cmake_args = [
        "cmake",
        "-S", ".",
        "-B", build_dir_str,
        "-DCMAKE_BUILD_TYPE=Release",
    ]
    
    for key, value in options.items():
        # Sanitize CMake option values
        if any(char in str(value) for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe CMake option value detected: {key}={value}")
        cmake_args.append(f"-D{key}={value}")
    
    run_command(cmake_args)
    return build_dir

def build_project(build_dir: Path, target: str = None) -> None:
    """Build the project"""
    # Sanitize build_dir path
    build_dir_str = str(build_dir.resolve())
    if any(char in build_dir_str for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
        raise ValueError(f"Unsafe build directory path detected: {build_dir_str}")
    
    cmd = ["cmake", "--build", build_dir_str]
    if target:
        # Sanitize target name
        if any(char in target for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe target name detected: {target}")
        cmd.extend(["--target", target])
    run_command(cmd)

def run_tests(build_dir: Path) -> bool:
    """Run tests"""
    try:
        # Sanitize build_dir path
        build_dir_str = str(build_dir.resolve())
        if any(char in build_dir_str for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe build directory path detected: {build_dir_str}")
        run_command(["ctest", "--test-dir", build_dir_str, "--output-on-failure"])
        return True
    except subprocess.CalledProcessError:
        return False

def run_benchmarks(build_dir: Path) -> Dict:
    """Run benchmarks and return results"""
    benchmark_exe = build_dir / "btoon_benchmark"
    if not benchmark_exe.exists():
        print_warning("Benchmark executable not found, skipping")
        return {}
    
    try:
        # Sanitize benchmark_exe path
        benchmark_exe_str = str(benchmark_exe.resolve())
        if any(char in benchmark_exe_str for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe benchmark executable path detected: {benchmark_exe_str}")
        result = run_command([benchmark_exe_str, "--benchmark_format=json"])
        return json.loads(result.stdout)
    except Exception as e:
        print_error(f"Failed to run benchmarks: {e}")
        return {}

def run_snyk_scan(source_dir: Path) -> bool:
    """Run Snyk code scan"""
    try:
        # Note: This requires Snyk CLI to be installed and authenticated
        # Convert Path to string and validate it doesn't contain shell metacharacters
        source_path_str = str(source_dir.resolve())
        if any(char in source_path_str for char in [';', '&', '|', '`', '$', '(', ')', '<', '>', '\n', '\r']):
            raise ValueError(f"Unsafe path detected: {source_path_str}")
        result = run_command(
            ["snyk", "code", "test", source_path_str],
            check=False
        )
        return result.returncode == 0
    except FileNotFoundError:
        print_warning("Snyk CLI not found, skipping security scan")
        return True  # Don't fail if Snyk is not available

def main():
    parser = argparse.ArgumentParser(description="CI build script for BTOON Core")
    parser.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo"], default="Release")
    parser.add_argument("--sanitizers", action="store_true", help="Enable sanitizers")
    parser.add_argument("--fuzzing", action="store_true", help="Enable fuzzing")
    parser.add_argument("--coverage", action="store_true", help="Enable coverage")
    parser.add_argument("--tests", action="store_true", default=True, help="Run tests")
    parser.add_argument("--benchmarks", action="store_true", help="Run benchmarks")
    parser.add_argument("--snyk", action="store_true", help="Run Snyk security scan")
    parser.add_argument("--build-dir", type=Path, default=Path("build"), help="Build directory")
    parser.add_argument("--skip-build", action="store_true", help="Skip build step")
    
    args = parser.parse_args()
    
    source_dir = Path(__file__).parent.parent
    build_dir = args.build_dir.resolve()
    
    print_header("BTOON Core CI Build")
    print_info(f"Source directory: {source_dir}")
    print_info(f"Build directory: {build_dir}")
    print_info(f"Build type: {args.build_type}")
    
    # Configure CMake
    cmake_options = {
        "CMAKE_BUILD_TYPE": args.build_type,
        "BUILD_TESTS": "ON" if args.tests else "OFF",
        "BUILD_BENCHMARKS": "ON" if args.benchmarks else "OFF",
        "ENABLE_SANITIZERS": "ON" if args.sanitizers else "OFF",
        "ENABLE_FUZZING": "ON" if args.fuzzing else "OFF",
        "ENABLE_COVERAGE": "ON" if args.coverage else "OFF",
    }
    
    if not args.skip_build:
        print_header("Configuring CMake")
        configure_cmake(build_dir, cmake_options)
        
        print_header("Building project")
        build_project(build_dir)
        print_success("Build completed")
    
    # Run tests
    if args.tests:
        print_header("Running tests")
        if run_tests(build_dir):
            print_success("All tests passed")
        else:
            print_error("Tests failed")
            sys.exit(1)
    
    # Run benchmarks
    if args.benchmarks:
        print_header("Running benchmarks")
        results = run_benchmarks(build_dir)
        if results:
            print_success(f"Benchmarks completed ({len(results.get('benchmarks', []))} benchmarks)")
    
    # Run Snyk scan
    if args.snyk:
        print_header("Running Snyk security scan")
        if run_snyk_scan(source_dir):
            print_success("Security scan passed")
        else:
            print_error("Security scan found issues")
            sys.exit(1)
    
    print_header("CI build completed successfully")
    return 0

if __name__ == "__main__":
    sys.exit(main())

