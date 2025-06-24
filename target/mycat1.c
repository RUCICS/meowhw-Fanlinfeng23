#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// 主函数：读取指定文件并将内容输出到标准输出
int main(int arg_count, char *arg_values[]) {
    // 检查参数数量是否正确
    if (arg_count != 2) {
        dprintf(STDERR_FILENO, "用法: %s <文件名>\n", arg_values[0]);
        return EXIT_FAILURE;
    }

    // 获取目标文件名
    const char *input_file = arg_values[1];

    // 打开文件（只读模式）
    int file_descriptor = open(input_file, O_RDONLY);
    if (file_descriptor < 0) {
        perror("无法打开文件");
        return EXIT_FAILURE;
    }

    // 设置单字节缓冲区
    char byte_buffer;
    ssize_t read_bytes = 0, written_bytes = 0;

    // 持续读取直到文件末尾
    while (1) {
        read_bytes = read(file_descriptor, &byte_buffer, sizeof(byte_buffer));

        // 读取出错处理
        if (read_bytes < 0) {
            perror("读取失败");
            close(file_descriptor);
            return EXIT_FAILURE;
        }

        // 读到文件末尾
        if (read_bytes == 0) {
            break;
        }

        // 将读取到的字节写入标准输出
        written_bytes = write(STDOUT_FILENO, &byte_buffer, read_bytes);
        if (written_bytes < 0) {
            perror("写入失败");
            close(file_descriptor);
            return EXIT_FAILURE;
        }
    }

    // 关闭文件描述符
    if (close(file_descriptor) == -1) {
        perror("关闭文件失败");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}