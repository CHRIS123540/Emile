import os

pipe_name = '/tmp/my_named_pipe'

# 确保管道存在
if not os.path.exists(pipe_name):
    os.mkfifo(pipe_name)

with open(pipe_name, 'r') as pipe_in:
    try:
        while True:
            # 尝试读取三个数据项
            batch = [pipe_in.readline().strip() for _ in range(3)]
            # 检查是否读取了足够的数据
            if len(batch) == 3:
                print("Received batch:", batch)
            else:
                # 如果读取的数据不足三个，退出循环
                break
    except KeyboardInterrupt:
        # 捕获Ctrl+C，退出程序
        print("Exiting...")

