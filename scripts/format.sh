#!/usr/bin/env bash
# Format maintained C and C++ sources.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

CHECK=0
if [[ "${1:-}" == "--check" ]]; then
    CHECK=1
elif [[ $# -gt 0 ]]; then
    echo "usage: scripts/format.sh [--check]" >&2
    exit 2
fi

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required" >&2
    exit 1
fi

cxx_files=()
while IFS= read -r file; do
    cxx_files+=("$file")
done < <(git ls-files '*.c' '*.h' '*.cpp' '*.hpp' '*.cc' '*.hh' '*.cppm')

if [[ ${#cxx_files[@]} -eq 0 ]]; then
    exit 0
fi

if [[ $CHECK -eq 1 ]]; then
    clang-format --dry-run --Werror "${cxx_files[@]}"
else
    clang-format -i "${cxx_files[@]}"
fi
