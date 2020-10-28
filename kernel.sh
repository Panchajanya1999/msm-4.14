#! /bin/bash

 # Script For Building Android arm64 Kernel
 #
 # Copyright (c) 2018-2020 Panchajanya1999 <rsk52959@gmail.com>
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #      http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
 #

#Kernel building script

# Function to show an informational message
msg() {
    echo -e "\e[1;32m$*\e[0m"
}

err() {
    echo -e "\e[1;41m$*\e[0m"
    exit 1
}

##------------------------------------------------------##
##----------Basic Informations, COMPULSORY--------------##

# The defult directory where the kernel should be placed
KERNEL_DIR=/mnt/kernels/Violet/msm-4.14

# The name of the Kernel, to name the ZIP
ZIPNAME="Team-420"

# The name of the device for which the kernel is built
MODEL="Redmi Note 7 Pro"

# The codename of the device
DEVICE="violet"

# The defconfig which should be used. Get it from config.gz from
# your device or check source
DEFCONFIG=vendor/nethunter_defconfig

# Specify compiler. 
# 'clang' or 'gcc'
COMPILER=clang

# Clean source prior building. 1 is NO(default) | 0 is YES
INCREMENTAL=1

# Push ZIP to Telegram. 1 is YES | 0 is NO(default)
PTTG=0
	if [ $PTTG = 1 ]
	then
		# Set Telegram Chat ID
		CHATID="-1001231303646"
	fi

# Generate a full DEFCONFIG prior building. 1 is YES | 0 is NO(default)
DEF_REG=0

# Build dtbo.img (select this only if your source has support to building dtbo.img)
# 1 is YES | 0 is NO(default)
BUILD_DTBO=1

# Sign the zipfile
# 1 is YES | 0 is NO
SIGN=0

# Silence the compilation
# 1 is YES(default) | 0 is NO
SILENCE=0

# Debug purpose. Send logs on every successfull builds
# 1 is YES | 0 is NO(default)
LOG_DEBUG=0

##------------------------------------------------------##
##---------Do Not Touch Anything Beyond This------------##

# Check if we are using a dedicated CI ( Continuous Integration ), and
# set KBUILD_BUILD_VERSION and KBUILD_BUILD_HOST and CI_BRANCH

## Set defaults first
DISTRO=$(cat /etc/*-release | head -n 1 | cut -b 7- | sed s/.$//)
KBUILD_BUILD_HOST=$(uname -a | awk '{print $2}')
CI_BRANCH=$(git rev-parse --abbrev-ref HEAD)
export KBUILD_BUILD_HOST CI_BRANCH

## Check for CI
if [ -n "$CI" ]
then
	if [ -n "$CIRCLECI" ]
	then
		export KBUILD_BUILD_VERSION=$CIRCLE_BUILD_NUM
		export KBUILD_BUILD_HOST="Kali"
		export CI_BRANCH=$CIRCLE_BRANCH
	fi
	if [ -n "$DRONE" ]
	then
		export KBUILD_BUILD_VERSION=$DRONE_BUILD_NUMBER
		export KBUILD_BUILD_HOST=$DRONE_SYSTEM_HOST
		export CI_BRANCH=$DRONE_BRANCH
	else
		echo "Not presetting Build Version"
	fi
fi

#Check Kernel Version
KERVER=$(make kernelversion)


# Set a commit head
COMMIT_HEAD=$(git log --oneline -1)

# Set Date 
DATE=$(TZ=Asia/Jakarta date +"%Y%m%d-%T")

#Now Its time for other stuffs like cloning, exporting, etc

 clone() {
	echo " "
	if [ $COMPILER = "clang" ]
	then
		msg "|| Cloning Clang-11 ||"
		git clone --depth=1 https://github.com/Panchajanya1999/azure-clang.git clang-llvm

		# Toolchain Directory defaults to clang-llvm
		TC_DIR=$KERNEL_DIR/clang-llvm
	elif [ $COMPILER = "gcc" ]
	then
		msg "|| Cloning GCC 9.3.0 baremetal ||"
		git clone --depth=1 https://github.com/arter97/arm64-gcc.git gcc64
		git clone --depth=1 https://github.com/arter97/arm32-gcc.git gcc32
		GCC64_DIR=$KERNEL_DIR/gcc64
		GCC32_DIR=$KERNEL_DIR/gcc32
	fi

	msg "|| Cloning Anykernel ||"
	git clone --depth 1 --no-single-branch https://github.com/Panchajanya1999/AnyKernel2.git -b $DEVICE
	msg "|| Cloning libufdt ||"
	git clone https://android.googlesource.com/platform/system/libufdt "$KERNEL_DIR"/scripts/ufdt/libufdt
}

##------------------------------------------------------##

exports() {
	export KBUILD_BUILD_USER="S133PY"
	export ARCH=arm64
	export SUBARCH=arm64

	if [ $COMPILER = "clang" ]
	then
		KBUILD_COMPILER_STRING=$("$TC_DIR"/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')
		PATH=$TC_DIR/bin/:$PATH
	elif [ $COMPILER = "gcc" ]
	then
		KBUILD_COMPILER_STRING=$("$GCC64_DIR"/bin/aarch64-elf-gcc --version | head -n 1)
		PATH=$GCC64_DIR/bin/:$GCC32_DIR/bin/:/usr/bin:$PATH
	fi

	export PATH KBUILD_COMPILER_STRING
	export BOT_MSG_URL="https://api.telegram.org/bot$token/sendMessage"
	export BOT_BUILD_URL="https://api.telegram.org/bot$token/sendDocument"
	PROCS=$(nproc --all)
	export PROCS
}

##---------------------------------------------------------##

tg_post_msg() {
	curl -s -X POST "$BOT_MSG_URL" -d chat_id="$2" \
	-d "disable_web_page_preview=true" \
	-d "parse_mode=html" \
	-d text="$1"

}

##----------------------------------------------------------------##

tg_post_build() {
	#Post MD5Checksum alongwith for easeness
	MD5CHECK=$(md5sum "$1" | cut -d' ' -f1)

	#Show the Checksum alongwith caption
	curl --progress-bar -F document=@"$1" "$BOT_BUILD_URL" \
	-F chat_id="$2"  \
	-F "disable_web_page_preview=true" \
	-F "parse_mode=html" \
	-F caption="$3 | <b>MD5 Checksum : </b><code>$MD5CHECK</code>"  
}

##----------------------------------------------------------##

build_kernel() {
	if [ $INCREMENTAL = 0 ]
	then
		msg "|| Cleaning Sources ||"
		make clean && make mrproper && rm -rf out
	fi

	if [ "$PTTG" = 1 ]
 	then
		tg_post_msg "<b>$KBUILD_BUILD_VERSION CI Build Triggered</b>%0A<b>Docker OS: </b><code>$DISTRO</code>%0A<b>Kernel Version : </b><code>$KERVER</code>%0A<b>Date : </b><code>$(TZ=Asia/Kolkata date)</code>%0A<b>Device : </b><code>$MODEL [$DEVICE]</code>%0A<b>Pipeline Host : </b><code>$KBUILD_BUILD_HOST</code>%0A<b>Host Core Count : </b><code>$PROCS</code>%0A<b>Compiler Used : </b><code>$KBUILD_COMPILER_STRING</code>%0a<b>Branch : </b><code>$CI_BRANCH</code>%0A<b>Top Commit : </b><code>$COMMIT_HEAD</code>%0A<b>Status : </b>#Nightly" "$CHATID"
	fi

	make O=out $DEFCONFIG
	if [ $DEF_REG = 1 ]
	then
		cp .config arch/arm64/configs/$DEFCONFIG
		git add arch/arm64/configs/$DEFCONFIG
		git commit -m "$DEFCONFIG: Regenerate

						This is an auto-generated commit"
	fi

	BUILD_START=$(date +"%s")
	
	if [ $COMPILER = "clang" ]
	then
		MAKE+=(
			CROSS_COMPILE=aarch64-linux-gnu- \
			CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
			CC=clang \
			AR=llvm-ar \
			OBJDUMP=llvm-objdump \
			STRIP=llvm-strip
		)
		
	elif [ $COMPILER = "gcc" ]
	then
		MAKE+=(
			CROSS_COMPILE_ARM32=arm-eabi- \
			CROSS_COMPILE=aarch64-elf- \
			AR=aarch64-elf-ar \
			OBJDUMP=aarch64-elf-objdump \
			STRIP=aarch64-elf-strip
		)
	fi
	
	if [ $SILENCE = "1" ]
	then
		MAKE+=( -s )
	fi

	msg "|| Started Compilation ||"
	make -j"$PROCS" O=out \
		NM=llvm-nm \
		OBJCOPY=llvm-objcopy \
		LD=ld.lld "${MAKE[@]}" 2>&1 | tee error.log

		BUILD_END=$(date +"%s")
		DIFF=$((BUILD_END - BUILD_START))

		if [ -f "$KERNEL_DIR"/out/arch/arm64/boot/Image.gz-dtb ] 
	    then
	    	msg "|| Kernel successfully compiled ||"
	    	if [ $BUILD_DTBO = 1 ]
			then
				msg "|| Building DTBO ||"
				tg_post_msg "<code>Building DTBO..</code>" "$CHATID"
				python2 "$KERNEL_DIR/scripts/ufdt/libufdt/utils/src/mkdtboimg.py" \
					create "$KERNEL_DIR/out/arch/arm64/boot/dtbo.img" --page_size=4096 "$KERNEL_DIR/out/arch/arm64/boot/dts/qcom/sm6150-idp-overlay.dtbo"
			fi
				gen_zip
		else
			if [ "$PTTG" = 1 ]
 			then
				tg_post_build "error.log" "$CHATID" "<b>Build failed to compile after $((DIFF / 60)) minute(s) and $((DIFF % 60)) seconds</b>"
			fi
		fi
	
}

##--------------------------------------------------------------##

gen_zip() {
	msg "|| Zipping into a flashable zip ||"
	mv "$KERNEL_DIR"/out/arch/arm64/boot/Image.gz-dtb AnyKernel2/Image.gz-dtb
	if [ $BUILD_DTBO = 1 ]
	then
		mv "$KERNEL_DIR"/out/arch/arm64/boot/dtbo.img AnyKernel2/dtbo.img
	fi
	cd AnyKernel2 || exit
	zip -r9 $ZIPNAME-$DEVICE-"$DATE" * -x .git README.md

	## Prepare a final zip variable
	ZIP_FINAL="$ZIPNAME-$DEVICE-$DATE.zip"

	if [ $SIGN = 1 ]
	then
		## Sign the zip before sending it to telegram
		if [ "$PTTG" = 1 ]
 		then
 			msg "|| Signing Zip ||"
 			tg_post_msg "<code>Signing Zip file with AOSP keys..</code>" "$CHATID"
 		fi
		curl -sLo zipsigner-3.0.jar https://raw.githubusercontent.com/baalajimaestro/AnyKernel2/master/zipsigner-3.0.jar
		java -jar zipsigner-3.0.jar $ZIPNAME-$DEVICE-"$DATE".zip "$ZIP_FINAL"-signed.zip
		ZIP_FINAL="$ZIP_FINAL-signed.zip"
	fi

	if [ "$PTTG" = 1 ]
 	then
		tg_post_build "$ZIP_FINAL" "$CHATID" "Build took : $((DIFF / 60)) minute(s) and $((DIFF % 60)) second(s)"
	fi
	cd ..
}

clone
exports
build_kernel

if [ $LOG_DEBUG = "1" ]
then
	tg_post_build "error.log" "$CHATID" "Debug Mode Logs"
fi

##----------------*****-----------------------------##
