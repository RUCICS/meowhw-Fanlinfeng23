#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
// 获取当前平台建议的页大小，用于对齐缓冲区
size_t get_system_pagesize() {
    long size = sysconf(_SC_PAGESIZE); // 通常是 4096
    if (size < 0) {
        perror("sysconf(_SC_PAGESIZE) failed");
        return 4096; // 默认值
    }
    return (size_t)size;
}

// 分配页对齐的内存：返回一个对齐指针，原始指针存在其前一个位置
void* alloc_aligned_block(size_t want_size) {
    size_t alignment = get_system_pagesize();
    size_t total = want_size + alignment + sizeof(void*); // 预留元信息和对齐空间

    void *raw = malloc(total);
    if (!raw) {
        fprintf(stderr, "Memory allocation failed (%zu bytes)\n", total);
        exit(EXIT_FAILURE);
    }

    // 向上对齐指针位置
    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t misaligned = raw_addr + alignment + sizeof(void*);
    uintptr_t aligned_addr = misaligned & ~(alignment - 1);

    void *aligned = (void*)aligned_addr;

    // 在对齐指针前一个单元存储原始malloc指针，方便释放
    ((void**)aligned)[-1] = raw;

    return aligned;
}

// 释放对齐分配的内存
void release_aligned_block(void *aligned_ptr) {
    if (aligned_ptr) {
        void *original_ptr = ((void**)aligned_ptr)[-1];
        free(original_ptr);
    }
}

int main(int argn, char *argv[]) {
    if (argn != 2) {
        dprintf(STDERR_FILENO, "Usage: %s <input_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *target_file = argv[1];
    int file_fd = open(target_file, O_RDONLY);

    if (file_fd < 0) {
        dprintf(STDERR_FILENO, "Cannot open '%s': %s\n", target_file, strerror(errno));
        return EXIT_FAILURE;
    }

    // 决定缓冲区大小，通常为页大小（4KB）
    size_t buf_size = get_system_pagesize();

    // 申请页对齐缓冲区
    void *io_buffer = alloc_aligned_block(buf_size);

    ssize_t read_count;
    while ((read_count = read(file_fd, io_buffer, buf_size)) > 0) {
        ssize_t written_total = 0;
        while (written_total < read_count) {
            ssize_t chunk_written = write(STDOUT_FILENO,
                                          (char*)io_buffer + written_total,
                                          read_count - written_total);
            if (chunk_written < 0) {
                perror("Write error");
                release_aligned_block(io_buffer);
                close(file_fd);
                return EXIT_FAILURE;
            }
            written_total += chunk_written;
        }
    }

    if (read_count < 0) {
        perror("Read error");
        release_aligned_block(io_buffer);
        close(file_fd);
        return EXIT_FAILURE;
    }

    // 清理资源
    release_aligned_block(io_buffer);
    close(file_fd);
    return EXIT_SUCCESS;
}