#!/usr/bin/env bash
set -euo pipefail

vendor="057e"
product="0337"
rule_path="/etc/udev/rules.d/51-gcadapter-webhid.rules"
vendor_num="$(printf '%x' "$((16#$vendor))")"
product_num="$(printf '%x' "$((16#$product))")"

find_interfaces() {
  for iface in /sys/bus/usb/devices/*:*; do
    [[ -f "$iface/bInterfaceClass" ]] || continue
    [[ "$(cat "$iface/bInterfaceClass")" == "03" ]] || continue
    dev="${iface%:*}"
    [[ -f "$dev/idVendor" && -f "$dev/idProduct" ]] || continue
    [[ "$(cat "$dev/idVendor")" == "$vendor" && "$(cat "$dev/idProduct")" == "$product" ]] || continue
    basename "$iface"
  done
}

hid_id_matches() {
  local uevent="$1"
  local hid_id bus got_vendor got_product
  hid_id="$(grep '^HID_ID=' "$uevent" 2>/dev/null | cut -d= -f2 || true)"
  [[ -n "$hid_id" ]] || return 1
  IFS=: read -r bus got_vendor got_product <<< "$hid_id"
  got_vendor="$(printf '%x' "$((16#$got_vendor))")"
  got_product="$(printf '%x' "$((16#$got_product))")"
  [[ "$got_vendor" == "$vendor_num" && "$got_product" == "$product_num" ]]
}

print_status() {
  echo "USB devices:"
  lsusb | grep -i "${vendor}:${product}" || echo "  no ${vendor}:${product} adapter visible to lsusb"
  echo

  echo "USB HID interfaces:"
  found=0
  while IFS= read -r iface; do
    [[ -n "$iface" ]] || continue
    found=1
    driver_path="/sys/bus/usb/devices/$iface/driver"
    if [[ -L "$driver_path" ]]; then
      driver="$(readlink -f "$driver_path")"
      echo "  $iface driver: ${driver##*/}"
    else
      echo "  $iface driver: none"
    fi
  done < <(find_interfaces)
  [[ "$found" == "1" ]] || echo "  no HID interface found for ${vendor}:${product}"
  echo

  echo "hidraw nodes:"
  hidraw_found=0
  for node in /sys/class/hidraw/hidraw*; do
    [[ -e "$node/device/uevent" ]] || continue
    if hid_id_matches "$node/device/uevent"; then
      hidraw_found=1
      echo "  /dev/${node##*/}"
      grep -E 'HID_ID|HID_NAME' "$node/device/uevent" || true
    fi
  done
  [[ "$hidraw_found" == "1" ]] || echo "  no hidraw node found for ${vendor}:${product}"
  echo

  echo "Existing adapter udev rules:"
  grep -RniE "${vendor}|${product}|gamecube|gcadapter" /etc/udev/rules.d /lib/udev/rules.d /usr/lib/udev/rules.d 2>/dev/null || true
}

require_root() {
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    exec sudo "$0" "$@"
  fi
}

install_rule() {
  require_root "$@"
  cat > "$rule_path" <<'EOF'
# Allow browser WebHID access to the Wii U / Switch mode GameCube adapter.
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="057e", ATTR{idProduct}=="0337", TAG+="uaccess", MODE="0660"
KERNEL=="hidraw*", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", TAG+="uaccess", MODE="0660"
EOF
  udevadm control --reload-rules
  udevadm trigger
  echo "Installed $rule_path"
  echo "Replug the adapter, then rerun: $0"
}

bind_usbhid() {
  require_root "$@"
  modprobe usbhid || true
  while IFS= read -r iface; do
    [[ -n "$iface" ]] || continue
    if [[ -L "/sys/bus/usb/devices/$iface/driver" ]]; then
      echo "$iface already has a driver"
      continue
    fi
    echo "$iface" > /sys/bus/usb/drivers/usbhid/bind
    echo "Bound $iface to usbhid"
  done < <(find_interfaces)
  udevadm trigger
}

unbind_usbhid() {
  require_root "$@"
  while IFS= read -r iface; do
    [[ -n "$iface" ]] || continue
    driver_path="/sys/bus/usb/devices/$iface/driver"
    if [[ ! -L "$driver_path" ]]; then
      echo "$iface already has no driver"
      continue
    fi
    driver="$(basename "$(readlink -f "$driver_path")")"
    if [[ "$driver" != "usbhid" ]]; then
      echo "$iface is bound to $driver, not usbhid"
      continue
    fi
    echo "$iface" > /sys/bus/usb/drivers/usbhid/unbind
    echo "Unbound $iface from usbhid"
  done < <(find_interfaces)
  udevadm trigger
}

case "${1:-}" in
  --install-udev)
    install_rule "$@"
    ;;
  --bind)
    bind_usbhid "$@"
    ;;
  --unbind)
    unbind_usbhid "$@"
    ;;
  --help|-h)
    cat <<EOF
Usage:
  $0                 Print adapter, driver, and hidraw status.
  $0 --install-udev  Install WebHID-friendly udev rules.
  $0 --bind          Temporarily bind the adapter HID interface to usbhid.
  $0 --unbind        Temporarily unbind the adapter from usbhid for raw USB access.

Webplay's default `make webplay` path uses the local libusb bridge, which needs
raw USB access and usually works best with no kernel driver bound. The older
WebHID experiment requires a /dev/hidraw* node, which needs the usbhid driver.
EOF
    ;;
  "")
    print_status
    ;;
  *)
    echo "unknown argument: $1" >&2
    exit 2
    ;;
esac
