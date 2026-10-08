# F01：本机 ARM64 容器运行环境验证

记录时间：2026-10-07 12:13 +08:00。
结果：**本次启动周期内 ARM64 容器运行验证通过**。仅完成环境准备，不代表 Frontend、KT2/KT3 或发布验收通过。

## 授权与初始状态

- 用户明确允许配置本机 ARM64 模拟环境。
- 宿主机及 Docker 引擎为 x86_64；此前 ARM64 容器报 `exec /bin/sh: exec format error`。
- 主机有 binfmt-support，但无 qemu-aarch64 注册；sudo 需要密码，未索取或使用用户密码。
- Docker 已缓存官方 `tonistiigi/binfmt` 安装镜像。本次未下载新镜像，未修改项目源码。

## 注册操作

安装工具是宿主架构 amd64，仅用于一次性注册模拟器，不是业务构建/测试/部署镜像。已在操作前说明；所有项目目标镜像仍为 ARM64。

安装镜像：

- Repository digest：`tonistiigi/binfmt@sha256:400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0`
- 本次执行的固定本地 image ID：`sha256:15935d0512bf0a8e5afa1df990d971cf97a619ede08fa60f2d5966a62de6d6b7`

```bash
docker run --rm --pull=never --privileged --network none \
  sha256:15935d0512bf0a8e5afa1df990d971cf97a619ede08fa60f2d5966a62de6d6b7 \
  --install arm64
```

输出包括：

```text
installing: arm64 OK
supported: linux/arm64
emulators: qemu-aarch64
```

只安装 arm64 handler，没有注册其他新架构。`/proc/sys/fs/binfmt_misc/qemu-aarch64` 显示：

```text
enabled
interpreter /usr/bin/qemu-aarch64
flags: POCF
```

F 标志使内核持有解释器，安装容器退出后仍可供其他容器运行 ARM64 程序；不要求业务镜像自行携带 QEMU。

## 实际运行验证

使用已缓存、镜像元数据为 arm64 的固定基座 digest：

```bash
docker run --rm --pull=never --platform linux/arm64 --network none \
  --entrypoint /bin/sh \
  registry.chengyistudio.com/cxx/algo-base@sha256:fc10bc0d656af3c2418391daa129821226a109ede59f7098e5cf8a8a7a3915fa \
  -ec 'printf "uname="; uname -m; test "$(uname -m)" = aarch64; printf "userspace="; dpkg --print-architecture; printf "shell=PASS\n"'
```

退出码 0，实际输出：

```text
uname=aarch64
userspace=arm64
shell=PASS
```

操作前正在运行的 11 个容器，操作后按名称核对均仍在运行。未执行任何业务容器停止/重启、Docker daemon 重启、镜像发布或仓库代码修改。

## 有效期与限制

- 这是内核运行时注册，**宿主机重启或 binfmt 被清理后需重新检查，必要时重新注册**；本次没有设置开机自动注册或写入宿主机持久化服务。
- 未验证实际 KT2/KT3 算法、Frontend 页面或模拟性能。
- 环境准备依独立授权先完成；F00 工作区保护基线仍未建立，不能因此越过后续修改边界检查。
- 后续流程仍为 ARM 服务器构建/测试并发布 Harbor，本机拉取 ARM64 镜像测试；不得以本环境通过代替任何应用验收。
