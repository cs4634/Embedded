/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Please Changeto Yourname (pcy2301)
 */
#include "fbputchar.h"
#include "usbkeyboard.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128
#define SCREEN_ROWS 24
#define SCREEN_COLS 64
#define INPUT_ROWS 2
#define INPUT_START_ROW (SCREEN_ROWS - INPUT_ROWS)
#define DIVIDER_ROW (INPUT_START_ROW - 1)
#define RECV_ROWS DIVIDER_ROW
#define INPUT_CAPACITY (SCREEN_COLS * INPUT_ROWS)

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

static pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;
static int running = 1;

static char recv_lines[RECV_ROWS][SCREEN_COLS + 1];
static int recv_row = 0;
static int recv_col = 0;

static char input_buf[INPUT_CAPACITY + 1];
static int input_len = 0;
static int cursor_pos = 0;

static uint8_t prev_keys[6];

static void clear_row(int row)
{
  int col;
  for (col = 0; col < SCREEN_COLS; col++) fbputchar(' ', row, col);
}

static void draw_row_text(int row, const char *text)
{
  int col;
  for (col = 0; col < SCREEN_COLS; col++) {
    char c = text[col];
    if (c == '\0') c = ' ';
    fbputchar(c, row, col);
  }
}

static void clear_screen(void)
{
  int row;
  for (row = 0; row < SCREEN_ROWS; row++) clear_row(row);
}

static void draw_divider(void)
{
  int col;
  for (col = 0; col < SCREEN_COLS; col++) fbputchar('-', DIVIDER_ROW, col);
}

static void init_recv_lines(void)
{
  int row, col;
  for (row = 0; row < RECV_ROWS; row++) {
    for (col = 0; col < SCREEN_COLS; col++) recv_lines[row][col] = ' ';
    recv_lines[row][SCREEN_COLS] = '\0';
  }
  recv_row = 0;
  recv_col = 0;
}

static void redraw_receive_region(void)
{
  int row;
  for (row = 0; row < RECV_ROWS; row++) draw_row_text(row, recv_lines[row]);
}

static void recv_new_line(void)
{
  int row, col;
  if (recv_row < RECV_ROWS - 1) {
    recv_row++;
  } else {
    for (row = 1; row < RECV_ROWS; row++) {
      memcpy(recv_lines[row - 1], recv_lines[row], SCREEN_COLS + 1);
    }
    for (col = 0; col < SCREEN_COLS; col++) recv_lines[RECV_ROWS - 1][col] = ' ';
    recv_lines[RECV_ROWS - 1][SCREEN_COLS] = '\0';
  }
  recv_col = 0;
}

static void append_received_text(const char *msg)
{
  while (*msg) {
    unsigned char c = (unsigned char)*msg++;
    if (c == '\r') continue;
    if (c == '\n') {
      recv_new_line();
      continue;
    }
    if (c < 32 || c > 126) c = '?';
    recv_lines[recv_row][recv_col++] = (char)c;
    if (recv_col >= SCREEN_COLS) recv_new_line();
  }
  redraw_receive_region();
}

static void redraw_input_region(void)
{
  int row, col, idx, cursor_row, cursor_col;
  char line[SCREEN_COLS + 1];

  for (row = 0; row < INPUT_ROWS; row++) {
    for (col = 0; col < SCREEN_COLS; col++) {
      idx = row * SCREEN_COLS + col;
      line[col] = (idx < input_len) ? input_buf[idx] : ' ';
    }
    line[SCREEN_COLS] = '\0';
    draw_row_text(INPUT_START_ROW + row, line);
  }

  if (cursor_pos < INPUT_CAPACITY) {
    cursor_row = INPUT_START_ROW + cursor_pos / SCREEN_COLS;
    cursor_col = cursor_pos % SCREEN_COLS;
    fbputchar('_', cursor_row, cursor_col);
  }
}

static void reset_input(void)
{
  input_buf[0] = '\0';
  input_len = 0;
  cursor_pos = 0;
}

static int key_in_prev(uint8_t key)
{
  int i;
  for (i = 0; i < 6; i++) {
    if (prev_keys[i] == key) return 1;
  }
  return 0;
}

static char keycode_to_ascii(uint8_t keycode, int shift)
{
  if (keycode >= 0x04 && keycode <= 0x1d) {
    char base = shift ? 'A' : 'a';
    return (char)(base + (keycode - 0x04));
  }

  switch (keycode) {
  case 0x1e: return shift ? '!' : '1';
  case 0x1f: return shift ? '@' : '2';
  case 0x20: return shift ? '#' : '3';
  case 0x21: return shift ? '$' : '4';
  case 0x22: return shift ? '%' : '5';
  case 0x23: return shift ? '^' : '6';
  case 0x24: return shift ? '&' : '7';
  case 0x25: return shift ? '*' : '8';
  case 0x26: return shift ? '(' : '9';
  case 0x27: return shift ? ')' : '0';
  case 0x2c: return ' ';
  case 0x2d: return shift ? '_' : '-';
  case 0x2e: return shift ? '+' : '=';
  case 0x2f: return shift ? '{' : '[';
  case 0x30: return shift ? '}' : ']';
  case 0x31: return shift ? '|' : '\\';
  case 0x33: return shift ? ':' : ';';
  case 0x34: return shift ? '"' : '\'';
  case 0x35: return shift ? '~' : '`';
  case 0x36: return shift ? '<' : ',';
  case 0x37: return shift ? '>' : '.';
  case 0x38: return shift ? '?' : '/';
  default: return 0;
  }
}

static void send_current_input(void)
{
  char out[INPUT_CAPACITY + 2];
  int n = input_len;

  if (n <= 0) return;

  memcpy(out, input_buf, n);
  out[n] = '\n';
  out[n + 1] = '\0';

  if (write(sockfd, out, n + 1) < 0) {
    perror("write");
  }

  reset_input();
  redraw_input_region();
}

static void insert_input_char(char c)
{
  if (input_len >= INPUT_CAPACITY) return;
  memmove(input_buf + cursor_pos + 1, input_buf + cursor_pos,
          (size_t)(input_len - cursor_pos + 1));
  input_buf[cursor_pos] = c;
  input_len++;
  cursor_pos++;
  redraw_input_region();
}

static void handle_key_event(uint8_t keycode, int shift)
{
  if (keycode == 0x29) { /* ESC */
    running = 0;
    return;
  }

  if (keycode == 0x4f) { /* Right arrow */
    if (cursor_pos < input_len) cursor_pos++;
    redraw_input_region();
    return;
  }

  if (keycode == 0x50) { /* Left arrow */
    if (cursor_pos > 0) cursor_pos--;
    redraw_input_region();
    return;
  }

  if (keycode == 0x2a) { /* Backspace */
    if (cursor_pos > 0) {
      memmove(input_buf + cursor_pos - 1, input_buf + cursor_pos,
              (size_t)(input_len - cursor_pos + 1));
      input_len--;
      cursor_pos--;
      redraw_input_region();
    }
    return;
  }

  if (keycode == 0x28) { /* Enter */
    send_current_input();
    return;
  }

  {
    char c = keycode_to_ascii(keycode, shift);
    if (c != 0) insert_input_char(c);
  }
}

int main()
{
  int err, i, rc;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  clear_screen();
  draw_divider();
  init_recv_lines();
  reset_input();
  redraw_receive_region();
  redraw_input_region();

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  if (pthread_create(&network_thread, NULL, network_thread_f, NULL) != 0) {
    fprintf(stderr, "Error: pthread_create failed\n");
    exit(1);
  }

  /* Look for and handle keypresses */
  memset(prev_keys, 0, sizeof(prev_keys));
  while (running) {
    rc = libusb_interrupt_transfer(keyboard, endpoint_address,
				   (unsigned char *) &packet, sizeof(packet),
				   &transferred, 0);
    if (rc == 0 && transferred == sizeof(packet) && running) {
      int shift = (packet.modifiers & (USB_LSHIFT | USB_RSHIFT)) != 0;
      for (i = 0; i < 6; i++) {
        uint8_t keycode = packet.keycode[i];
        if (keycode != 0 && !key_in_prev(keycode)) {
          pthread_mutex_lock(&screen_mutex);
          handle_key_event(keycode, shift);
          pthread_mutex_unlock(&screen_mutex);
        }
      }
      memcpy(prev_keys, packet.keycode, sizeof(prev_keys));
    }
  }

  shutdown(sockfd, SHUT_RDWR);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  (void)ignored;
  /* Receive data */
  while (running && (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0) {
    recvBuf[n] = '\0';
    pthread_mutex_lock(&screen_mutex);
    append_received_text(recvBuf);
    pthread_mutex_unlock(&screen_mutex);
  }

  running = 0;
  return NULL;
}
