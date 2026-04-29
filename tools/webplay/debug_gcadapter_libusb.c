#include <libusb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define VENDOR_ID 0x057e
#define PRODUCT_ID 0x0337
#define TIMEOUT_MS 100
#define REPORT_SIZE 37

static volatile sig_atomic_t keep_running = 1;

static void on_sigint(int sig) {
  (void)sig;
  keep_running = 0;
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void dump_hex(const uint8_t* data, int size) {
  for (int i = 0; i < size; ++i) {
    printf("%02x%s", data[i], i + 1 == size ? "" : " ");
  }
}

int main(void) {
  signal(SIGINT, on_sigint);

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

  libusb_device* device = libusb_get_device(handle);
  struct libusb_config_descriptor* config = NULL;
  rc = libusb_get_active_config_descriptor(device, &config);
  if (rc != LIBUSB_SUCCESS) {
    fprintf(stderr, "libusb_get_active_config_descriptor failed: %s\n", libusb_error_name(rc));
    libusb_close(handle);
    libusb_exit(ctx);
    return 1;
  }

  uint8_t endpoint_in = 0;
  uint8_t endpoint_out = 0;
  for (int i = 0; i < config->bNumInterfaces; ++i) {
    const struct libusb_interface* iface = &config->interface[i];
    for (int a = 0; a < iface->num_altsetting; ++a) {
      const struct libusb_interface_descriptor* alt = &iface->altsetting[a];
      for (int e = 0; e < alt->bNumEndpoints; ++e) {
        const struct libusb_endpoint_descriptor* ep = &alt->endpoint[e];
        if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_INTERRUPT)
          continue;
        if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
          endpoint_in = ep->bEndpointAddress;
        else
          endpoint_out = ep->bEndpointAddress;
      }
    }
  }
  libusb_free_config_descriptor(config);

  printf("endpoints: in=0x%02x out=0x%02x\n", endpoint_in, endpoint_out);
  if (!endpoint_in || !endpoint_out) {
    fprintf(stderr, "missing interrupt endpoints\n");
    libusb_close(handle);
    libusb_exit(ctx);
    return 1;
  }

  rc = libusb_kernel_driver_active(handle, 0);
  if (rc == 1) {
    rc = libusb_detach_kernel_driver(handle, 0);
    printf("detach kernel driver: %s\n", libusb_error_name(rc));
  } else {
    printf("kernel driver active: %d\n", rc);
  }

  rc = libusb_control_transfer(handle, 0x21, 11, 0x0001, 0, NULL, 0, 1000);
  printf("control transfer 0x21/11: %s (%d)\n", rc < 0 ? libusb_error_name(rc) : "OK", rc);

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
  printf("init interrupt write: %s transferred=%d\n", libusb_error_name(rc), transferred);

  printf("Move sticks and press buttons for 12 seconds.\n");
  long start = now_ms();
  int reads = 0;
  uint8_t last[64] = {0};
  int last_size = 0;
  while (keep_running && now_ms() - start < 12000) {
    uint8_t data[REPORT_SIZE] = {0};
    transferred = 0;
    rc = libusb_interrupt_transfer(handle, endpoint_in, data, sizeof(data), &transferred, TIMEOUT_MS);
    if ((rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_OVERFLOW) && transferred > 0) {
      bool changed = transferred != last_size;
      for (int i = 0; i < transferred && !changed; ++i)
        changed = data[i] != last[i];
      if (changed || reads < 5) {
        printf("%04d %d bytes%s: ", reads, transferred,
               rc == LIBUSB_ERROR_OVERFLOW ? " overflow" : "");
        dump_hex(data, transferred);
        printf("\n");
      }
      for (int i = 0; i < transferred; ++i)
        last[i] = data[i];
      last_size = transferred;
      reads += 1;
    } else if (rc != LIBUSB_ERROR_TIMEOUT) {
      printf("read failed: %s transferred=%d\n", libusb_error_name(rc), transferred);
    }
  }

  printf("done, reports read: %d\n", reads);
  libusb_release_interface(handle, 0);
  libusb_close(handle);
  libusb_exit(ctx);
  return 0;
}
