#!/bin/bash

echo "Choose compiler:"
read -p "clang/gcc: " -e -i clang compiler
echo ""

if [ "$compiler" == "clang" ]
then

source ./setenv.sh clang

else

source ./setenv.sh gcc

fi

if [ -z $1 ] && [ -z $2 ]
then

echo "Choose options:"
echo "1: clean working tree"
echo "2: compile"
echo "3: clean and compile"
echo "4: clean, compile and pack boot image"
echo "5: clean, compile and create anykernel zip"
echo "6: clean, compile, pack boot image and create anykernel zip"
echo ""

read -p "Please choose an option: " -e -i 2 option
echo ""

read -p "Please specify number of cores: " -e -i auto cores
echo ""

else

option=$1
cores=$2

fi

if [ "$cores" == "auto" ]
then
cores=$(ncpus)+1
cores=$((cores+1))
fi

echo "Setting up working environment"
source ./setenv.sh

echo ""
echo "ARCH="$ARCH
echo "DTC_EXT="$DTC_EXT
if [ "$compiler" == "gcc" ]
then
echo "CROSS_COMPILE="$CROSS_COMPILE
elif [ "$compiler" == "clang" ]
then
echo "PATH="$PATH
fi
echo ""

version="$(./kernelversion.sh | grep -v make)"
echo "Kernel Version is ${version}"
echo ""

if [[ ! -e releases ]]; then
	mkdir releases
fi

case $option in

	1)
	./clean.sh
	;;

	2)
	./make.sh $cores $compile
	;;

	3)
	./clean.sh
	./defconfig.sh
	./make.sh $cores $compiler
	;;

	4)
	echo "Please enter your password:"
	read -s pw
	./clean.sh
	./defconfig.sh
	./make.sh $cores $compiler

	cp out/arch/arm64/boot/Image.gz-dtb AIK-Linux/split_img/boot.img-zImage
	cd AIK-Linux
	echo $pw | sudo -S ./repackimg.sh
	rm split_img/boot.img-zImage
	mv image-new.img ../releases/TNOKernel-v$version.img
	;;

	5)
	./clean.sh
	./defconfig.sh
	./make.sh $cores $compiler
	cp out/arch/arm64/boot/Image.gz-dtb anykernel3/
	cd anykernel3
	zip -r9 TNOKernel-v$version.zip * -x .git README.md *placeholder
	mv TNOKernel-v$version.zip ../releases
	rm Image.gz-dtb
	;;

	6)
        echo "Please enter your password:"
        read -s pw
        ./clean.sh
        ./defconfig.sh
        ./make.sh $cores $compiler

        cp out/arch/arm64/boot/Image.gz-dtb AIK-Linux/split_img/boot.img-zImage
        cd AIK-Linux
        echo $pw | sudo -S ./repackimg.sh
        rm split_img/boot.img-zImage
        mv image-new.img ../releases/TNOKernel-v$version.img

        cp out/arch/arm64/boot/Image.gz-dtb anykernel3/
        cd anykernel3
        zip -r9 TNOKernel-v$version.zip * -x .git README.md *placeholder
        mv TNOKernel-v$version.zip ../releases
        rm Image.gz-dtb

esac

