FROM docker.io/zmkfirmware/zmk-build-arm:stable

WORKDIR /app

COPY config/west.yml config/west.yml

# West Init
RUN west init -l config
# West Update
RUN west update
COPY patches ./layout_patches/
RUN git -C zmk apply /app/layout_patches/zmk-split-notify-backpressure.patch \
    && git -C zmk apply /app/layout_patches/zmk-usb-hid-stable-buffer.patch
# West Zephyr export
RUN west zephyr-export

COPY CMakeLists.txt Kconfig ./layout_module/
COPY zephyr ./layout_module/zephyr
COPY dts ./layout_module/dts
COPY include ./layout_module/include
COPY src ./layout_module/src

COPY bin/build.sh ./

CMD ["./build.sh"]
