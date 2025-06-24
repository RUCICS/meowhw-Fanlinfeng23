#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>
// ----------- 通用数学函数：最大公约数与最小公倍数 -----------
static size_t compute_gcd(size_t x, size_t y) {
    while (y != 0) {
        size_t r = x % y;
        x = y;
        y = r;
    }
    return x;
}

static size_t calc_lcm_value(size_t x, size_t y) {
    return x / compute_gcd(x, y) * y;
}

// ----------- 判断某数是否是2的幂次，用于优化对齐策略 -----------
static bool is_pow_of_two(size_t val) {
    return val > 0 && (val & (val - 1)) == 0;
}

// ----------- 动态判断 I/O 最佳缓冲区大小：考虑页大小与块大小 -----------
static size_t determine_io_blocksize(int filedesc) {
    long sys_pagesize = sysconf(_SC_PAGESIZE);
    if (sys_pagesize < 0) {
        perror("sysconf(_SC_PAGESIZE) failed");
        sys_pagesize = 4096; // 默认页大小
    }

    size_t blk_size = (size_t)sys_pagesize;

    struct stat fs_info;
    if (fstat(filedesc, &fs_info) == 0) {
        size_t probed_blksize = (size_t)fs_info.st_blksize;
        if (probed_blksize >= 512 && probed_blksize <= (1 << 20) && is_pow_of_two(probed_blksize)) {
            blk_size = probed_blksize;
        }
    } else {
        perror("fstat failed");
    }

    return calc_lcm_value(sys_pagesize, blk_size);
}

// ----------- 手动页对齐分配，返回对齐地址，并在前方保存原始指针 -----------
static void* allocate_page_aligned(size_t bytes) {
    long pg_sz = sysconf(_SC_PAGESIZE);
    if (pg_sz < 0) pg_sz = 4096;

    size_t offset = (size_t)pg_sz + sizeof(void*);
    size_t total = bytes + offset;

    void* raw_mem = malloc(total);
    if (!raw_mem) {
        fprintf(stderr, "Failed to allocate %zu bytes\n", total);
        exit(EXIT_FAILURE);
    }

    uintptr_t raw = (uintptr_t)raw_mem;
    uintptr_t aligned = (raw + offset - 1) & ~(pg_sz - 1);
    ((void**)aligned)[-1] = raw_mem;

    return (void*)aligned;
}

// ----------- 释放页对齐内存，恢复原始指针再 free -----------
static void free_page_aligned(void* ptr) {
    if (!ptr) return;
    void* raw = ((void**)ptr)[-1];
    free(raw);
}

int main(int arg_count, char* arg_values[]) {
    if (arg_count != 2) {
        dprintf(STDERR_FILENO, "Usage: %s <input_file>\n", arg_values[0]);
        return EXIT_FAILURE;
    }

    const char* filepath = arg_values[1];
    int input_fd = open(filepath, O_RDONLY);
    if (input_fd < 0) {
        dprintf(STDERR_FILENO, "Unable to open '%s': %s\n", filepath, strerror(errno));
        return EXIT_FAILURE;
    }

    // 确定 I/O 缓冲区大小，并进行页对齐分配
    size_t buf_size = determine_io_blocksize(input_fd);
    void* io_buf = allocate_page_aligned(buf_size);

    // ----------- 文件读取与标准输出循环逻辑 -----------
    bool eof_reached = false;
    while (!eof_reached) {
        ssize_t read_bytes = read(input_fd, io_buf, buf_size);
        if (read_bytes < 0) {
            perror("read() failed");
            free_page_aligned(io_buf);
            close(input_fd);
            return EXIT_FAILURE;
        }

        if (read_bytes == 0) break; // 文件结束

        // 确保所有读取的内容都完整写出
        ssize_t offset = 0;
        while (offset < read_bytes) {
            ssize_t w = write(STDOUT_FILENO, (char*)io_buf + offset, read_bytes - offset);
            if (w < 0) {
                perror("write() failed");
                free_page_aligned(io_buf);
                close(input_fd);
                return EXIT_FAILURE;
            }
            offset += w;
        }
    }

    // ----------- 清理资源 -----------
    free_page_aligned(io_buf);
    close(input_fd);
    return EXIT_SUCCESS;
}