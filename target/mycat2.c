#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

// 获取系统建议的I/O缓冲区大小（页大小）
size_t determine_buffer_size() {
    long sys_page_size = sysconf(_SC_PAGESIZE); // 获取内存页大小
    if (sys_page_size <= 0) {
        // 获取失败时回退到常见页大小
        fprintf(stderr, "sysconf error, fallback to 4KB\n");
        return 4096;
    }
    return (size_t)sys_page_size;
}

// 主函数，复制文件内容到标准输出（按缓冲块方式）
int main(int arg_total, char *arg_list[]) {
    // 检查参数个数
    if (arg_total != 2) {
        dprintf(STDERR_FILENO, "Usage: %s <filepath>\n", arg_list[0]);
        return EXIT_FAILURE;
    }

    // 获取目标路径
    const char *filepath = arg_list[1];

    // 以只读模式打开目标文件
    int input_fd = open(filepath, O_RDONLY);
    if (input_fd < 0) {
        dprintf(STDERR_FILENO, "Failed to open file '%s': %s\n", filepath, strerror(errno));
        return EXIT_FAILURE;
    }

    // 计算合适的缓冲区大小
    size_t buf_capacity = determine_buffer_size();

    // 分配缓冲内存
    char *buf_ptr = (char *)malloc(buf_capacity);
    if (buf_ptr == NULL) {
        perror("Memory allocation failed");
        close(input_fd);
        return EXIT_FAILURE;
    }

    // 开始循环读取并写入
    ssize_t chunk_in = 0;
    while ((chunk_in = read(input_fd, buf_ptr, buf_capacity)) > 0) {
        ssize_t written_total = 0;
        // 处理部分写入情况
        while (written_total < chunk_in) {
            ssize_t chunk_out = write(STDOUT_FILENO, buf_ptr + written_total, chunk_in - written_total);
            if (chunk_out < 0) {
                perror("Write error");
                free(buf_ptr);
                close(input_fd);
                return EXIT_FAILURE;
            }
            written_total += chunk_out;
        }
    }

    // 错误处理
    if (chunk_in < 0) {
        perror("Read error");
        free(buf_ptr);
        close(input_fd);
        return EXIT_FAILURE;
    }

    // 资源释放
    free(buf_ptr);
    close(input_fd);
    return EXIT_SUCCESS;
}