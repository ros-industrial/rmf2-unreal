#!/bin/bash

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS] <TARGET_DIR>

Run Clang Format all C++ files in the target directory.

Positional Arguments:
  <TARGET_DIR>    The path to the target directory (Required)

Options:
  -h, --help      Display this help message and exit.
  --reformat      Reformat files in place

Examples:
  $(basename "$0") -h
  $(basename "$0") .
  $(basename "$0") --reformat .
EOF
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  show_help
  exit 0
fi

# Parse optional flags first
_reformat_enabled=0
if [ "$1" = "--reformat" ]; then
  _reformat_enabled=1
  shift
fi

# Parse positional arguments
TARGET_DIR="$1"
if [ -z "$TARGET_DIR" ]; then
  echo "Error: Missing required positional argument <TARGET_DIR>."
  echo "Use $(basename "$0") -h for help."
  exit 1
fi

echo "Target directory is: $TARGET_DIR"

# 2. Shift away all processed options ($1, -v, -f, etc.)
shift $((OPTIND -1))
shopt -s globstar extglob nullglob

files=( "${TARGET_DIR}"/**/*.@(h|hpp|cpp) )
files_failed=0

for file in "${files[@]}"; do
  echo "Processing: $file"

  diff -u "$file" <(clang-format "${file}")
  _result=$?
  if [ "$_result" -ne 0 ]; then
    ((files_failed++))

    # New Line
    echo
  fi

  if [ "$_reformat_enabled" = 1 ]; then
    clang-format -i "$file"
  fi

done

if [ "$files_failed" -ne 0 ]; then
  echo "$files_failed files with code style divergence"
  exit 1
else
  echo "no files with code style divergence"
  exit 0
fi

