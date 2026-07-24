#!/bin/bash
set -e

# Verify if there are any changes to be committed
status_output=$(git status --porcelain)
if [ -z "$status_output" ]; then
    echo "No changes to commit. Aborting version bump."
    exit 0
fi

cmake_file="CMakeLists.txt"
if [ ! -f "$cmake_file" ]; then
    echo "Error: $cmake_file not found in the current directory." >&2
    exit 1
fi

echo "Reading version from $cmake_file..."

# Find version line (e.g., VERSION 26.7.45)
version_line=$(grep -E 'VERSION [0-9]+\.[0-9]+\.[0-9]+' "$cmake_file" | head -n 1)
if [ -z "$version_line" ]; then
    echo "Error: Could not find VERSION YY.MM.incremental pattern in $cmake_file" >&2
    exit 1
fi

# Extract version string (e.g. 26.7.45)
curr_version=$(echo "$version_line" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
echo "Current version found: $curr_version"

# Parse components
IFS='.' read -r curr_yy curr_mm curr_inc <<< "$curr_version"

# Ensure components are base-10 numbers to avoid octal interpretation issues in bash
curr_yy=$((10#$curr_yy))
curr_mm=$((10#$curr_mm))
curr_inc=$((10#$curr_inc))

# Get current year (YY) and month (MM)
now_yy=$(date +%y)
now_mm=$(date +%m)

# Convert to base-10 to remove leading zeros (e.g. 07 -> 7)
now_yy=$((10#$now_yy))
now_mm=$((10#$now_mm))

# Compare and determine new version
if [ "$now_yy" -eq "$curr_yy" ] && [ "$now_mm" -eq "$curr_mm" ]; then
    new_yy=$curr_yy
    new_mm=$curr_mm
    new_inc=$((curr_inc + 1))
else
    new_yy=$now_yy
    new_mm=$now_mm
    new_inc=0
fi

new_version="${new_yy}.${new_mm}.${new_inc}"
echo "New version calculated: $new_version"

# Replace version in CMakeLists.txt using Python for safe, cross-platform string replacement
python3 -c "
with open('$cmake_file', 'r', encoding='utf-8') as f:
    content = f.read()
updated = content.replace('VERSION $curr_version', 'VERSION $new_version', 1)
with open('$cmake_file', 'w', encoding='utf-8') as f:
    f.write(updated)
"

echo "Successfully updated $cmake_file to version $new_version"

# Git actions: git add .
echo "Running: git add ."
git add .

# Git actions: git commit -m "vYY.MM.incremental"
commit_message="v$new_version"
echo "Running: git commit -m \"$commit_message\""
git commit -m "$commit_message"
echo "Version bump and git commit completed successfully!"

# Create release note and annotated tag
release_notes_file=$(mktemp)

echo "v$new_version" > "$release_notes_file"
echo "" >> "$release_notes_file"
echo "Release Notes:" >> "$release_notes_file"
echo "- Automated release for version v$new_version" >> "$release_notes_file"
echo "" >> "$release_notes_file"
echo "Files modified in this release:" >> "$release_notes_file"
# List files changed in the HEAD commit
git diff-tree --no-commit-id --name-status -r HEAD | sed 's/^/- /' >> "$release_notes_file"

# Check if there is a previous tag to list historical commits
last_tag=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [ -n "$last_tag" ]; then
    # List commits between last_tag and HEAD~1 (excluding the release commit itself)
    commits=$(git log "${last_tag}..HEAD~1" --oneline 2>/dev/null || true)
    if [ -n "$commits" ]; then
        echo "" >> "$release_notes_file"
        echo "Commits included since last release ($last_tag):" >> "$release_notes_file"
        echo "$commits" | sed 's/^/- /' >> "$release_notes_file"
    fi
else
    # If no previous tag, list all commits except HEAD
    commits=$(git log "HEAD~1" --oneline 2>/dev/null || true)
    if [ -n "$commits" ]; then
        echo "" >> "$release_notes_file"
        echo "Prior commits in history:" >> "$release_notes_file"
        echo "$commits" | sed 's/^/- /' >> "$release_notes_file"
    fi
fi

echo "Creating tag v$new_version with release notes..."
git tag -a "v$new_version" -F "$release_notes_file"
rm "$release_notes_file"

# Git actions: git push and git push tag
echo "Running: git push"
git push

echo "Running: git push origin v$new_version"
git push origin "v$new_version"

echo "Release v$new_version created and pushed successfully!"
