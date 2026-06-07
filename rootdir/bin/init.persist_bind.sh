#!/vendor/bin/sh

log_msg() {
    /system/bin/log -t init.persist_bind "$1" 2>/dev/null
}

prepare_sensor_dirs() {
    mkdir -p /persist/sensors/registry/registry
    chown system system /persist/sensors
    chown system system /persist/sensors/registry
    chown system system /persist/sensors/registry/registry
    chown system system /persist/sensors/registry/registry/sensors_registry 2>/dev/null
    chown system system /persist/sensors/sensors_settings 2>/dev/null
    chown system system /persist/sensors/registry/sns_reg_config 2>/dev/null
    chown system system /persist/sensors/registry/config 2>/dev/null
    chmod 0775 /persist/sensors
    chmod 0775 /persist/sensors/registry
    chmod 0775 /persist/sensors/registry/registry
    chmod 0664 /persist/sensors/sensors_settings 2>/dev/null
}

mkdir -p /persist
setprop vendor.persist.bind.ready 0

for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
    if grep -q " /mnt/vendor/persist " /proc/mounts; then
        if ! grep -q " /persist " /proc/mounts; then
            /system/bin/mount -o bind /mnt/vendor/persist /persist
        fi

        if grep -q " /persist " /proc/mounts; then
            prepare_sensor_dirs
            setprop vendor.persist.bind.ready 1
            log_msg "bound /mnt/vendor/persist to /persist on attempt ${attempt}"
            exit 0
        fi
    fi

    sleep 1
done

log_msg "failed to bind /mnt/vendor/persist to /persist"
exit 1
