"""启动器：绕过 idf.py 的 MSys 检测。

idf.py 的 __main__ 守卫里有：

    if 'MSYSTEM' in os.environ:
        print_warning(...)      # 只警告，随后什么都不做就退出

MSYSTEM 由 Git Bash 注入且无法从外部清除（harness 会重新加回），
所以在 Python 进程内部删掉它，再以 __main__ 身份执行 idf.py。
"""
import os
import runpy
import sys

os.environ.pop('MSYSTEM', None)   # 必须在解释器内部删，外部 unset 无效

IDF_PY = r'C:/Espressif/frameworks/esp-idf-v5.5.5/tools/idf.py'

# 直接执行脚本时 Python 会自动把脚本目录加进 sys.path，
# 用 runpy 则不会，需要手动补上（idf.py 依赖同目录的 python_version_checker）
sys.path.insert(0, os.path.dirname(IDF_PY))

sys.argv = [IDF_PY] + sys.argv[1:]
runpy.run_path(IDF_PY, run_name='__main__')
