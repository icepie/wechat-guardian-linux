# WeChat Anti-Recall for Linux

针对 Arch Linux 原生微信 `4.1.1.8` 的运行时防撤回实现。

## 支持范围

此版本**只支持**下面这个微信主程序：

- 路径：`/opt/wechat/wechat`
- 架构：x86-64 PIE ELF
- Build ID：`be2d6b53c50f5cf754b00a8001e5ee88980fdeeb`
- SHA256：`1630e2bf9ad852e4fca938bb83f99c40d5d8acecdd09b6aa4aac53df761377cf`

未知 Build ID 会安全退出，不安装 Hook，也不会修改微信磁盘文件。

`LD_PRELOAD` 会被 Portable 传给部分微信辅助进程；Runtime 会根据各进程的
Build ID 自动忽略它们。默认不输出这些辅助进程的 unsupported 日志。

## 实现原理

1. Portable 从 `~/.local/share/WeChat_Data/portable.env` 读取 `LD_PRELOAD`。
2. `libwechat-antirecall.so` 在微信进程启动时加载。
3. Runtime 校验 `/proc/self/exe` 的 GNU Build ID。
4. 根据 `/proc/self/maps` 计算 PIE load bias。
5. 严格校验 `parseRevokeXML` 入口的 32 字节机器码。
6. 使用 Zydis 解码并重定位被覆盖的 x86-64 指令，创建 trampoline。
7. 原函数解析 `<revokemsg>` 后，对远端撤回将消息结构的 `newmsgid` 清零，从而阻止原消息删除。
8. 检测到“你撤回了一条消息”时保留微信原生行为。

已逆向确认的当前版本配置：

- `parseRevokeXML` RVA：`0x497b4e0`
- `newmsgid` 字段偏移：`+0x148`
- `replacemsg` 字段偏移：`+0x150`

## 构建

依赖：

```bash
sudo pacman -S --needed cmake ninja gcc zydis
```

构建和测试：

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 安装

建议先用只观察、不修改消息的 probe 模式：

```bash
sudo ./scripts/install.sh probe
```

完全退出微信后重新启动。确认能正常启动且日志出现：

```text
[wechat-antirecall] enabled: WeChat 4.1.1.8 hook installed (probe-only)
```

再切换为正式阻止模式：

```bash
sudo ./scripts/install.sh blocking
```

重新启动微信后检查：

```bash
./scripts/status.sh
```

预期包含：

```text
Runtime: loaded
```

## 卸载

```bash
sudo ./scripts/uninstall.sh
```

然后完全退出并重新启动微信。

## 测试清单

需使用另一个账号进行真实撤回回归：

1. 私聊发送普通文本后由对方撤回：原消息应保留。
2. 群聊由其他成员撤回：原消息应保留。
3. 当前账号主动撤回自己的消息：应保持微信原生删除行为。
4. 图片、文件、引用消息撤回：确认不崩溃、会话列表正常。
5. 微信完全退出并重启：功能仍生效。
6. 微信升级后：Runtime 应报告 unsupported Build ID，而不是尝试 Hook。

## 风险与限制

- 微信是闭源 stripped ELF，函数签名和消息结构通过当前构建的反汇编推导；真实账号撤回测试仍是最后必要验证。
- 第一版仅保留原消息，不改写微信撤回提示。
- “自己撤回”依靠 `replacemsg`/原始 XML 中的本地化文本识别；若微信使用新的文案，需要补充 marker。
- 该方案仅修改进程内存，不修改 `/opt/wechat/wechat`。
- 如果出现启动异常，执行卸载脚本即可取消注入。
