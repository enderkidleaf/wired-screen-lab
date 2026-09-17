# IDD 驱动签名与测试模式

本项目的虚拟扩展屏使用 Windows Indirect Display Driver（IDD）。普通桌面投送不依赖该驱动；只有“启动 USB 副屏”创建真正扩展屏时才需要它。

当前发布的 IDD 包是 **测试签名驱动**，仅供开发和本机验证。要加载它，Windows 必须启用测试签名模式并重启。请勿把此安装方式用于生产电脑或面向普通用户分发。

## 测试模式安装

开始前，请确认你拥有管理员权限，并保存 BitLocker 恢复密钥。如果命令提示设置受 Secure Boot 策略保护，说明这台电脑的 Secure Boot 阻止测试模式；需要先在 UEFI 固件中处理 Secure Boot，再继续。更改 Secure Boot 前应先暂停 BitLocker 保护。

从已构建源码的项目根目录，以“管理员身份运行”的 PowerShell 执行：

```powershell
./native/scripts/sign_install_test_idd.ps1 -EnableTestSigning
```

脚本只会启用测试签名模式，然后结束。**重启 Windows** 后，再次以管理员身份执行：

```powershell
./native/scripts/sign_install_test_idd.ps1
```

第二次执行会创建或复用 `WiredScreen Test Driver` 本地测试证书，将它加入本机受信任根和受信任发布者存储，对 IDD 目录文件签名，并通过 `pnputil` 安装驱动。随后启动 `WiredScreen.exe`，点击“启动 USB 副屏”。

### 使用发布 ZIP 中的驱动

发布包的 `Driver/` 目录包含已测试签名的 `IddSampleDriver.inf`、`.cat`、`.dll` 和对应 `WiredScreenTestDriver.cer`。在测试模式已经启用并重启后，可在管理员 PowerShell 中执行：

```powershell
Import-Certificate .\Driver\WiredScreenTestDriver.cer Cert:\LocalMachine\Root
Import-Certificate .\Driver\WiredScreenTestDriver.cer Cert:\LocalMachine\TrustedPublisher
pnputil /add-driver .\Driver\IddSampleDriver.inf /install
```

这些命令只适用于本项目发布包中的测试驱动；不要导入来源不明的测试证书或安装来源不明的驱动。

## 测试模式的影响

- 桌面右下角会显示 `Test Mode` 水印。
- Windows 仍要求驱动文件带数字签名，但测试模式允许由不受信任根机构签发的测试证书签名的驱动加载。
- 因此，恶意或质量差的测试签名驱动更容易获得内核级权限。仅安装你确认来源和内容的驱动。
- 启用测试模式可能要求关闭 Secure Boot；这会降低启动链的保护能力，并可能触发 BitLocker 恢复。
- 它通常不会降低普通应用、游戏或网络的性能，但不建议长期在存放重要数据的主力电脑上保持开启。

微软关于测试签名模式的说明见：[Enable loading of test-signed code](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)。

## 退出测试模式

以管理员身份运行：

```powershell
bcdedit /set testsigning off
```

然后重启 Windows。退出后，当前测试签名 IDD 将不再加载，“启动 USB 副屏”的虚拟扩展屏功能会失效；普通桌面投送仍可使用。

## 面向正式用户的路线

没有可通用于所有软件、由微软直接提供的正式签名虚拟显示器驱动。IddCx 是 Windows 提供的框架，虚拟显示器驱动包仍由软件供应商实现和签名。

要让用户无需测试模式使用本项目的 IDD，应使用自己的正式驱动包：

1. 固定 INF 中的 Provider、DriverVer、CatalogFile 与硬件 ID，并对 Release 包完成安装、升级、休眠恢复和多显示器测试。
2. 购买 EV 代码签名证书，注册 Windows Hardware Developer Program，并将证书关联到 Hardware Dev Center。
3. 提交 Attestation Signing，取得微软签名后的目录文件；这是手动分发驱动包的现实起点。
4. 如需 Windows Update 自动分发，再完成 HLK/WHCP 测试和发布标签。

第三方软件如 Duet、SuperDisplay 和 spacedesk 也依赖显示驱动；区别在于它们提供的是已签名、随安装器分发的驱动包。不能将第三方驱动拆出或捆绑到本项目中，除非其许可证明确允许。

参考资料：

- [Windows 驱动签名要求](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs)
- [Windows IDD 模型概览](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [发布驱动到 Windows Update](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/publish-a-driver-to-windows-update)
