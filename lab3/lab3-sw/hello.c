/*
 * Userspace program that communicates with the vga_ball device driver
 * through ioctls.
 *
 * This version sends ball (x, y) coordinates. The bounce logic is in software.
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "vga_ball.h"

int vga_ball_fd;

static int set_ball_pos(uint16_t x, uint16_t y)
{
  vga_ball_arg_t vla;
  vla.pos.x = x;
  vla.pos.y = y;
  if (ioctl(vga_ball_fd, VGA_BALL_WRITE_POS, &vla)) {
    perror("ioctl(VGA_BALL_WRITE_POS) failed");
    return -1;
  }
  return 0;
}

int main(void)
{
  static const char filename[] = "/dev/vga_ball";
  const int radius = 16;
  const int x_min = radius;
  const int x_max = 639 - radius;
  const int y_min = radius;
  const int y_max = 479 - radius;

  int x = 320;
  int y = 240;
  int vx = 1;
  int vy = 1;

  printf("VGA ball userspace program started\n");

  if ((vga_ball_fd = open(filename, O_RDWR)) == -1) {
    fprintf(stderr, "could not open %s\n", filename);
    return -1;
  }

  while (1) {
    x += vx;
    y += vy;

    if (x <= x_min) {
      x = x_min;
      vx = -vx;
    } else if (x >= x_max) {
      x = x_max;
      vx = -vx;
    }

    if (y <= y_min) {
      y = y_min;
      vy = -vy;
    } else if (y >= y_max) {
      y = y_max;
      vy = -vy;
    }

    if (set_ball_pos((uint16_t)x, (uint16_t)y) != 0)
      break;

    usleep(10000);
  }

  close(vga_ball_fd);
  printf("VGA ball userspace program terminating\n");
  return 0;
}
