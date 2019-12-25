export ARCH=arm64
export DTC_EXT=dtc
if [ "$1" == "gcc" ]
then
export CROSS_COMPILE=../../../toolchain/gcc-linaro-6.5.0-2018.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
elif [ "$1" == "clang" ]
then
export PATH="../../clang-r353983c1/bin:../../../toolchain/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin:${PATH}"
fi
