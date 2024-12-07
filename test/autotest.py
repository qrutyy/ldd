# SPDX-License-Identifier: GPL-2.0-only

import argparse
import os
import random
import shutil
import subprocess
from itertools import product

BASE_DIR = '../'
TEST_DIR = os.path.join(BASE_DIR, 'test/generated_tf')
DEFAULT_SUITABLE_FILE_SIZES = [32, 36, 48, 64, 128, 256, 512, 1024, 2048, 4096]

test_count = 0
errors = []

def get_even_divisors(n):
    divisors = set()
    for i in range(2, int(n**0.5) + 1):
        if n % i == 0:
            if i % 2 == 0 and i < 32:
                divisors.add(i)
            if (n // i) % 2 == 0 and (n // i) < 32:
                divisors.add(n // i)
    return divisors

def run_make_commands(commands):
    try:
        for cmd in commands:
            subprocess.run(['make', cmd], cwd=BASE_DIR, check=True, text=True)
        print("Driver prepared successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error during driver preparation: {e}")

def clean_directory(directory):
    if not os.path.exists(directory):
        print(f"Directory {directory} does not exist.")
        return
    for item in os.listdir(directory):
        item_path = os.path.join(directory, item)
        try:
            if os.path.isfile(item_path) or os.path.islink(item_path):
                os.unlink(item_path)
            elif os.path.isdir(item_path):
                shutil.rmtree(item_path)
        except Exception as e:
            print(f"Failed to delete {item_path}. Reason: {e}")

def create_test_files(num_files, file_size_kb, output_dir=TEST_DIR):
    os.makedirs(output_dir, exist_ok=True)
    for i in range(num_files):
        input_file = os.path.join(output_dir, f"in_tf_{i + 1}_{file_size_kb}KB.txt")
        with open(input_file, 'wb') as f:
            f.write(os.urandom(file_size_kb * 1024))
        print(f"Created test file: {input_file} ({file_size_kb} KB)")

def compare_files(file1, file2):
    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        while True:
            chunk1 = f1.read(4096)
            chunk2 = f2.read(4096)
            if chunk1 != chunk2:
                print(f"Mismatch found between {file1} and {file2}.")
                return False
            if not chunk1:
                break
    print(f"Files {file1} and {file2} are identical.")
    return True

def run_dd_command(command):
    try:
        subprocess.run(command, shell=True, check=True, text=True)
        print(f"Executed command: {command}")
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {e}")

def process_test_file(vbd_name, input_file, output_file, block_sizes):
    global test_count
    file_size = os.path.getsize(input_file)
    for write_bs, read_bs in product(block_sizes, repeat=2):
        write_count = file_size // (write_bs * 1024)
        read_count = file_size // (read_bs * 1024)

        run_dd_command(f"dd if={input_file} of=/dev/{vbd_name} oflag=direct bs={write_bs}k count={write_count}")
        run_dd_command(f"dd if=/dev/{vbd_name} of={output_file} iflag=direct bs={read_bs}k count={read_count}")
        
        test_count+=1

        if not compare_files(input_file, output_file):
            errors.append((write_bs, read_bs))

def run_tests(vbd_name, num_files, file_size_kb, block_size_kb):
    if file_size_kb == -1:
        for size in DEFAULT_SUITABLE_FILE_SIZES:        
            create_test_files(num_files, file_size_kb, TEST_DIR)
    else:
        create_test_files(num_files, file_size_kb, TEST_DIR)

    block_sizes = [block_size_kb] if block_size_kb > 0 else get_even_divisors(file_size_kb)

    for i in range(num_files):
        input_file = os.path.join(TEST_DIR, f"in_tf_{i + 1}_{file_size_kb}KB.txt")
        output_file = os.path.join(TEST_DIR, f"out_tf_{i + 1}_{file_size_kb}KB.txt")
        process_test_file(vbd_name, input_file, output_file, block_sizes)


def main():
    parser = argparse.ArgumentParser(description="Run tests on virtual block device using 'dd'.")
    parser.add_argument('--vbd_name', '-vbd', type=str, default="/dev/lsvbd1", help="Name of the virtual block device")
    parser.add_argument('--num_files', '-n', type=int, default=5, help="Number of test files to create")
    parser.add_argument('--file_size_kb', '-fs', type=int, default=1024, help='Size of each test file in KB. \n '
                             'Set to 0 for a random choice of 1 file size. \n '
                             'Set to -1 for running all file sizes.')
    parser.add_argument('--block_size_kb', '-bs', type=int, default=0, help='Block size for dd command in KB. \nSet to 0 for automatic selection.')
    parser.add_argument('--clear', '-c', action='store_true', help="Clear the test directory")
    args = parser.parse_args()

    if args.clear:
        clean_directory(TEST_DIR)
    else:
        run_make_commands(['clean', '', 'ins', 'set'])
        clean_directory(TEST_DIR)
        file_size_kb = args.file_size_kb if args.file_size_kb > 0 else random.choice(DEFAULT_SUITABLE_FILE_SIZES)
        run_tests(args.vbd_name, args.num_files, file_size_kb, args.block_size_kb)

        if errors:
            print("Errors encountered in the following tests:", errors)
        else:
            print(f"All tests ({test_count})passed successfully!")
        clean_directory(TEST_DIR)

if __name__ == "__main__":
    main()
