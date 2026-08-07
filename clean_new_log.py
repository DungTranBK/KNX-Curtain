import sys
import re

def clean_log(input_file, output_file):
    with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()

    started = False
    with open(output_file, 'w', encoding='utf-8') as f:
        for line in lines:
            if "#CD:BEGIN#" in line:
                started = True
                f.write("#CD:BEGIN#\n")
                continue
            if "#CD:END#" in line:
                started = False
                f.write("#CD:END#\n")
                break
            if started and "#CD:" in line:
                # Extract the hex part after #CD:
                # Example: [00:00:47.549,863] <err> coredump: #CD:5a4502000300050029000000 [0m
                match = re.search(r'#CD:([0-9a-fA-F]+)', line)
                if match:
                    f.write("#CD:" + match.group(1) + '\n')

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 clean_new_log.py <input_log> <output_log>")
    else:
        clean_log(sys.argv[1], sys.argv[2])
