import argparse
import random
import shutil
import subprocess
import os

BDRM_DIR = '../'
TEST_DIR = './generated_tf'

DEF_BLOCK_SIZES = [1, 2, 4, 8, 16, 32, 64]

DEF_SUITABLE_FB = {
    16: [1, 2, 4, 8],
    32: [1, 2, 4, 8, 16],
    64: [1, 2, 4, 8, 16, 32],
    128: [1, 2, 4, 8, 16, 32, 64],
    256: [1, 2, 4, 8, 16, 32, 64, 128],
    512: [1, 2, 4, 8, 16, 32, 64, 128, 256],
    1024: [1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
    2048: [1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
    4096: [1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
    8192: [1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
    # Not default ones
    10: [1, 2, 5, 10],
    200: [1, 2, 4, 10, 20, 50, 100],
    500: [1, 2, 5, 10, 25, 50, 100, 250],
}

test_count = 0
errors = []

# def prepare_driver():
    # try:
    #     subprocess.run(['make', 'clean'], cwd=BDRM_DIR, check=True, text=True)
    #     subprocess.run(['make'], cwd=BDRM_DIR, check=True, text=True)
    #     subprocess.run(['insmod', 'bdrm.ko'], cwd=BDRM_DIR, check=True, text=True)
    #     subprocess.run(['echo', '1 /dev/vdb', '>', '/sys/module/bdrm/parameters/set_redirect_bd'], shell=True,
    #                    check=True)
    #     print("Driver prepared successfully.")
    # except subprocess.CalledProcessError as e:
    #     print(f"Error preparing the driver: {e}")


def clean_test_dir(test_dir):
    if not os.path.exists(test_dir):
        print(f"Directory {test_dir} does not exist.")
        return

    for filename in os.listdir(test_dir):
        file_path = os.path.join(test_dir, filename)
        try:
            if os.path.isfile(file_path) or os.path.islink(file_path):
                os.unlink(file_path)
                print(f"Removed file: {file_path}")
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)
                print(f"Removed directory: {file_path}")
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


def compare_files(file1, file2):
    with open(file1, 'rb') as f1, open(file2, 'rb') as f2:
        while True:
            chunk1 = f1.read(4096)
            chunk2 = f2.read(4096)

            if chunk1 != chunk2:
                return False

            if not chunk1:
                return True


def run_dd_command(input_file, output_file, block_size, count):
    global test_count

    try:
        write_cmd = f"dd if={input_file} of=/dev/bdr1 oflag=direct bs={block_size}K count={count}"
        print(f"Running command: {write_cmd}")
        subprocess.run(write_cmd, shell=True, check=True, text=True)

        read_cmd = f"dd if=/dev/bdr1 of={output_file} iflag=direct bs={block_size}K count={count}"
        print(f"Running command: {read_cmd}")
        subprocess.run(read_cmd, shell=True, check=True, text=True)

        print(f"Completed processing file: {input_file}")

        test_count += 1

        if compare_files(input_file, output_file):
            print("Files are identical")
        else:
            print("Files are different")
            errors.append([input_file, output_file, block_size, count])

    except subprocess.CalledProcessError as e:
        print(f"Error during 'dd' command execution: {e}")


def join_files(num_files, file_size_kb, block_size_kb):
    create_test_files(num_files, file_size_kb, TEST_DIR)

    for i in range(1, num_files + 1):
        input_file = os.path.join(TEST_DIR, f"in_tf_{i}_{file_size_kb}KB.txt")
        output_file = os.path.join(TEST_DIR, f"out_tf_{i}_{file_size_kb}KB.txt")
        print(input_file)

        file_size_key = max(k for k in DEF_SUITABLE_FB if k <= file_size_kb)

        if block_size_kb == 0:
            suitable_block_sizes = DEF_SUITABLE_FB[file_size_key]
            for block_size in suitable_block_sizes:
                count = os.path.getsize(input_file) // (block_size * 1024)
                run_dd_command(input_file, output_file, block_size, int(count))
        else:
            count = os.path.getsize(input_file) // (block_size_kb * 1024)
            run_dd_command(input_file, output_file, block_size_kb, int(count))


def proceed_run(num_files, file_size_kb, block_size_kb):
    #prepare_driver()
    clean_test_dir(TEST_DIR)

    if file_size_kb == 0:
        file_size_kb = random.choice(list(DEF_SUITABLE_FB.keys()))
        join_files(num_files, file_size_kb, block_size_kb)
    elif file_size_kb == -1:
        for k in list(DEF_SUITABLE_FB.keys()):
            join_files(num_files, k, block_size_kb)
    else:
        join_files(num_files, file_size_kb, block_size_kb)

    if len(errors) != 0:
        print(errors)
    else:
        print("\n\033[1mAll files are identical\033[0m")

    print(f"\n\033[1mTest passed: {test_count}, Failed: {int(len(errors) / 2)}\n\033[o \n")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create test files and run dd commands on a block device.')
    parser.add_argument('--num_files', type=int, default=5, help='Number of test files to create')
    parser.add_argument('--file_size_kb', type=int, default=10,
                        help='Size of each test file in KB. \n '
                             'Set to 0 for a random choice of 1 file size. \n '
                             'Set to -1 for running all file sizes.')
    parser.add_argument('--block_size_kb', type=int, default=0,
                        help='Block size for dd command in KB. \nSet to 0 for automatic selection.')
    parser.add_argument('--clear', '-c', help='Apply to clear test utilities.', action='store_true')
    
    args = parser.parse_args()

    if args.clear:
        clean_test_dir(TEST_DIR)
    else:
        proceed_run(num_files=args.num_files, file_size_kb=args.file_size_kb, block_size_kb=args.block_size_kb)
