#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    const char *pipeName = "/tmp/my_named_pipe";
    int pipe_fd;

    // 创建命名管道
    mkfifo(pipeName, 0666);

    // 打开管道进行写入
    pipe_fd = open(pipeName, O_WRONLY);
    if (pipe_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    while (1) {
        const char *data = "1\n";
        write(pipe_fd, data, 2); // 写入数字“1”和换行符
        sleep(1); // 每秒写入一次
    }

    // 关闭管道
    close(pipe_fd);
    return 0;
}

