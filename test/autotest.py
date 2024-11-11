from itertools import product
import argparse
import random
import shutil
import subprocess
import os

BDRM_DIR = '../'
TEST_DIR = '../test/generated_tf'

DEF_SUITABLE_FB = [32, 36, 48, 64, 128, 256, 512, 1024, 2048, 4096]

test_count = 0
errors = []

def get_odd_divs(n):
    divs = set()
    for i in range(2, int(n ** 0.5) + 1):
        if n % i == 0 and i % 2 == 0 and (n // i) % 2 == 0:
            divs.add(i)
            divs.add(n // i)
    return divs


def prepare_driver():
    try:
        subprocess.run(['make', 'clean'], cwd=BDRM_DIR, check=True, text=True)
        subprocess.run(['make'], cwd=BDRM_DIR, check=True, text=True)
        subprocess.run(['make', 'ins'], cwd=BDRM_DIR, check=True, text=True)
        subprocess.run(['make', 'set'], cwd=BDRM_DIR, check=True, text=True)
        print("Driver prepared successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Error preparing the driver: {e}")


def clean_dir(dir):
    if not os.path.exists(dir):
        print(f"Directory {dir} does not exist.")
        return

    for filename in os.listdir(dir):
        file_path = os.path.join(dir, filename)
        try:
            if os.path.isfile(file_path) or os.path.islink(file_path):
                os.unlink(file_path)
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)
        except Exception as e:
            print(f"Failed to delete {file_path}. Reason: {e}")


def create_test_files(num_files, file_size_kb, output_dir=TEST_DIR):
    os.makedirs(output_dir, exist_ok=True)

    for i in range(num_files):
        input_file_name = f"in_tf_{i + 1}_{file_size_kb}KB.txt"
        output_file_name = f"out_tf_{i + 1}_{file_size_kb}KB.txt"
        input_file_path = os.path.join(output_dir, input_file_name)
        output_file_path = os.path.join(output_dir, output_file_name)

        print(input_file_path)
        with open(input_file_path, 'wb') as f:
            data = os.urandom(file_size_kb * 1024)
            f.write(data)

        print(f"Created file: {input_file_path} with size {file_size_kb} KB and output file: {output_file_path}")


def compare_files(file1, file2, block_size, count):
    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        while True:
            chunk1 = f1.read(4096)
            chunk2 = f2.read(4096)

            if chunk1 != chunk2:
                print("Files are different")
                errors.append([file1, file2, block_size[0], count])
                return 

            if not chunk1:
                print("Files are identical")
                return


def run_dd_write_command(input_file, block_size, count):
    try:
        write_cmd = f"dd if={input_file} of=/dev/lsvbd1 oflag=direct bs={block_size}k count={count}"
        print(f"Running command: {write_cmd}")
        subprocess.run(write_cmd, shell=True, check=True, text=True)

    except subprocess.CalledProcessError as e:
        print(f"Error during 'dd' command execution: {e}")

def run_dd_read_command(output_file, block_size, count): 
    try:
        read_cmd = f"dd if=/dev/lsvbd1 of={output_file} iflag=direct bs={block_size}k count={count}"
        print(f"Running command: {read_cmd}")
        subprocess.run(read_cmd, shell=True, check=True, text=True)


    except subprocess.CalledProcessError as e:
        print(f"Error during 'dd' command execution: {e}")

def run_test_files(num_files, file_size_kb, block_size_kb):
    global test_count

    create_test_files(num_files, file_size_kb, TEST_DIR)

    for i in range(1, num_files + 1):
        input_file = os.path.join(TEST_DIR, f"in_tf_{i}_{file_size_kb}KB.txt")
        output_file = os.path.join(TEST_DIR, f"out_tf_{i}_{file_size_kb}KB.txt")
        print(input_file)

        if block_size_kb == 0:
            rw_pairs = list(product(get_odd_divs(file_size_kb), repeat=2))
            for rw_bs in rw_pairs:
                input_size = os.path.getsize(input_file)
                run_dd_write_command(input_file, rw_bs[0], input_size // (rw_bs[0] * 1024))
                run_dd_read_command(output_file, rw_bs[1], input_size // (rw_bs[1] * 1024))
                print(f"Completed processing file: {input_file}")
                test_count += 1
            
                compare_files(input_file, output_file, rw_bs, [input_size // rw_bs[0], input_size // rw_bs[1]])
        else:
            count = os.path.getsize(input_file) // (block_size_kb * 1024)
            run_dd_read_command(input_file, block_size_kb, int(count))
            run_dd_write_command(output_file, block_size_kb, int(count))
            
            compare_files(input_file, output_file, list(block_size_kb), list(count))
       
def proceed_run(num_files, file_size_kb, block_size_kb):
#     prepare_driver()
    clean_dir(TEST_DIR)

    if file_size_kb == 0:
        file_size_kb = random.choice(DEF_SUITABLE_FB)
        run_test_files(num_files, file_size_kb, block_size_kb)
    elif file_size_kb == -1:
        for k in DEF_SUITABLE_FB:
            run_test_files(num_files, k, block_size_kb)
    else:
        run_test_files(num_files, file_size_kb, block_size_kb)

    if len(errors) != 0:
        print(errors)
    else:
        print("\n\033[1mAll files are identical\033[0m")

    print(f"\n\033[1mTest passed: {test_count}, Failed: {int(len(errors) / 2)}\n\033[o \n")
    clean_dir(TEST_DIR)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create test files and run dd commands on a block device.')
    parser.add_argument('--num_files', '-n', type=int, default=5, help='Number of test files to create')
    parser.add_argument('--file_size_kb', '-fs', type=int, default=10,
                        help='Size of each test file in KB. \n '
                             'Set to 0 for a random choice of 1 file size. \n '
                             'Set to -1 for running all file sizes.')
    parser.add_argument('--block_size_kb', '-bs', type=int, default=0,
                        help='Block size for dd command in KB. \nSet to 0 for automatic selection.')
    parser.add_argument('--clear', '-c', help='Apply to clear test utilities.', action='store_true')
    
    args = parser.parse_args()

    if args.clear:
        clean_dir(TEST_DIR)
    else:
        proceed_run(num_files=args.num_files, file_size_kb=args.file_size_kb, block_size_kb=args.block_size_kb)
