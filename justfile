[no-cd, no-exit-message]
app_build:
    west build -b adafruit_feather_nrf52840

[no-cd, no-exit-message]
flash:
    west flash

[no-cd, no-exit-message]
rtt:
    west rtt

[no-cd, no-exit-message]
build:
    #!/usr/bin/env sh
    if [ -z `just find_nearest_prj` ]; then
        echo "Couldn't find prj.conf in current or parent directory(s)"
        exit 1
    fi
    west build -b native_sim -p auto -s `just find_nearest_prj`

[no-cd, no-exit-message]
@ run:
    just build
    build/zephyr/zephyr.exe

[no-exit-message]
@ test test_dir='.':
    twister --clobber-output --coverage --coverage-basedir $APP_ROOT -T {{test_dir}} --platform native_sim

[no-cd, no-exit-message]
@ clean:
    echo "Deleting build dir..."
    rm -R build

[no-cd, no-exit-message]
@ prepare_artifacts:
	mkdir -p artifacts
	cp build/zephyr/zephyr.hex artifacts/marlin-lpm-$(git rev-parse --short HEAD).hex
	cp build/zephyr/zephyr.bin artifacts/marlin-lpm-$(git rev-parse --short HEAD).bin
	cp build/zephyr/zephyr.elf artifacts/marlin-lpm-$(git rev-parse --short HEAD).elf
	cp proto/core.proto artifacts/marlin-lpm-$(git rev-parse --short HEAD).proto


[no-cd, private, no-exit-message]
find_nearest_prj:
    #!/usr/bin/env sh
    current_dir=$(pwd)
    while [ "$current_dir" != "/" ]; do
        if [ -f "$current_dir/prj.conf" ]; then
            echo "$current_dir"
            exit 0
        fi
        current_dir=$(dirname "$current_dir")
    echo ""
    done


[no-exit-message]
install_module module_subdir +cmake_options="":
    #!/usr/bin/env sh
    cd $WEST_WORKSPACE/{{module_subdir}}
    mkdir -p build
    cmake {{cmake_options}} . -B build
    cd build
    sudo make install

[no-exit-message]
install_zephyr:
    west init -l .
    west update --group-filter=-private

[no-exit-message]
install_all:
    just install_zephyr
    just install_module magic_enum -DMAGIC_ENUM_OPT_BUILD_EXAMPLES=OFF -DMAGIC_ENUM_OPT_BUILD_TESTS=OFF
    just install_module etl
