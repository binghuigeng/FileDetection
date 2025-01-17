/****************************************************************************
**
** @brief 使用监控工具结合终止逻辑
** 如果无法预先确定目标程序的写文件时机，可以结合 ReadDirectoryChangesW 实现监控，当检测到文件写入事件时立即终止目标程序。
**
** 总结
** • 核心逻辑：通过检测文件写入事件实时终止写文件程序，模拟断电。
** • 适用场景：
**     • 确定目标程序及其执行时机（直接终止）
**     • 不确定时机（通过文件写入监控自动触发）
** • 注意事项：
**     • 使用 TerminateProcess 等强制终止方法可能导致目标程序资源未释放，适合测试目的。
**
**
** MonitorFileWrite 中的代码利用了 ReadDirectoryChangesW 来监控文件系统事件，ReadDirectoryChangesW 是一个非常强大的 API，支持监控以下几种事件（包括但不限于）：
**
** 监控的事件类型
** ReadDirectoryChangesW 可以捕获以下文件或目录的更改：
**
** 文件内容修改
**
** 通过 FILE_NOTIFY_CHANGE_LAST_WRITE 标志触发。
** 通常用于捕捉文件内容被更改的时机，例如文件写入。
** 文件名更改
**
** 通过 FILE_NOTIFY_CHANGE_FILE_NAME 标志触发。
** 包括文件的创建、删除或重命名。
** 目录名更改
**
** 通过 FILE_NOTIFY_CHANGE_DIR_NAME 标志触发。
** 用于捕捉目录的创建、删除或重命名。
** 属性更改
**
** 通过 FILE_NOTIFY_CHANGE_ATTRIBUTES 标志触发。
** 当文件的只读、隐藏等属性发生变化时触发。
** 文件大小更改
**
** 通过 FILE_NOTIFY_CHANGE_SIZE 标志触发。
** 文件内容增加或减少导致文件大小变化。
** 安全性更改
**
** 通过 FILE_NOTIFY_CHANGE_SECURITY 标志触发。
** 例如文件权限的更改。
** 文件访问时间更改
**
** 通过 FILE_NOTIFY_CHANGE_LAST_ACCESS 标志触发。
** 捕获文件被读取或访问的情况。
**
****************************************************************************/

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

// 监控标志
std::atomic<bool> monitoring(true);

// 根据进程名终止目标程序
void KillProcessByName(const std::wstring& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to create process snapshot: " << GetLastError() << std::endl;
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (processName == pe.szExeFile) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    TerminateProcess(hProcess, 1); // 立即结束指定的进程。强制终止一个正在运行的进程，并释放与该进程相关的所有资源。结束的过程是迅速而直接的
                    std::wcout << L"Terminated process: " << processName << std::endl;
                    CloseHandle(hProcess);
                } else {
                    std::wcerr << L"Failed to open process: " << GetLastError() << std::endl;
                }
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
}

// 文件监控
void MonitorFileWrite(const std::wstring& directory, const std::wstring& targetFile, const std::wstring& processName) {
    HANDLE hDir = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to open directory for monitoring: " << GetLastError() << std::endl;
        return;
    }

    char buffer[1024];
    DWORD bytesReturned;

    while (monitoring) {
        if (ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE, // 只会捕捉文件内容发生修改的事件，例如写入操作
            &bytesReturned,
            nullptr,
            nullptr
        )) {
            FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
            do {
                std::wstring fileName(info->FileName, info->FileNameLength / sizeof(WCHAR));

                if (fileName == targetFile) {
                    std::wcout << L"Detected write event on: " << fileName << std::endl;
                    KillProcessByName(processName); // 终止写文件程序
                    return;
                }

                if (info->NextEntryOffset != 0) {
                    info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<char*>(info) + info->NextEntryOffset
                    );
                } else {
                    info = nullptr;
                }
            } while (info);
        } else {
            std::wcerr << L"Failed to read directory changes: " << GetLastError() << std::endl;
            break;
        }
    }

    CloseHandle(hDir);
}


int main() {
    // 监控文件夹路径
    std::wstring directory = L"E:\\History";

    // 目标文件名
    std::wstring targetFile = L"info_his.dat";

    // 写文件程序名
    std::wstring processName = L"TxrUi.exe";

    // 创建监控线程
    std::thread monitorThread(MonitorFileWrite, directory, targetFile, processName);

    std::wcout << L"Monitoring directory for changes. Press Enter to exit." << std::endl;
    std::wcin.get();

    // 停止监控并等待线程退出
    monitoring = false;
    monitorThread.join();

    return 0;
}
