# WeChat Anti-Recall for Linux

面向 Arch Linux 原生微信的运行时防撤回工具。当前已在微信 `4.1.1.8` 上完成真实账号撤回测试：别人撤回后原消息保留，自己撤回仍使用微信原生行为。

> 不修改 `/opt/wechat/wechat` 文件。通过用户级 `.desktop` 启动器注入运行时库，微信升级后版本不匹配会自动停用 Hook。

## 一键使用

首次安装：

```bash
git clone https://github.com/yang316/wechat-antirecall-linux.git
cd wechat-antirecall-linux
sudo ./wechat-antirecall install
```

这条命令会自动：

1. 检查编译依赖；
2. 编译项目；
3. 执行全部自动测试；
4. 校验当前微信 Build ID 和 Hook 入口机器码；
5. 安装运行时库；
6. 写入用户级原生桌面启动器；
7. 自动重启微信。

安装完成后，管理命令也会复制到 `/usr/local/bin`。状态、诊断、模式切换、重启和卸载可在任意目录直接使用：

```bash
wechat-antirecall status
wechat-antirecall doctor
sudo wechat-antirecall enable
sudo wechat-antirecall disable
sudo wechat-antirecall uninstall
```

`install` 和 `build` 需要访问源代码，请在项目目录使用 `./wechat-antirecall`。

正常状态示例：

```text
Mode: blocking
Library: installed (/usr/lib/wechat-antirecall/libwechat-guardian.so)
Runtime: loaded (PID 12345)
```

## 常用命令

```bash
# 安装并启用正式防撤回
sudo ./wechat-antirecall install

# 只观察撤回事件，不阻止消息删除
sudo ./wechat-antirecall install probe

# 已安装后切换到正式防撤回
sudo ./wechat-antirecall enable

# 已安装后切换到观察模式
sudo ./wechat-antirecall probe

# 临时记录图片资源字段；仅用于当前微信版本的原图下载适配，保留防撤回行为
sudo wechat-antirecall trace-images

# 在收到中图后自动请求原图（当前仅适配微信 4.1.1.8）
sudo wechat-antirecall auto-original

# 暂时禁用，保留运行时库
sudo wechat-antirecall disable

# 查看状态
wechat-antirecall status

# 检查依赖、微信版本和安装状态
wechat-antirecall doctor

# 仅构建和测试
./wechat-antirecall build

# 手动重启微信
sudo wechat-antirecall restart

# 完全卸载
sudo wechat-antirecall uninstall
```

原有脚本仍可使用，内部会转发到统一命令：

```bash
sudo ./scripts/install.sh blocking
./scripts/status.sh
sudo ./scripts/uninstall.sh
```

## 支持范围

当前版本仅支持：

- 微信路径：`/opt/wechat/wechat`
- 微信版本：`4.1.1.8`
- 架构：x86-64 PIE ELF
- Build ID：`be2d6b53c50f5cf754b00a8001e5ee88980fdeeb`
- SHA256：`1630e2bf9ad852e4fca938bb83f99c40d5d8acecdd09b6aa4aac53df761377cf`
- 启动环境：Arch Linux 原生桌面启动器（无 bwrap / Portable 依赖）

微信升级后先运行：

```bash
wechat-antirecall doctor
```

如果出现 `unsupported Build ID`，说明新版微信尚未适配。Runtime 不会用旧地址强行 Hook。

## 实现原理

1. 在 `~/.local/share/applications/wechat.desktop` 注入 `LD_PRELOAD=/usr/lib/wechat-antirecall/libwechat-guardian.so`。
2. 桌面启动器以原生方式启动微信并加载 Runtime。
3. Runtime 校验 `/proc/self/exe` 的 GNU Build ID。
4. 从 `/proc/self/maps` 计算 PIE load bias。
5. 严格校验 `parseRevokeXML` 和图片资源调度入口的 32 字节机器码。
6. 使用 Zydis 解码并重定位被覆盖的 x86-64 指令，创建 trampoline。
7. 原函数解析 `<revokemsg>` 后，对远端撤回清空消息结构的 `newmsgid`，阻止原消息删除。
8. 检测“你撤回了一条消息”等本地撤回标记，保留微信自己的撤回行为。

当前逆向配置：

- `parseRevokeXML` RVA：`0x497b4e0`
- `newmsgid` 字段偏移：`+0x148`
- `replacemsg` 字段偏移：`+0x150`

`LD_PRELOAD` 可能传给微信辅助进程；Runtime 会按 Build ID 自动忽略这些进程。


### 自动原图

`auto-original` 等价于收到图片后自动打开并选择“查看原图”：收到 `type=2` 中图资源后，复制其元数据，仅将资源类型改为 `type=1`、文件 ID 从 `_2` 改为 `_0`、缓存目标从 `_mid_temp` 改为 `_hd_temp`，随后由微信自身下载队列请求、解密和写入可用的原图。服务端没有原图时，微信按原生行为保留已收到的中图；命名不匹配的资源不发起请求。

原图会写入微信的普通图片缓存目录，文件名以 `_hd_temp` 结尾；客户端界面仍可像手动下载一样查看或保存。日志记录“queued full resource”表示请求已进入微信下载队列，不表示服务端一定提供该原图。

当前仅适配微信 `4.1.1.8`；更新微信后 Runtime 会因 Build ID/两个 Hook 入口机器码校验失败而拒绝启用。

### Fcitx5 输入法

启动器显式传递 `GTK_IM_MODULE=fcitx`、`QT_IM_MODULE=fcitx`、`SDL_IM_MODULE=fcitx` 和 `XMODIFIERS=@im=fcitx`，因此通过 `wechat-antirecall restart` 启动时也能连接当前用户会话的 Fcitx5 服务。系统仍需安装并运行 `fcitx5`、`fcitx5-gtk` 与 `fcitx5-qt`。
## 手动构建

依赖：

```bash
sudo pacman -S --needed cmake ninja gcc zydis
```

构建和测试：

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/antirecall-inspect /opt/wechat/wechat
```

自动测试包括：

- GNU Build ID 解析；
- 特征码扫描；
- x86-64 Inline Hook 和 trampoline；
- Hook 卸载恢复；
- 统一 CLI 的安装、模式切换、禁用和卸载；
- 当前微信 Hook 入口机器码验证。

## 故障排查

### 状态显示 Runtime not loaded

重新启动微信：

```bash
sudo wechat-antirecall restart
```

随后检查：

```bash
wechat-antirecall status
```

### 微信升级后失效

```bash
wechat-antirecall doctor
```

若 Build ID 不受支持，需要重新逆向定位新版微信，不能直接复用旧 RVA 和结构偏移。

### 微信无法启动

立即禁用注入：

```bash
sudo wechat-antirecall disable
```

或完全卸载：

```bash
sudo wechat-antirecall uninstall
```

## 已验证行为

- 私聊中别人撤回普通文本：原消息保留；
- Runtime 在 Portable/bwrap 沙箱内成功加载；
- 自己撤回的识别逻辑已实现；建议在每次微信升级适配后重新回归此场景；
- 完全退出并重新启动微信后仍然生效；
- 未知 Build ID 安全禁用，不安装 Hook。

## 限制与风险

- 微信是闭源 stripped ELF，升级后通常需要重新分析。
- 当前实现保留原消息，但不额外改写或美化撤回提示。
- 自己撤回识别依赖 `replacemsg` 和原始 XML 中的本地化文本；微信修改文案后可能需要更新 marker。
- Runtime 只修改微信进程内存，不修改微信磁盘二进制。
- 使用前请自行评估账号、隐私及软件许可风险。

## 项目目录

```text
wechat-antirecall          统一管理命令
src/runtime.cpp            撤回 Hook 逻辑
src/inline_hook.cpp        x86-64 Hook/trampoline
scripts/                   兼容旧命令的包装脚本
tools/inspect.cpp          微信版本和目标机器码检查
tests/                     自动测试
```
