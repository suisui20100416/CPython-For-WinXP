# CPython 3.14.0 — Windows XP SP3 (32-bit) Port

基于 **CPython 3.14.0 Stable** 的 **Windows XP SP3 x86（NT 5.1）原生移植版**

只是因为喜欢XP系统 所以就做了这个项目喵

项目全程借助AI完成 不保证所有设备稳定可用喵

---

## 运行环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows XP **SP3** 32 位（NT 5.1 build 2600） |
| CPU | 支持 **SSE2** 的 x86（MSVC v141_xp / 静态 UCRT 的硬性前提，无法关闭） |
| 磁盘 | 安装约 80 MB |

**发行注意**：本版本不依赖 XP SP3 之后的任何系统补丁或运行库 redistribution；
Python 自身以 `/MT` 静态链接 UCRT，目标机无需 VC++ Redistributable。

---

## 源码树结构（相对本文件）

```
cpython/
├─ build_python_xp.bat        # 主体：freeze(v143) + pythoncore/exe(v141_xp)
├─ build_tcltk_xp.bat         # tcl 9.0.4 + tk 9.0.4.1 + _tkinter.pyd + 脚本库 四步一条链
├─ build_ext_modules_xp.bat   # XP 干净的扩展模块（_socket/_ctypes/_sqlite3/...）
├─ build_openssl_xp.bat      # OpenSSL 3.5.9（llvm-mingw，_ssl/_hashlib 依赖）
├─ build_msi_xp.bat         # 8 个子 MSI + WiX bundle 安装器
├─ rebuild_launchers_xp.bat   # 只重编 py/pyw/venv launcher（快速迭代）
├─ Tools/xp/                  # 辅助脚本 + 审计工具 + SDK 7.1A 头补充
│   ├─ patch_pe_subsys.py / stamp_openssl_xp.py / inspect_msi.py ...
│   ├─ audit_pe_imports.py / check_subsys.py / verify_against_sdk71a.py
│   └─ sdk71a-extra/          # 7.1A 缺失头（VersionHelpers.h 等）
├─ externals/                 # 外部依赖源码（见下文），产物目录已 gitignore
└─ build/                     # 输出（gitignore，不入库）
    ├─ python-3.14.0.exe      # 自包含安装器（最终交付物）
    ├─ *.msi                  # 8 个子 MSI
    └─ out/
        ├─ pcbuild/win32/     # PC 编译产物（python314.dll / *.pyd / pdb）
        ├─ obj/               # MSBuild 中间物
        └─ ...                # 安装布局树（Lib/DLLs/Scripts/python.exe）
```

---

## 构建方式

### 前置工具链

| 组件 | 用途 | 默认路径（可用环境变量覆盖） |
| --- | --- | --- |
| VS2017 Professional（14.16.27023，v141_xp） | XP 二进制编译 | `VSROOT17` |
| VS2022 BuildTools（14.44.35207，v143） | freeze 工具 / zlib-ng（与 3.15 XP 分支同配方） | `VSROOT22` |
| Windows SDK 10（10.0.26100） | 头文件 / UCRT 模板 | `SDK10` |
| Microsoft SDK 7.1A | XP 时代导入库参照 | `SDK71A` |
| UCRT 10.0.10240（SDK 内） | XP 干净的 UCRT 代 | `UCRTVER` |
| 任意已装 Python 3.x（`py`） | 构建辅助脚本 | `PYEXE` |
| llvm-mingw（含 mingw32-make） | OpenSSL 自编 | `LLVMMINGW` |
| perl 5.22+（须带 ExtUtils::MakeMaker / Pod::Usage，如 Git for Windows 附带的 perl） | OpenSSL Configure | `PERL` |

### 构建步骤

```bat
build_openssl_xp.bat      REM 0. OpenSSL 3.5.9 via llvm-mingw（_ssl/_hashlib 的链接输入）
build_python_xp.bat       REM 1. python314.dll + python.exe + launchers（全量约 15-20 分钟）
build_tcltk_xp.bat        REM 2. tcl/tk 9.0.4 + _tkinter.pyd（链接需要第 1 步的 python314.lib）
build_ext_modules_xp.bat  REM 3. 19 个扩展模块 .pyd（_ssl/_hashlib 链接第 0 步的 OpenSSL）
build_msi_xp.bat          REM 4. 8 个子 MSI + bundle 安装器（约 5 分钟）
```

> 顺序有依赖：第 2 步链接第 1 步的 `python314.lib`，第 3 步链接第 0 步的
> OpenSSL 与第 1 步的 `python314.lib`，必须自上而下执行；
> `build_tcltk_xp.bat` 的 zip 解压（step 4）等内部步骤使用 `py` launcher。

产物位置：

- 安装器：`build\python-3.14.0.exe`（自包含，可直接在 XP 上安装）
- 各子 MSI：`build\*.msi`
- 直接运行布局：`build\out\`（不经安装直接跑 `build\out\python.exe`）

### 构建质量门禁

每次构建后建议跑内置审计（Tools/xp/）：

```bat
python Tools\xp\audit_pe_imports.py build\out\pcbuild\win32\python314.dll
python Tools\xp\check_subsys.py     build\out\pcbuild\win32\python314.dll
```

- `audit_pe_imports.py`：比对导入表与 XP 时代的导出参照，0 个 Vista+ 符号
  才算通过（黑名单含 advapi32 等 MSI 时代踩过的坑，如 `RegDeleteKeyExW`）；
- `check_subsys.py`：PE 子系统版本必须 ≤ 5.1（链接期由 XP 补丁保证，此处复核）。

### 关键构建机制

- **双工具链拆分**：freeze 工具与 zlib-ng 用 v143（v141 在 3.15 上有静态类型
  机制的代码生成回归，3.14 分支沿用同一配方）；XP 二进制用 v141_xp + SDK 7.1A
  头 + UCRT 10240 + `/MT` 静态 CRT + `UseEnv=true`（链接器吃环境 LIB，避免
  26100 UCRT 带入 Vista+ 符号）。
- **XP 词汇开关**：`/p:XPBuild=true` 定义 `Py_XP`，源码切换到
  `PC/xp_compat.h` 的 XP 兼容层（Vista+ API 全部有垫片或回退实现）。
- **OpenSSL 自编**：llvm-mingw `perl Configure mingw shared no-asm`，入库的
  Makefile 已含 XP 修改（`-D_WIN32_WINNT=0x0501` + 子系统 5.1 链接参数，带
  Apache-2.0 §4(b) 标注；fresh 树由 `Tools/xp/stamp_openssl_xp.py` 固化时间戳），
  `.dll.a` 复制为 MSVC 可链的 `.lib`；
- **PE 子系统**：链接期固定 5.1；nmake 独立构建的 DLL（tcl90/tcl9tk90/
  tcl9dde14/tcl9registry13）由 tcl/tk 的 `win/rules.vc` 内钉
  `-subsystem/-osversion 5.01` 直出；仅 WiX 预编译的 bundle 引擎由
  `Tools/xp/patch_pe_subsys.py` 事后补丁。
- **MSI 改造**：`msi.props` 的非 release dev 版本号分支全链关闭
  （`BuildForRelease=true`）；`_TransformWxlTemplates` 的 wxl 增量缓存每轮清空；
  WiX bootstrap 与 WixCA 二进制同样按 5.1 处理。
- **与 3.15 XP 分支的差异**：3.14 官方 Windows 构建默认 Tcl/Tk 8.6.15，本分支
  改用与 externals 一致的 **Tcl/Tk 9.0.4**（`PCbuild/tcltk.props` 默认值 +
  `_tkinter` 的 Tcl 9 支持路径），脚本库布局 `tcl\tcl9.0` / `tcl\tk9.0` 与
  3.15 XP 版相同；3.14 bundled 的 pip 25.2 其 vendored platformdirs 本就调用
  `SHGetFolderPathW`（XP 原生 API），无需 3.15 分支的 pip wheel XP 补丁。

---

## xp 分支补丁概要

- `PC/xp_compat.h|c`：XP 兼容词汇表——`SHGetKnownFolderPath`→`SHGetFolderPathW`
  回退、`AddDllDirectory`→PATH 前插、`GetFinalPathNameByHandle` stub、
  `RegGetValueW`/`WaitableTimerEx`/`BCrypt` 等垫片；
- `Python/dynload_win.c`：扩展依赖搜索改用 `LOAD_WITH_ALTERED_SEARCH_PATH`
  （XP 不支持 `LOAD_LIBRARY_SEARCH_*`）；
- `Modules/socketmodule.c`：去掉 `WSA_FLAG_NO_HANDLE_INHERIT`（Win7 SP1+）；
- `Modules/getpath.c`：realpath 在 XP 下直接返回原路径；
- `PC/launcher2.c`：argv0 匹配的 ordinal 比较直接实现（XP 的 CompareStringW
  拒绝 `NORM_LINGUISTIC_CASING`+不变区域设置组合）、`_readIni` 不依赖 XP 不
  设置的 LastError；
- `Lib/tkinter/__init__.py`：import 时设置 `TK_LIBRARY`（适配安装布局）；
- `PC/_wmimodule.cpp`：`propsys.dll!VariantToString` 改 `VariantChangeTypeEx`
  垫片（XP 原生无 propsys.dll）。

---

## 外部依赖（externals/）

全部源码随仓库分发：
bzip2-1.0.8、libffi-3.4.4（**含 XP 重编运行库** win32/libffi-8.dll）、
mpdecimal-4.0.0、openssl-3.5.9-src（**含自编配置 Makefile**）、
sqlite-3.53.4.0、tcl-9.0.4.0、tk-9.0.4.1（**含 ITaskbarList3 判空补丁**）、
tcltk-9.0.4.0/win32（**镜像的 XP 构建运行库** tcl90.dll / tcl9tk90.dll）、
xz-5.8.1.1（windows/config.h 已补丁）、zlib-ng-2.2.4、zstd-1.5.7、
windows-installer/wix-314。

> 注：sqlite/xz/openssl 三项取 3.15 XP 分支已验证的版本集，与 3.14 官方默认
> （sqlite 3.50.4.0 / xz 5.2.5 / openssl 3.0.18）不同——统一 externals 版本集
> 让两个 XP 分支共享同一套已 XP 化的第三方源码，`PCbuild/python.props` 的
> 默认路径已同步对齐。

## 许可证

Python 本体遵循 [PSF License](LICENSE)。XP 移植补丁遵循相同许可。

本项目是 CPython 的非官方修改版，与 Python Software Foundation 无关联。
