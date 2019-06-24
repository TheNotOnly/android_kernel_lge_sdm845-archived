#!/bin/bash

echo "Choose options:"
echo "1: clean working tree"
echo "2: clean and compile"
echo "3: clean, compile and pack boot image"
echo "4: clean, compile and create anykernel zip"

echo "Please choose an option"
read option

echo "Setting up working environment"
source ./setenv.sh

echo $ARCH
echo $DTC_EXT
echo $CROSS_COMPILE

version="$(./kernelversion.sh | grep -v make)"
echo "Kernel Version is ${version}"

case $option in

	1)
	./clean.sh
	;;

	2)
	./clean.sh
	./defconfig
	./make.sh
	;;

	3)
	echo "Please enter your password:"
	read pw
	./clean.sh
	./defconfig.sh
	./make.sh

	cp out/arch/arm64/boot/Image.gz-dtb AIK-Linux/split_img/boot.img-zImage
	cd AIK-Linux
	echo $pw | sudo -S ./repackimg.sh
	rm split-img/boot.img-zImage
	mv image-new.img ../releases/boot-$version.img
	;;

	4)
	./clean.sh
	./defconfig.sh
	./make.sh
	cp out/arch/arm64/boot/Image.gz-dtb anykernel3/
	cd anykernel3
	zip -r9 $version.zip * -x .git README.md *placeholder
	mv $version.zip ../releases
	rm Image.gz-dtb
	;;
esac
