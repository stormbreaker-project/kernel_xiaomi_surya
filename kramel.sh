git clone https://github.com/SreekanthPalakurthi/Clang-dumpyard.git --depth=1 clang
git clone https://github.com/SreekanthPalakurthi/Clang-dumpyard.git -b gcc64 --depth=1 gcc
git clone https://github.com/SreekanthPalakurthi/Clang-dumpyard.git -b gcc32  --depth=1 gcc32
git clone --depth=1 https://github.com/stormbreaker-project/AnyKernel3.git -b surya
export TZ=Asia/Kolkata 
IMAGE=$(pwd)/out/arch/arm64/boot/Image
DTBO=$(pwd)/out/arch/arm64/boot/dtbo.img
TANGGAL=${VERSION}-$(date +"%d%m%H%M")
START=$(date +"%s")
CLANG_VERSION=$(clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')
BRANCH=$(git rev-parse --abbrev-ref HEAD)
KERNEL_DIR=$(pwd)
PATH="${KERNEL_DIR}/clang/bin:${KERNEL_DIR}/gcc/bin:${KERNEL_DIR}/gcc32/bin:${PATH}"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_HOST="Forenche"
export KBUILD_BUILD_USER="StormBreakerCI"
export chat_id="-1001683587045"
export DEF="surya_defconfig"
export VERSION="Idk"
export KBUILD_COMPILER_STRING=$("$KERNEL_DIR"/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')
LNXVER=$(make O=out kernelversion  --no-print-directory)
DEF_REG=0
BUILD_DTBO=1
SIGN_BUILD=0
INCREMENTAL=1

if [ $INCREMENTAL = 0 ]
then
	make O=out clean && make O=out mrproper && rm -rf out && cd AnyKernel3/ && rm -rf * && git reset --hard && cd ..
fi


if [ $DEF_REG = 1 ]
then
make O=out ARCH=arm64 ${DEF}
mv out/.config arch/arm64/configs/${DEF}
		git add arch/arm64/configs/${DEF}
		git commit -m "defconfig: Regenerate
						This is an auto-generated commit"
fi

curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Buckle up bois ${BRANCH} build has started" -d chat_id=${chat_id} -d parse_mode=HTML

echo "CONFIG_PATCH_INITRAMFS=y" >> arch/arm64/configs/surya_defconfig

make O=out ARCH=arm64 $DEF
	make -j$(nproc --all) O=out \
                      CC=clang \
		      CLANG_TRIPLE=aarch64-linux-gnu- \
                      CROSS_COMPILE=aarch64-linux-android- \
                      CROSS_COMPILE_ARM32=arm-linux-androideabi- \
		      LD=ld.lld \
		      AS=llvm-as \
		      AR=llvm-ar \
                      NM=llvm-nm \
                      OBJCOPY=llvm-objcopy \
                      OBJDUMP=llvm-objdump \
                      STRIP=llvm-strip 2>&1 | tee build.log
		      
END=$(date +"%s")
DIFF=$((END - START))
if [ -f $(pwd)/out/arch/arm64/boot/Image ]
	then
        if [ BUILD_DTBO = 1 ]
        then
		git clone --depth=1 https://android.googlesource.com/platform/system/libufdt libufdt
                python2 "libufdt/utils/src/mkdtboimg.py" \
					        create "out/arch/arm64/boot/dtbo.img" --page_size=4096 $(pwd)/out/arch/arm64/boot/dts/qcom/*.dtbo
        fi
# Post to CI channel
curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Branch: <code>$(git rev-parse --abbrev-ref HEAD)</code>
Compiler Used : <code>${CLANG_VERSION} </code>
Latest Commit: <code>$(git log --pretty=format:'%h : %s' -1)</code>
Linux Version : <code>${LNXVER}</code>
<i>Build compiled successfully in $((DIFF / 60)) minute(s) and $((DIFF % 60)) seconds</i>" -d chat_id=${chat_id} -d parse_mode=HTML
#curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Flash now else bun" -d chat_id=${chat_id} -d parse_mode=HTML

cp $(pwd)/out/arch/arm64/boot/Image $(pwd)/AnyKernel3
cp $(pwd)/out/arch/arm64/boot/dtb.img $(pwd)/AnyKernel3

        if [ -f ${DTBO} ]
        then
                cp ${DTBO} $(pwd)/AnyKernel3
        fi

        cd AnyKernel3 && make normal
	ZIP_FINAL=$(echo *.zip)

        if [ SIGN_BUILD = 1 ]
        then
                java -jar zipsigner-4.0.jar  StormBreaker-surya-${TANGGAL}.zip StormBreaker-surya-${TANGGAL}-signed.zip

        curl -F chat_id="${chat_id}"  \
                    -F caption="sha1sum: $(sha1sum Storm*-signed.zip | awk '{ print $1 }')" \
                    -F document=@"$(pwd)/StormBreaker-surya-${TANGGAL}-signed.zip" \
                    https://api.telegram.org/bot${TOKEN}/sendDocument


        else

        curl -F chat_id="${chat_id}"  \
                    -F caption="sha1sum: $(sha1sum Storm*.zip | awk '{ print $1 }')" \
                    -F document=@"$ZIP_FINAL" \
                    https://api.telegram.org/bot${TOKEN}/sendDocument
	fi

    curl -s -X POST "https://api.telegram.org/bot$TOKEN/sendSticker" \
        -d sticker="CAACAgUAAxkBAAJi017AAw5j25_B3m8IP-iy98ffcGHZAAJAAgACeV4XIusNfRHZD3hnGQQ" \
        -d chat_id="$chat_id"
cd ..
else
        curl -F chat_id="${chat_id}"  \
                    -F caption="Build ended with an error, F in the chat plox" \
                    -F document=@"build.log" \
                    https://api.telegram.org/bot${TOKEN}/sendDocument

        curl -s -X POST "https://api.telegram.org/bot$TOKEN/sendSticker" \
        -d sticker="CAACAgUAAxkBAAK74mCvV3W62vmSIcqQo61RtBxEK0dVAALGAgACw2B4VehbCiKmZwTjHwQ" \
        -d chat_id="$chat_id"

fi

if [[ -f ${IMAGE} &&  ${DTBO} ]]
then
   mv -f $IMAGE ${DTBO} AnyKernel3
fi
