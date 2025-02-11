# Monitor File Kill Process
"""
监测文件写入并立即终止进程
1. 使用 inotify（Linux）或 ReadDirectoryChangesW（Windows） 来监听文件打开而不仅仅是写入。
2. 提高轮询频率，缩短检测间隔，尽量减少响应延迟。
3. 直接监控文件句柄，在进程打开文件的瞬间杀死它。

优化速度
1. 监听文件访问：
    通过 ReadDirectoryChangesW() 监听文件打开和修改，而不是等它写完再处理。
2. 多线程实时监听：
    监控线程独立运行，不依赖主进程循环，提高反应速度。
3. Windows API 级别检测：
    比 watchdog 更快，可以在文件刚被写入时就立刻触发杀进程逻辑。

打包
pyinstaller --onefile -i activity.ico --name FileDetection file_detection.py
"""
import sys
import os
import psutil
import time
import win32file
import win32con
import threading

IS_RUNNING = True  # 控制监控循环的标志


def kill_process_by_name(process_name):
    """立即终止目标进程"""
    for proc in psutil.process_iter(attrs=['pid', 'name']):
        if proc.info['name'] == process_name:
            print(f"Terminating process {process_name} (PID: {proc.info['pid']})")
            proc.kill()


def clear_file_content(file_path):
    """清空文件内容"""
    max_retries = 5
    for attempt in range(max_retries):
        try:
            with open(file_path, 'w') as file:
                file.truncate(0)  # 清空文件内容
            print(f"The file is corrupted: {file_path}.")
            break
        except PermissionError:
            # print(f"Permission denied on {file_path}. Attempt {attempt + 1}/{max_retries}. Retrying in 1 second...")
            time.sleep(1)  # 等待1秒后重试


def monitor_file(file_to_watch, process_name):
    """使用 Windows API 监控文件是否被打开"""
    global IS_RUNNING  # 声明使用全局变量

    h_dir = win32file.CreateFile(
        os.path.dirname(file_to_watch),
        0x0001,
        win32con.FILE_SHARE_READ | win32con.FILE_SHARE_WRITE | win32con.FILE_SHARE_DELETE,
        None,
        win32con.OPEN_EXISTING,
        win32con.FILE_FLAG_BACKUP_SEMANTICS,
        None
    )

    while IS_RUNNING:
        results = win32file.ReadDirectoryChangesW(
            h_dir,
            1024,
            False,
            win32con.FILE_NOTIFY_CHANGE_LAST_WRITE | win32con.FILE_NOTIFY_CHANGE_SIZE | win32con.FILE_NOTIFY_CHANGE_FILE_NAME
        )
        for action, filename in results:
            if filename == os.path.basename(file_to_watch):
                print(f"Detected activity on {file_to_watch}")
                kill_process_by_name(process_name)  # 终止进程
                clear_file_content(file_to_watch)  # 损坏文件
                IS_RUNNING = False  # 修改全局变量，设置 IS_RUNNING 为假以停止监控
                break


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: FileDetection <file_path> <process_name>")
        sys.exit(1)

    # 需要监控的文件路径
    FILE_TO_WATCH = sys.argv[1]
    # 需要终止的进程名称
    PROCESS_NAME = sys.argv[2]

    print(f"Monitoring {FILE_TO_WATCH} for activity...")
    monitor_thread = threading.Thread(target=monitor_file, args=(FILE_TO_WATCH, PROCESS_NAME), daemon=True)
    monitor_thread.start()

    try:
        while IS_RUNNING:
            time.sleep(1)
    except KeyboardInterrupt:
        print("KeyboardInterrupt")
    finally:
        input("Press Enter to quit")  # 等待用户按下回车键退出程序
