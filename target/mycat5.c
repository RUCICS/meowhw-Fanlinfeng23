#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>
// ================= 工具函数：数学相关 ===================

// 计算最大公因数（辗转相除法）
static size_t compute_gcd_value(size_t m, size_t n) {
    while (n != 0) {
        size_t t = m % n;
        m = n;
        n = t;
    }
    return m;
}

// 计算最小公倍数
static size_t least_common_multiple(size_t x, size_t y) {
    if (x == 0 || y == 0) return 0;
    return (x / compute_gcd_value(x, y)) * y;
}

// 检查是否为2的幂次：位运算优化
static bool check_power_of_two(size_t number) {
    return number && !(number & (number - 1));
}

// ============= 计算推荐缓冲区大小 ==============

/**
 * 根据文件描述符和系统页面信息，计算建议的缓冲区大小（页大小与文件块大小的 LCM）；
 * 如果获取失败，则使用默认值。
 */
static size_t suggest_buffer_size(int fdesc) {
    long pagesz = sysconf(_SC_PAGESIZE);
    if (pagesz < 0) {
        perror("Unable to determine page size");
        pagesz = 4096;
    }

    struct stat fs_meta;
    size_t blk_sz = (size_t)pagesz;
    if (fstat(fdesc, &fs_meta) == 0) {
        size_t candidate = fs_meta.st_blksize;
        if (candidate >= 512 && candidate <= (1 << 20) && check_power_of_two(candidate)) {
            blk_sz = candidate;
        }
    } else {
        fprintf(stderr, "fstat error on fd=%d: %s\n", fdesc, strerror(errno));
    }

    size_t base_unit = least_common_multiple((size_t)pagesz, blk_sz);

    // 使用放大倍率提高效率，避免频繁系统调用
    const size_t kBoost = 128;
    const size_t kCeiling = 1024 * 1024;  // 1MB 最大限制
    size_t suggestion = base_unit * kBoost;

    return suggestion < kCeiling ? suggestion : kCeiling;
}

// ============= 页对齐分配器实现 ==============

/**
 * 分配页对齐内存，额外空间用于记录原始指针
 */
static void* aligned_buffer_acquire(size_t capacity) {
    long pg = sysconf(_SC_PAGESIZE);
    if (pg < 0) pg = 4096;

    size_t total_request = capacity + sizeof(void*) + pg;
    void* raw = malloc(total_request);
    if (!raw) {
        perror("malloc error (aligned)");
        exit(EXIT_FAILURE);
    }

    uintptr_t offset_addr = (uintptr_t)raw + sizeof(void*) + pg - 1;
    uintptr_t aligned = offset_addr & ~(pg - 1);
    ((void**)aligned)[-1] = raw;  // 存储原始位置

    return (void*)aligned;
}

/**
 * 释放对齐内存，根据头部的记录找到原始 malloc 地址
 */
static void aligned_buffer_release(void* aligned_ptr) {
    if (!aligned_ptr) return;
    void* origin = ((void**)aligned_ptr)[-1];
    free(origin);
}

// ============= 主程序逻辑入口 ==============

int main(int argc, char* argv[]) {
    if (argc < 2) {
        dprintf(STDERR_FILENO, "Usage: %s <file_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* fname = argv[1];
    int fhandle = open(fname, O_RDONLY);
    if (fhandle == -1) {
        fprintf(stderr, "Failed to open '%s': %s\n", fname, strerror(errno));
        return EXIT_FAILURE;
    }

    // ==== 获取合适的缓冲区大小并申请对齐内存 ====
    size_t io_buf_cap = suggest_buffer_size(fhandle);
    printf("Buffer size selected: %zu bytes\n", io_buf_cap);

    void* io_buf = aligned_buffer_acquire(io_buf_cap);

    // ==== 主循环读取并输出 ====
    while (1) {
        ssize_t nread = read(fhandle, io_buf, io_buf_cap);
        if (nread < 0) {
            perror("read() failed");
            aligned_buffer_release(io_buf);
            close(fhandle);
            return EXIT_FAILURE;
        }
        if (nread == 0) break; // EOF reached

        ssize_t written = 0;
        while (written < nread) {
            ssize_t nwrite = write(STDOUT_FILENO, (char*)io_buf + written, nread - written);
            if (nwrite < 0) {
                perror("write() error");
                aligned_buffer_release(io_buf);
                close(fhandle);
                return EXIT_FAILURE;
            }
            written += nwrite;
        }
    }

    // ==== 清理资源 ====
    aligned_buffer_release(io_buf);
    close(fhandle);
    return EXIT_SUCCESS;
}