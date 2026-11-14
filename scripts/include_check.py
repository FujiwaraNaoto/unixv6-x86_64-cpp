#!/usr/bin/env python3
# include の過不足チェック (clang-tidy の misc-include-cleaner)。
#
#   make include-check                  … 手元で実行 (clang-tidy-18 以上が必要)
#   make include-check CLANG_TIDY=...   … clang-tidy のパスを指定
#
# 報告するのは 2 種類:
#   - included header X is not used directly   … 使っていない include
#   - no header providing "Y" is directly included … 別のヘッダ経由で入っているだけの記号
#
# 手順:
#   1. make -n -B で実際のコンパイルコマンドを取り出し、build/compile_commands.json を作る。
#      Makefile のフラグを二重管理しないため、自前でフラグを組み立てない。
#      通常のソースは通常構成 (TESTS=0) で見る。init/main.cpp の #ifdef ENABLE_TESTS の
#      向こう側の include を誤って「未使用」と判定しないため。tests/ だけテスト構成で見る。
#   2. ヘッダも 1 つずつ main file として検査する (clang-tidy は main file しか見ないため)。
#   3. --github を付けると GitHub Actions の ::error 注釈としても出力する。
#
# 意図的な再エクスポートには、include の行末に `// IWYU pragma: export` を付ける。
# 検出 (パースエラー含む) が 1 件でもあれば終了コード 1。CI はこれで落ちる。

import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPDB_DIR = os.path.join(ROOT, 'build')

# libc の関数 (memset など) を自前で定義するファイル。宣言を include しない作りなので、
# 「memset を提供するヘッダが無い」と言われ続けるうえ、ローカル変数まで誤検出される。
EXCLUDE = {'lib/crt_stubs.cpp'}


def compile_commands(tests):
    out = subprocess.check_output(['make', '-n', '-B', f'TESTS={int(tests)}', 'all'], cwd=ROOT, text=True,
                                  stderr=subprocess.DEVNULL)
    cmds = {}
    for line in out.splitlines():
        args = shlex.split(line)
        if not args or not args[0].startswith('g++') or '-c' not in args:
            continue
        args = [a for a in args if a not in ('-MMD', '-MP')]
        cmds[args[-1]] = args
    return cmds


def main():
    clang_tidy = os.environ.get('CLANG_TIDY', 'clang-tidy-18')
    github = '--github' in sys.argv

    kernel = compile_commands(tests=False)
    tests = compile_commands(tests=True)
    entries = {f: a for f, a in kernel.items()}
    entries.update({f: a for f, a in tests.items() if f.startswith('tests/')})

    # ヘッダはテスト構成のフラグで検査する (tests/ も -I に入っていて、どのヘッダも見つかる)
    base = next(iter(tests.values()))
    base = base[:base.index('-c')]
    headers = subprocess.check_output(['git', 'ls-files', '*.hpp', '*.h'], cwd=ROOT, text=True).split()
    for h in headers:
        entries[h] = base + ['-x', 'c++', '-fsyntax-only', h]

    os.makedirs(COMPDB_DIR, exist_ok=True)
    db = [{'directory': ROOT, 'file': f, 'arguments': a} for f, a in sorted(entries.items())]
    with open(os.path.join(COMPDB_DIR, 'compile_commands.json'), 'w') as fp:
        json.dump(db, fp, indent=1)

    files = sorted(f for f in entries if f not in EXCLUDE)
    proc = subprocess.run([clang_tidy, '-p', COMPDB_DIR, '--checks=-*,misc-include-cleaner', '--quiet', *files],
                          cwd=ROOT, text=True, capture_output=True)
    pat = re.compile(r'^(?P<file>[^:\s]+):(?P<line>\d+):(?P<col>\d+): (?P<kind>warning|error): (?P<msg>.*?)'
                     r'(?: \[[\w.-]+\])?$')
    found = set()
    errors = 0
    for line in (proc.stdout + proc.stderr).splitlines():
        m = pat.match(line)
        if not m:
            continue
        path = os.path.relpath(os.path.join(ROOT, m['file']), ROOT)
        key = (path, int(m['line']), int(m['col']), m['kind'], m['msg'])
        if key in found:
            continue
        found.add(key)
        if m['kind'] == 'error':
            errors += 1

    for path, line, col, kind, msg in sorted(found):
        print(f'{path}:{line}:{col}: {kind}: {msg}')
        if github:
            print(f'::error file={path},line={line},col={col},title=include-cleaner::{msg}')

    unused = sum(1 for k in found if 'not used directly' in k[4])
    missing = sum(1 for k in found if 'no header providing' in k[4])
    print(f'\n[include-check] unused: {unused}, missing: {missing}, parse errors: {errors} '
          f'({len(files)} files)')
    if errors:
        print('[include-check] clang がパースできないファイルがある。結果が欠けている可能性がある。')
    if found:
        sys.exit(1)


if __name__ == '__main__':
    main()
