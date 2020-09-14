export PATH="$HOME/tc/proton-clang/bin:$PATH"
SECONDS=0
ZIPNAME="TestKernel-LA.UM.9.x-surya-$(date '+%Y%m%d-%H%M').zip"

if ! [ -d "$HOME/tc/proton-clang" ]; then
echo "Proton clang not found! Cloning..."
if ! git clone -q --depth=1 --single-branch https://github.com/kdrag0n/proton-clang ~/proton; then
echo "Cloning failed! Aborting..."
exit 1
fi
fi

mkdir -p out
make O=out ARCH=arm64 vendor/surya-perf_defconfig

if [[ $1 == "-r" || $1 == "--regen" ]]; then
cp out/.config arch/arm64/configs/vendor/surya-perf_defconfig
echo -e "\nRegened defconfig succesfully!"
exit 0
else
echo -e "\nStarting compilation...\n"
make -j$(nproc --all) O=out ARCH=arm64 CC=clang AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- Image.gz-dtb dtbo.img
fi

if [ -f "out/arch/arm64/boot/Image.gz-dtb" ] && [ -f "out/arch/arm64/boot/dtbo.img" ]; then
echo -e "\nKernel compiled succesfully! Zipping up...\n"
git clone -q https://github.com/ghostrider-reborn/AnyKernel3 -b surya
cp out/arch/arm64/boot/Image.gz-dtb AnyKernel3
cp out/arch/arm64/boot/dtbo.img AnyKernel3
rm -f *zip
cd AnyKernel3
zip -r9 "../$ZIPNAME" * -x '*.git*' README.md *placeholder
cd ..
rm -rf AnyKernel3
echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s) !"
if command -v gdrive &> /dev/null; then
gdrive upload --share $ZIPNAME
else
echo "Zip: $ZIPNAME"
fi
rm -rf out/arch/arm64/boot
else
echo -e "\nCompilation failed!"
fi
