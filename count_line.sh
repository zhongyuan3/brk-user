#!/bin/bash

directories=(
    "./include/"
    "./lib/"
    "./src/"
    "./mkfs/"
)

extensions=("c" "h" "S")

total_lines=0

temp_file=$(mktemp)

for dir in "${directories[@]}"; do
    if [ ! -d "$dir" ]; then
        continue
    fi

    for ext in "${extensions[@]}"; do
        find "$dir" -type f -name "*.$ext" 2>/dev/null >>"$temp_file"
    done
done

if [ ! -s "$temp_file" ]; then
    rm -f "$temp_file"
    exit 0
fi

while IFS= read -r file; do
    lines=$(wc -l <"$file" 2>/dev/null)
    if [ -n "$lines" ] && [ "$lines" -eq "$lines" ] 2>/dev/null; then
        total_lines=$((total_lines + lines))
    fi
done <"$temp_file"

rm -f "$temp_file"

echo "$total_lines"
