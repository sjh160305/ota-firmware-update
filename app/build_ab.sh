#!/bin/bash
###############################################################################
# build_ab.sh  -  把 App(2322) 链接成 A/B 两个地址的裸 bin (用于 A/B XIP OTA)
#
# 用法:
#   1) 先在 CDK IDE 里正常 Build 一次 2322 (编译出 Obj/*.o 和各 .a 库)
#   2) 在本目录 Git Bash 运行:  ./build_ab.sh
#   产物 (Obj/ 下):
#     hello_world_A_0x18010000.bin   链到 BANK_A, 入口 0x18010044
#     hello_world_B_0x18040000.bin   链到 BANK_B, 入口 0x18040044
#
# 升级规则: 当前跑 A 就发 B 的 bin, 跑 B 就发 A 的 bin (Boot 会校验入口是否落在
#           目标槽窗口, 发错直接拒收)。这是裸 XIP 镜像, 与 CRC32 版 Boot 匹配;
#           不是 aft_build.sh 那种带 mnt 头的签名镜像。
###############################################################################
set -e
cd "$(dirname "$0")"

# --- 工具链 (不在 PATH 时用 CDKRepo 里的 Xuantie GCC) ---
if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    export PATH="/d/C-Sky/CDKRepo/Toolchain/XTGccElfNewlib/V2.6.1/R/bin:$PATH"
fi
GCC=riscv64-unknown-elf-gcc
OBJCOPY=riscv64-unknown-elf-objcopy
READELF=riscv64-unknown-elf-readelf

# --- 链接参数 (与 hello_world.mk 保持一致, 仅去掉 -Tapp.ld 由本脚本指定) ---
COMMON="-mcpu=e906fd --specs=nosys.specs -nostartfiles -Wl,--gc-sections -Wl,-zmax-page-size=1024"
LIBS="-Wl,--whole-archive -lchip_td1601 -lconsole -lcsi -lminilibc -lmm -ltd1601_evb -Wl,--no-whole-archive -lm"
LIBPATH="-LObj \
 -LObj/BuildSet/Packages/chip_td1601/V1.0.0 \
 -LObj/BuildSet/Packages/console/V1.0.0 \
 -LObj/BuildSet/Packages/csi/V1.0.0 \
 -LObj/BuildSet/Packages/minilibc/V1.0.0 \
 -LObj/BuildSet/Packages/mm/V1.0.0 \
 -LObj/BuildSet/Packages/td1601_evb/V1.0.0"

link_one() {
    local tag="$1" ld="$2" addr="$3"
    local elf="Obj/hello_world_${tag}.elf"
    local bin="Obj/hello_world_${tag}_${addr}.bin"
    echo "==> 链接 $tag  ($ld, ORIGIN $addr)"
    $GCC -o "$elf" @hello_world.txt $COMMON -T"$ld" $LIBPATH $LIBS
    $OBJCOPY -O binary "$elf" "$bin"
    local entry
    entry=$($READELF -h "$elf" | sed -n 's/.*Entry point address:\s*//p')
    echo "    OK -> $bin   (entry $entry, $(wc -c < "$bin") bytes)"
}

link_one A gcc_flash_app_a.ld  0x18012000
link_one B gcc_flash_app_b.ld 0x18109000 
echo "完成。A/B 两个裸 XIP bin 已生成在 Obj/ 下。"
