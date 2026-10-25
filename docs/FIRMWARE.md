# 固件（Mu-Silicium）：构建与打包

设备的启动镜像是 **Mu-Silicium UEFI 固件 + UniXNU 载荷** 拼出来的一个 Android
boot image。这份文档记录它是怎么来的，以及改了固件之后怎么重新打包。

## 一、构建固件

固件源码是 `Mu-Silicium` 的一个分支，平台包在
`Platforms/OnePlus/kebabPkg`，工具链 `CLANGPDB`（系统 clang 即可）。

```sh
cd <Mu-Silicium 检出目录>
./build-kebab.sh          # 本仓库 scripts/build-firmware.sh 的副本
```

约 20 秒出产物：

```
Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd     3,145,728 字节（3 MiB）
```

### 为什么不用 stuart

上游用 `stuart_build`（edk2-pytool-extensions）驱动构建。它只是设置几个环境
变量再调用 EDK2 的 `build`，而那套 Python 包对 Python 版本很敏感 —— 一次
`pip install` 升级就会让插件加载失败。脚本里把**上次成功构建记录的确切参数**
固化下来，不依赖 stuart：

| 变量 | 值 |
| --- | --- |
| `WORKSPACE` | 检出根目录 |
| `PACKAGES_PATH` | `.:Platforms/OnePlus:Common/Mu:Common/Mu_OEM_Sample:Mu_Basecore:Silicon/Qualcomm:Silicon/Silicium:Silicium-ACPI:Silicium-ACPI/Platforms/OnePlus:Silicium-ACPI/Silicon/Qualcomm` |
| `EDK_TOOLS_PATH` | `Mu_Basecore/BaseTools` |
| `PATH` | 追加 `BaseTools/BinWrappers/PosixLike`、`Bin/Mu-Basetools_extdep/Linux-x86`、`Bin/mu_nasm_extdep/Linux-x86-64`、`/usr/lib/llvm-21/bin` |

另外 BaseTools 的包装脚本调用 `python`（不带 3），部分发行版只有 `python3`，
脚本会在 `~/.local/share/kebab-fw-bin` 里建一个私有软链，不动系统。

构建 `-D` 参数（缺一个都会编出不对的固件）：

```
-p kebabPkg/kebab.dsc -b DEBUG -t CLANGPDB -a AARCH64
-D ONE_CRYPTO_PATH=Mu_Basecore/CryptoPkg/Binaries/onecrypto-bin_extdep
-D BUILDID_STRING=Unknown -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE
-D ENABLE_SECUREBOOT=0 -D FD_BASE=0x9fc00000 -D FD_SIZE=0x300000 -D FD_BLOCKS=0x300
```

### 调试输出

`kebabPkg/kebab.dsc` 里的

```
gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000000   # 只打错误
gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x00000000   # 全静音
```

是**编译期**常量，改完必须重新构建，运行时没有开关。

## 二、镜像布局

`fastboot flash boot_b` 刷的那个文件是 Android boot image v0：

```
偏移       内容
0x000000   Android boot 头（"ANDROID!"，page_size 0x1000）
0x001000   0x70 字节 shim + UEFI FV（kernel_size = 0x70 + 0x300000）
...        填充
0x800000   UniXNU 载荷卷（67,108,864 字节）
```

头部实测字段：

| 字段 | 值 |
| --- | --- |
| `kernel_size` | `0x300070` |
| `kernel_addr` | `0x8000` |
| `ramdisk_size` | `0x2bc78f` |
| `ramdisk_addr` | `0x1000000` |
| `page_size` | `0x1000` |
| `name` / `cmdline` | 空 |

总长固定 96 MiB。

## 三、重新打包

固件改完后**只替换 FV 区域**，boot 头、shim、载荷全部原样保留：

```sh
python3 scripts/repack-firmware.py \
  --template signed.img \
  --fv      <Mu-Silicium>/Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd \
  --out     signed-new.img
```

脚本会校验新 FV 与模板里的 `kernel_size - 0x70` 一致；不一致时拒绝拼接，
因为那意味着 boot 头也要改。

只想换载荷（内核）时，用同一个脚本加 `--payload`：

```sh
python3 scripts/repack-firmware.py --template signed.img \
  --payload payload.img --out XNU-<n>.img
```

这一步等价于日常刷机用的「`payload.img` 拼到 0x800000」。

## 四、验证打包正确的判据

替换后应当只有 FV 区域不同：

```
大小       100663296（96 MiB），与模板一致
boot 头    0x0000..0x0FFF 完全相同
载荷区     0x800000 起完全相同
差异区间   落在 0x1070 .. 0x300070 之内
```
