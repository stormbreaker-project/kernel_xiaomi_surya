git clone --depth=1 https://github.com/mvaisakh/gcc-arm64.git -b gcc-master gcc64
git clone --depth=1 https://github.com/mvaisakh/gcc-arm.git -b gcc-master gcc32
git clone --depth=1 https://github.com/stormbreaker-project/AnyKernel3.git -b surya

export TZ=Asia/Kolkata 
IMAGE=$(pwd)/out/arch/arm64/boot/Image.gz
DTBO=$(pwd)/out/arch/arm64/boot/dtbo.img
TANGGAL=${VERSION}-$(date +"%d%m%H%M")
START=$(date +"%s")
BRANCH=$(git rev-parse --abbrev-ref HEAD)
export VERSION=X9-BETA
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_HOST="Forenche"
export KBUILD_BUILD_USER="StormBreakerCI"
export chat_id="-1001683587045"
export DEF="surya_defconfig"
TC_DIR=${PWD}
GCC64_DIR="${PWD}/gcc64"
GCC32_DIR="${PWD}/gcc32"
export PATH="$TC_DIR/bin/:$GCC64_DIR/bin/:$GCC32_DIR/bin/:/usr/bin:$PATH"
BUILD_DTBO=1
SIGN_BUILD=0

echo "CONFIG_PATCH_INITRAMFS=y" >> arch/arm64/configs/surya_defconfig

curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Buckle up bois ${BRANCH} build has started" -d chat_id=${chat_id} -d parse_mode=HTML

   make O=out ARCH=arm64 $DEF
       make -j$(nproc --all) O=out \
				ARCH=arm64 \
				CROSS_COMPILE_ARM32=arm-eabi- \
				CROSS_COMPILE=aarch64-elf- \
				AR=llvm-ar \
				NM=llvm-nm \
				OBJCOPY=llvm-objcopy \
				LD=aarch64-elf-ld.lld 2>&1 | tee build.log

END=$(date +"%s")
DIFF=$((END - START))

if [ -f $(pwd)/out/arch/arm64/boot/Image.gz ]
	then
        if [ BUILD_DTBO = 1 ]
        then
		git clone --depth=1 https://android.googlesource.com/platform/system/libufdt libufdt
                python2 "libufdt/utils/src/mkdtboimg.py" \
					        create "out/arch/arm64/boot/dtbo.img" --page_size=4096 $(pwd)/out/arch/arm64/boot/dts/qcom/*.dtbo
        fi
# Post to CI channel
curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Branch: <code>$(git rev-parse --abbrev-ref HEAD)</code>
Compiler Used : <code>GCC aka Giga Chad Compiler</code>
Latest Commit: <code>$(git log --pretty=format:'%h : %s' -1)</code>
<i>Build compiled successfully in $((DIFF / 60)) minute(s) and $((DIFF % 60)) seconds</i>" -d chat_id=${chat_id} -d parse_mode=HTML
#curl -s -X POST https://api.telegram.org/bot${TOKEN}/sendMessage -d text="Flash now else bun" -d chat_id=${chat_id} -d parse_mode=HTML

cp $(pwd)/out/arch/arm64/boot/Image.gz $(pwd)/AnyKernel3
cp $(pwd)/out/arch/arm64/boot/dtb.img $(pwd)/AnyKernel3/dtb.img

        if [ -f ${DTBO} ]
        then
                cp ${DTBO} $(pwd)/AnyKernel3
        fi

        cd AnyKernel3
        make normal
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
