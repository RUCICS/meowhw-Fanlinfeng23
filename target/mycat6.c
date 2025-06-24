#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
// ------------ 辅助工具函数区域 ------------

// 辗转相除法求最大公因数（GCD）
static size_t compute_gcd(size_t x, size_t y) {
    while (y != 0) {
        size_t temp = y;
        y = x % y;
        x = temp;
    }
    return x;
}

// 利用 GCD 计算最小公倍数（LCM）
static size_t compute_lcm(size_t m, size_t n) {
    if (m == 0 || n == 0) return 0;
    return (m / compute_gcd(m, n)) * n;
}

// 计算推荐的缓冲区大小（页大小与文件块大小的倍数，控制最大不超过1MB）
static size_t determine_optimal_block(int file_descriptor) {
    long sys_page_size = sysconf(_SC_PAGESIZE);
    if (sys_page_size <= 0) sys_page_size = 4096;

    struct stat file_stat;
    size_t fs_block = sys_page_size;

    if (fstat(file_descriptor, &file_stat) == 0) {
        size_t blk_candidate = file_stat.st_blksize;
        if (blk_candidate > 0 && blk_candidate <= (1 << 20) && ((blk_candidate & (blk_candidate - 1)) == 0)) {
            fs_block = blk_candidate;
        }
    }

    size_t merged = compute_lcm(sys_page_size, fs_block);
    size_t scale_factor = 128;
    size_t proposed = merged * scale_factor;

    return proposed <= (1 << 20) ? proposed : (1 << 20);
}

// ------------ 内存分配模块（页对齐） ------------

// 申请页对齐缓冲区，额外存储原始 malloc 地址
static void* page_aligned_malloc(size_t sz) {
    long pg_size = sysconf(_SC_PAGESIZE);
    if (pg_size <= 0) pg_size = 4096;

    size_t request_sz = sz + pg_size + sizeof(void*);
    void* base_ptr = malloc(request_sz);
    if (!base_ptr) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    uintptr_t raw = (uintptr_t)base_ptr;
    uintptr_t adjusted = (raw + pg_size + sizeof(void*) - 1) & ~(pg_size - 1);
    void** save = (void**)(adjusted - sizeof(void*));
    *save = base_ptr;

    return (void*)adjusted;
}

// 对齐内存释放（读取前置 header 得到 malloc 原始指针）
static void page_aligned_free(void* aligned_ptr) {
    if (!aligned_ptr) return;
    void* original = *((void**)((uintptr_t)aligned_ptr - sizeof(void*)));
    free(original);
}

// ------------ 主程序入口 ------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        dprintf(STDERR_FILENO, "Usage: %s <file_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* filepath = argv[1];
    int f = open(filepath, O_RDONLY);
    if (f == -1) {
        perror("Failed to open input file");
        return EXIT_FAILURE;
    }

    // 建议操作系统进行顺序读取优化
    #ifdef __linux__
        posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
        posix_fadvise(f, 0, 0, POSIX_FADV_WILLNEED);
    #endif

    size_t buf_capacity = determine_optimal_block(f);
    void* io_buf = page_aligned_malloc(buf_capacity);

    ssize_t read_bytes = 0;
    int out_fd = STDOUT_FILENO;

    // 主循环：读取 → 输出
    while ((read_bytes = read(f, io_buf, buf_capacity)) > 0) {
        ssize_t processed = 0;
        while (processed < read_bytes) {
            ssize_t written = write(out_fd, (char*)io_buf + processed, read_bytes - processed);
            if (written < 0) {
                perror("Write to stdout failed");
                page_aligned_free(io_buf);
                close(f);
                return EXIT_FAILURE;
            }
            processed += written;
        }
    }

    // 读取出错时处理
    if (read_bytes < 0) {
        perror("Reading file failed");
        page_aligned_free(io_buf);
        close(f);
        return EXIT_FAILURE;
    }

    // 文件内容读取完毕后，提示系统释放页缓存
    #ifdef __linux__
        posix_fadvise(f, 0, 0, POSIX_FADV_DONTNEED);
    #endif

    page_aligned_free(io_buf);
    close(f);
    return EXIT_SUCCESS;
}