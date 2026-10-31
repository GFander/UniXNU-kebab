0450–0453原安装服务初始化修复证据；只发布自制源码补丁/测试和符号化元数据。原始日志本地封存，sha256见验收JSON；无iOS二进制、缓存、密钥、镜像、账户记录或用户文件。安装/迁移/可用界面未完成。

```sh
python3 scripts/tests/run-installer-library.py --source /path/to/replayed-xnu --qualification docs/qualification/0453 --cache /path/to/cache
```
