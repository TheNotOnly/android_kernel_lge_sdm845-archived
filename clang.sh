PATH="../../clang-r353983c1/bin:../../aarch64-linux-android-4.9/bin:${PATH}" \
make -j7 O=out \
                      ARCH=arm64 \
                      CC=clang \
                      CLANG_TRIPLE=aarch64-linux-gnu- \
                      CROSS_COMPILE=aarch64-linux-android-
