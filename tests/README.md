# ExecCmdSync 单元测试（手动执行）

回归 `ExecCmdSync()` 的管道死锁：旧实现在读取子进程输出前先 `waitpid()`，
子进程输出超过管道缓冲区（约 64KiB）时双方互等死锁，会卡死
`dumpsys activity` top app 检测、`cmd settings` 亮度采样等路径。

## 运行

在仓库根目录手动执行：

```sh
./test.sh          # 等价于 ./test.sh host，本机 POSIX 环境编译并运行
./test.sh host     # 同上（macOS/Linux 均可，自动使用 clang++/g++）
./test.sh device   # 用 $ANDROID_NDK 交叉编译 arm64，经 adb 在设备上运行
```

测试进程带 30 秒看门狗：若回归了死锁，用例会被 SIGKILL（退出码 137），
不会永久挂住。

## 用例（tests/test_exec_cmd_sync.cpp）

- `TestSmallOutput`：小输出内容与返回码正确。
- `TestEmptyOutput`：无输出时 `content` 被清空。
- `TestLargeOutputNoDeadlock`：核心回归，约 1MiB 输出（远超管道缓冲区）
  必须在 10 秒内返回，且内容长度/结构完整。
- `TestMergedStderrNoDeadlock`：stdout/stderr 大量交错写入同一管道不卡死。
- `TestNullContentLargeChild`：`content == nullptr`（无管道）时大输出子进程
  也能正常回收。
- `TestExitCodePropagation`：非零退出码透传。
- `TestExecFailure`：`execv()` 失败时返回 255。

## 宿主编译 shim

生产代码依赖 bionic/glibc 的 `pipe2()` 和 `<sys/prctl.h>`，macOS 上没有。
`tests/host_shim.cpp` 与 `tests/host_shim/include/` 仅用于本机测试构建，
不参与 Android 生产构建（NDK/Linux 下走真实 libc 符号）。
