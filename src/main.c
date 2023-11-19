#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_timer.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <time.h>

static uint64_t Interval;

//打印必要信息
static void stats_display()
{
	const char clr[] = {27, '[', '2', 'J', '\0'};
	const char top_left[] = {27, '[', '1', ';', '1', 'H', '\0'};
	int i, portid;

	/* Clear screen and move to top left */
	printf("%s%s", clr, top_left);
	printf("Timer callback executed at %ld\n", time(NULL));
}



// 定时器回调函数
static void timer_callback(struct rte_timer *timer, void *arg) {

	stats_display();

}

int main(int argc, char *argv[]) {

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

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    // 初始化定时器子系统
    rte_timer_subsystem_init();

    // 创建定时器
    struct rte_timer timer;
    rte_timer_init(&timer);

    // 设置定时器每秒触发一次
    uint64_t hz = rte_get_timer_hz();
	Interval = hz;
    rte_timer_reset(&timer, hz, PERIODICAL, rte_lcore_id(), timer_callback, NULL);


    // 主循环
    while (1) {
        const char *data = "1\n";
        write(pipe_fd, data, 2); // 写入数字“1”和换行符

        rte_timer_manage();
        usleep(1000000);

    }

    return 0;
}
