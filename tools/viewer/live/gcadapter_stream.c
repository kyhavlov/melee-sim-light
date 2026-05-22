#include <libusb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VENDOR_ID 0x057e
#define PRODUCT_ID 0x0337
#define TIMEOUT_MS 100
#define REPORT_SIZE 37
#define PORT_COUNT 4
#define PORT_BYTES 9
#define EMIT_INTERVAL_MS 8

typedef struct Controller {
  bool connected;
  uint16_t buttons;
  int main_x;
  int main_y;
  int c_x;
  int c_y;
  int l;
  int r;
} Controller;

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int sig) {
  (void)sig;
  keep_running = 0;
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static bool same_state(const Controller* a, const Controller* b) {
  return memcmp(a, b, sizeof(Controller) * PORT_COUNT) == 0;
}

static uint16_t parse_buttons(uint8_t b1, uint8_t b2) {
  uint16_t buttons = 0;
  if (b1 & 0x01) buttons |= 0x0100;  // A
  if (b1 & 0x02) buttons |= 0x0200;  // B
  if (b1 & 0x04) buttons |= 0x0400;  // X
  if (b1 & 0x08) buttons |= 0x0800;  // Y
  if (b1 & 0x10) buttons |= 0x0001;  // D-left
  if (b1 & 0x20) buttons |= 0x0002;  // D-right
  if (b1 & 0x40) buttons |= 0x0004;  // D-down
  if (b1 & 0x80) buttons |= 0x0008;  // D-up
  if (b2 & 0x01) buttons |= 0x1000;  // Start
  if (b2 & 0x02) buttons |= 0x0010;  // Z
  if (b2 & 0x04) buttons |= 0x0020;  // R
  if (b2 & 0x08) buttons |= 0x0040;  // L
  return buttons;
}

static void parse_report(const uint8_t* data, Controller* out) {
  for (int port = 0; port < PORT_COUNT; ++port) {
    const uint8_t* p = &data[1 + port * PORT_BYTES];
    const bool connected = (p[0] & 0xf0) != 0;
    out[port].connected = connected;
    out[port].buttons = connected ? parse_buttons(p[1], p[2]) : 0;
    out[port].main_x = connected ? p[3] : 128;
    out[port].main_y = connected ? p[4] : 128;
    out[port].c_x = connected ? p[5] : 128;
    out[port].c_y = connected ? p[6] : 128;
    out[port].l = connected ? p[7] : 0;
    out[port].r = connected ? p[8] : 0;
  }
}

static void emit_state(uint64_t seq, const Controller* ports) {
  printf("{\"seq\":%llu,\"ports\":[", (unsigned long long)seq);
  for (int i = 0; i < PORT_COUNT; ++i) {
    const Controller* p = &ports[i];
    printf("%s{\"connected\":%s,\"buttons\":%u,\"mainX\":%d,\"mainY\":%d,"
           "\"cX\":%d,\"cY\":%d,\"l\":%d,\"r\":%d}",
           i == 0 ? "" : ",", p->connected ? "true" : "false", p->buttons, p->main_x,
           p->main_y, p->c_x, p->c_y, p->l, p->r);
  }
  printf("]}\n");
  fflush(stdout);
}

static int find_endpoints(libusb_device_handle* handle, uint8_t* endpoint_in,
                          uint8_t* endpoint_out) {
  libusb_device* device = libusb_get_device(handle);
  struct libusb_config_descriptor* config = NULL;
  int rc = libusb_get_active_config_descriptor(device, &config);
  if (rc != LIBUSB_SUCCESS)
    return rc;

  for (int i = 0; i < config->bNumInterfaces; ++i) {
    const struct libusb_interface* iface = &config->interface[i];
    for (int a = 0; a < iface->num_altsetting; ++a) {
      const struct libusb_interface_descriptor* alt = &iface->altsetting[a];
      for (int e = 0; e < alt->bNumEndpoints; ++e) {
        const struct libusb_endpoint_descriptor* ep = &alt->endpoint[e];
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT)
          continue;
        if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
          *endpoint_in = ep->bEndpointAddress;
        else
          *endpoint_out = ep->bEndpointAddress;
      }
    }
  }

  libusb_free_config_descriptor(config);
  return LIBUSB_SUCCESS;
}

int main(void) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  libusb_context* ctx = NULL;
  int rc = libusb_init(&ctx);
  if (rc != LIBUSB_SUCCESS) {
    fprintf(stderr, "libusb_init failed: %s\n", libusb_error_name(rc));
    return 1;
  }

  libusb_device_handle* handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
  if (!handle) {
    fprintf(stderr, "could not open %04x:%04x\n", VENDOR_ID, PRODUCT_ID);
    libusb_exit(ctx);
    return 1;
  }

  uint8_t endpoint_in = 0;
  uint8_t endpoint_out = 0;
  rc = find_endpoints(handle, &endpoint_in, &endpoint_out);
  if (rc != LIBUSB_SUCCESS || !endpoint_in || !endpoint_out) {
    fprintf(stderr, "failed to discover interrupt endpoints: %s\n", libusb_error_name(rc));
    libusb_close(handle);
    libusb_exit(ctx);
    return 1;
  }

  rc = libusb_kernel_driver_active(handle, 0);
  if (rc == 1)
    (void)libusb_detach_kernel_driver(handle, 0);

  (void)libusb_control_transfer(handle, 0x21, 11, 0x0001, 0, NULL, 0, 1000);

  rc = libusb_claim_interface(handle, 0);
  if (rc != LIBUSB_SUCCESS) {
    fprintf(stderr, "claim interface failed: %s\n", libusb_error_name(rc));
    libusb_close(handle);
    libusb_exit(ctx);
    return 1;
  }

  uint8_t init = 0x13;
  int transferred = 0;
  rc = libusb_interrupt_transfer(handle, endpoint_out, &init, 1, &transferred, TIMEOUT_MS);
  if (rc != LIBUSB_SUCCESS || transferred != 1) {
    fprintf(stderr, "adapter init failed: %s transferred=%d\n", libusb_error_name(rc), transferred);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 1;
  }

  Controller ports[PORT_COUNT] = {0};
  Controller last_ports[PORT_COUNT] = {0};
  long last_emit = 0;
  uint64_t seq = 0;

  while (keep_running) {
    uint8_t data[REPORT_SIZE] = {0};
    transferred = 0;
    rc = libusb_interrupt_transfer(handle, endpoint_in, data, sizeof(data), &transferred,
                                   TIMEOUT_MS);
    if ((rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_OVERFLOW) && transferred == REPORT_SIZE &&
        data[0] == 0x21) {
      parse_report(data, ports);
      const long now = now_ms();
      if (!same_state(ports, last_ports) || now - last_emit >= EMIT_INTERVAL_MS) {
        emit_state(seq++, ports);
        memcpy(last_ports, ports, sizeof(ports));
        last_emit = now;
      }
    } else if (rc != LIBUSB_ERROR_TIMEOUT) {
      fprintf(stderr, "read failed: %s transferred=%d\n", libusb_error_name(rc), transferred);
    }
  }

  libusb_release_interface(handle, 0);
  libusb_close(handle);
  libusb_exit(ctx);
  return 0;
}
