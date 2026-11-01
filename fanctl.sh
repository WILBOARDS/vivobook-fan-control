
#!/usr/bin/env bash
set -euo pipefail

HWMON=""

for d in /sys/class/hwmon/hwmon*; do
    if [[ -r "$d/name" ]] && [[ "$(cat "$d/name")" == "asus" ]] &&
       [[ -e "$d/pwm1_enable" ]] && [[ -e "$d/pwm2_enable" ]]; then
        HWMON="$d"
        break
    fi
done

if [[ -z "$HWMON" ]]; then
    echo "ASUS fan controller not detected."
    exit 1
fi

show_status() {
    echo "=== ASUS Vivobook Fan Status ==="

    for n in 1 2; do
        if [[ "$n" == 1 ]]; then
            label="CPU"
        else
            label="GPU"
        fi

        rpm=$(cat "$HWMON/fan${n}_input")
        mode=$(cat "$HWMON/pwm${n}_enable")

        case "$mode" in
            0) mode_name="Full speed" ;;
            1) mode_name="Manual" ;;
            2) mode_name="Automatic" ;;
            *) mode_name="Unknown ($mode)" ;;
        esac

        printf "%s Fan: %s RPM | %s\n" \
            "$label" "$rpm" "$mode_name"
    done

    echo
    echo "Hardware path: $HWMON"
}

set_mode() {
    fan="$1"
    mode="$2"

    case "$fan" in
        cpu) n=1 ;;
        gpu) n=2 ;;
        *) echo "Fan must be cpu or gpu"; exit 1 ;;
    esac

    case "$mode" in
        auto) value=2 ;;
        full) value=0 ;;
        *) echo "Mode must be auto or full"; exit 1 ;;
    esac

    echo "$value" | sudo tee "$HWMON/pwm${n}_enable" >/dev/null

    echo "$fan fan set to $mode"
}

case "${1:-status}" in
    status)
        show_status
        ;;
    cpu)
        set_mode cpu "${2:-}"
        ;;
    gpu)
        set_mode gpu "${2:-}"
        ;;
    all-auto)
        echo 2 | sudo tee "$HWMON/pwm1_enable" >/dev/null
        echo 2 | sudo tee "$HWMON/pwm2_enable" >/dev/null
        echo "Both fans restored to automatic mode."
        ;;
    all-full)
        echo 0 | sudo tee "$HWMON/pwm1_enable" >/dev/null
        echo 0 | sudo tee "$HWMON/pwm2_enable" >/dev/null
        echo "Both fans set to full speed."
        ;;
    *)
        echo "Usage:"
        echo "  ./fanctl.sh status"
        echo "  ./fanctl.sh cpu auto|full"
        echo "  ./fanctl.sh gpu auto|full"
        echo "  ./fanctl.sh all-auto"
        echo "  ./fanctl.sh all-full"
        exit 1
        ;;
esac
