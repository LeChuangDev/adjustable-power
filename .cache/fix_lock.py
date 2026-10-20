"""把 dependencies.lock 里指向旧项目位置的绝对路径改成当前位置。

组件管理器在解析 type: local 的组件时会把绝对路径记进 lock 文件，
项目目录一搬动这些路径就失效，CMake 配置阶段直接 FATAL_ERROR。
"""
import io
import os

LOCK = 'dependencies.lock'
OLD = 'C:\\Users\\zhang\\Desktop\\adjustable-power'
NEW = 'C:\\Users\\zhang\\DeskBox\\项目\\adjustable-power'

text = io.open(LOCK, encoding='utf-8').read()
count = text.count(OLD)
if count == 0:
    print('no stale paths found (already fixed?)')
else:
    io.open(LOCK, 'w', encoding='utf-8', newline='').write(text.replace(OLD, NEW))
    print('replaced %d occurrences' % count)
