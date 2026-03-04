/*
 * CSEE 4840 Lab 2
 * Name/UNI: Please Change to Yourname (pcy2301)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000
#define BUFFER_SIZE 128

/* Screen dimensions */
#define MAX_COLS 64
#define MAX_ROWS 24
#define DIVIDER_ROW 21
#define INPUT_START_ROW 22

int sockfd;
struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
pthread_mutex_t fb_lock = PTHREAD_MUTEX_INITIALIZER;

void *network_thread_f(void *);

/* ==== UI & Buffer State ==== */
char input_buf[BUFFER_SIZE];
int input_len = 0;
int cursor_pos = 0;
int msg_row = 0;
int msg_col = 0;

/* External functions from fbputchar.c */
extern void fb_clear();
extern void fb_scroll_up(int start_row, int end_row);
extern void fb_clear_row(int row);

/* ==== USB HID Keymap ==== */
const char keymap_nosigned[128] = {
    0,0,0,0, 'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0', '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/', 0
};
const char keymap_shifted[128] = {
    0,0,0,0, 'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')', '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '\"', '~', '<', '>', '?', 0
};

/* ==== Helper Drawing Functions ==== */
void update_input_area() {
    pthread_mutex_lock(&fb_lock);
    
    fb_clear_row(INPUT_START_ROW);
    fb_clear_row(INPUT_START_ROW + 1);

    for (int i = 0; i < input_len; i++) {
        int r = INPUT_START_ROW + (i / MAX_COLS);
        int c = i % MAX_COLS;
        if (r < MAX_ROWS) {
            fbputchar(input_buf[i], r, c);
        }
    }

    int cursor_r = INPUT_START_ROW + (cursor_pos / MAX_COLS);
    int cursor_c = cursor_pos % MAX_COLS;
    if (cursor_r < MAX_ROWS) {
        fbputchar('_', cursor_r, cursor_c);
    }
    
    pthread_mutex_unlock(&fb_lock);
}

void print_msg_char(char c) {
    if (c == '\n') {
        msg_col = 0;
        msg_row++;
    } else {
        fbputchar(c, msg_row, msg_col);
        msg_col++;
        if (msg_col >= MAX_COLS) {
            msg_col = 0;
            msg_row++;
        }
    }

    if (msg_row >= DIVIDER_ROW) {
        fb_scroll_up(0, DIVIDER_ROW - 1);
        msg_row = DIVIDER_ROW - 1;
        msg_col = 0;
    }
}

/* ==== Main Function ==== */
int main() {
    int err, col;
    struct sockaddr_in serv_addr;
    struct usb_keyboard_packet packet;
    int transferred;
    
    uint8_t last_keys[6] = {0}; 

    if ((err = fbopen()) != 0) {
        fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
        exit(1);
    }

    fb_clear();
    for (col = 0 ; col < MAX_COLS ; col++) {
        fbputchar('-', DIVIDER_ROW, col);
    }
    update_input_area();

    if ((keyboard = openkeyboard(&endpoint_address)) == NULL) {
        fprintf(stderr, "Did not find a keyboard\n");
        exit(1);
    }
    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Error: Could not create socket\n");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error: connect() failed. Is the server running?\n");
        exit(1);
    }

    pthread_create(&network_thread, NULL, network_thread_f, NULL);

    for (;;) {
        libusb_interrupt_transfer(keyboard, endpoint_address,
                                  (unsigned char *) &packet, sizeof(packet),
                                  &transferred, 0);
        
        if (transferred == sizeof(packet)) {
            if (packet.keycode[0] == 0x29) break;

            int shift_pressed = (packet.modifiers & 0x02) || (packet.modifiers & 0x20);

            for (int i = 0; i < 6; i++) {
                uint8_t key = packet.keycode[i];
                if (key == 0) continue;

                int is_new = 1;
                for (int j = 0; j < 6; j++) {
                    if (key == last_keys[j]) { is_new = 0; break; }
                }

                if (is_new) {
                    if (key == 0x2a) { 
                        if (cursor_pos > 0) {
                            for (int k = cursor_pos - 1; k < input_len - 1; k++) {
                                input_buf[k] = input_buf[k+1];
                            }
                            input_len--;
                            cursor_pos--;
                            update_input_area();
                        }
                    } else if (key == 0x50) { 
                        if (cursor_pos > 0) { cursor_pos--; update_input_area(); }
                    } else if (key == 0x4f) { 
                        if (cursor_pos < input_len) { cursor_pos++; update_input_area(); }
                    } else if (key == 0x28) { 
                        if (input_len > 0) {
                            input_buf[input_len] = '\n';
                            write(sockfd, input_buf, input_len + 1);
                            
                            input_len = 0;
                            cursor_pos = 0;
                            update_input_area();
                        }
                    } else if (key < 128) { 
                        char ascii_char = shift_pressed ? keymap_shifted[key] : keymap_nosigned[key];
                        if (ascii_char != 0 && input_len < BUFFER_SIZE - 2) {
                            for (int k = input_len; k > cursor_pos; k--) {
                                input_buf[k] = input_buf[k-1];
                            }
                            input_buf[cursor_pos] = ascii_char;
                            input_len++;
                            cursor_pos++;
                            update_input_area();
                        }
                    }
                }
            }
            
            for (int i = 0; i < 6; i++) {
                last_keys[i] = packet.keycode[i];
            }
        }
    }

    pthread_cancel(network_thread);
    pthread_join(network_thread, NULL);
    return 0;
}

void *network_thread_f(void *ignored) {
    char recvBuf[BUFFER_SIZE];
    int n;
    
    while ((n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0) {
        recvBuf[n] = '\0';
        
        pthread_mutex_lock(&fb_lock);
        for (int i = 0; i < n; i++) {
            print_msg_char(recvBuf[i]);
        }
        pthread_mutex_unlock(&fb_lock);
    }
    return NULL;
}
