# ビルド専用のイメージ。macOS など Linux 以外のホストでカーネルをビルドするために使う。
# 中身は CI (.github/workflows/ci.yml) の Install toolchain と同じにしてあり、
# 手元・CI・Docker で同じツールチェインになる。QEMU はホスト側のものを使うので入れない。
#
#   make docker-iso   … このイメージでビルドして ISO を作る
#   make docker-run   … 同上のあと、ホストの QEMU で起動
#
# grub-pc-bin (BIOS 用 GRUB) は amd64 にしか無いので、Apple Silicon でも
# linux/amd64 で動かす (Makefile が --platform を付ける)。
FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       make g++-13 binutils nasm grub-common grub-pc-bin xorriso mtools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
