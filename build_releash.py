#!/usr/bin/env python3
import os
import re
import sys
import datetime
import subprocess

def main():
    # Verify if there are any changes to be committed
    try:
        status_check = subprocess.run(["git", "status", "--porcelain"], capture_output=True, text=True, check=True)
        if not status_check.stdout.strip():
            print("No changes to commit. Aborting version bump.")
            sys.exit(0)
    except subprocess.CalledProcessError as e:
        print(f"Error checking git status: {e}", file=sys.stderr)
        sys.exit(1)

    cmake_file = "CMakeLists.txt"
    
    if not os.path.exists(cmake_file):
        print(f"Error: {cmake_file} not found in the current directory.", file=sys.stderr)
        sys.exit(1)
        
    print(f"Reading version from {cmake_file}...")
    
    with open(cmake_file, "r", encoding="utf-8") as f:
        content = f.read()
        
    # Pattern to find the VERSION line (e.g., VERSION 26.7.42)
    # This is equivalent to searching with grep "VERSION <number>"
    pattern = r"(VERSION\s+(\d+)\.(\d+)\.(\d+))"
    match = re.search(pattern, content)
    
    if not match:
        print("Error: Could not find VERSION YY.MM.incremental pattern in CMakeLists.txt", file=sys.stderr)
        sys.exit(1)
        
    full_match = match.group(1)
    curr_yy = int(match.group(2))
    curr_mm = int(match.group(3))
    curr_inc = int(match.group(4))
    
    current_version = f"{curr_yy}.{curr_mm}.{curr_inc}"
    print(f"Current version found: {current_version}")
    
    # Get current year (YY) and month (MM)
    now = datetime.datetime.now()
    now_yy = now.year % 100
    now_mm = now.month
    
    # Compare with current date
    if now_yy == curr_yy and now_mm == curr_mm:
        # Same YY.MM, increment the incremental part
        new_yy = curr_yy
        new_mm = curr_mm
        new_inc = curr_inc + 1
    else:
        # YY.MM changed, start incremental from 0
        new_yy = now_yy
        new_mm = now_mm
        new_inc = 0
        
    new_version = f"{new_yy}.{new_mm}.{new_inc}"
    print(f"New version calculated: {new_version}")
    
    # Replace the old version line with the new one
    new_match = f"VERSION {new_version}"
    updated_content = content.replace(full_match, new_match, 1)
    
    # Write changes back to CMakeLists.txt
    with open(cmake_file, "w", encoding="utf-8") as f:
        f.write(updated_content)
        
    print(f"Successfully updated CMakeLists.txt to version {new_version}")
    
    # Git actions: git add .
    try:
        print("Running: git add .")
        subprocess.run(["git", "add", "."], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error during 'git add .': {e}", file=sys.stderr)
        sys.exit(1)
        
    # Git actions: git commit -m "vYY.MM.incremental"
    commit_message = f"v{new_version}"
    try:
        print(f"Running: git commit -m \"{commit_message}\"")
        subprocess.run(["git", "commit", "-m", commit_message], check=True)
        print("Version bump and git commit completed successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Error during 'git commit': {e}", file=sys.stderr)
        sys.exit(1)

    # Git actions: git push
    try:
        print("Running: git push")
        subprocess.run(["git", "push"], check=True)
        print("Git push completed successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Error during 'git push': {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()

