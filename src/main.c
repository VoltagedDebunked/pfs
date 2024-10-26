#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Game constants
#define MAX_PLATFORMS 14
#define MAX_COLLECTIBLES 20
#define PLAYER_WIDTH 30
#define PLAYER_HEIGHT 30
#define PLATFORM_HEIGHT 11
#define COLLECTIBLE_SIZE 15
#define GRAVITY 1.0f
#define JUMP_FORCE -12.0f
#define MOVE_SPEED 8.0f
#define DOUBLE_JUMP_FORCE -10.0f
#define MAX_VELOCITY 15.0f

// Colors
#define COLOR_BLACK 0x000000
#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_YELLOW 0xFFFF00
#define COLOR_PURPLE 0xFF00FF
#define COLOR_CYAN 0x00FFFF
#define COLOR_WHITE 0xFFFFFF

// Game states
enum GameState {
    GAME_RUNNING,
    GAME_PAUSED,
    GAME_OVER
};

// Entity structures
typedef struct {
    float x, y;
    float vel_x, vel_y;
    int jumps_remaining;
    int score;
    int is_facing_right;
} Player;

typedef struct {
    int x, y;
    int width;
    int is_moving;
    float move_speed;
    float initial_x;
    float move_range;
} Platform;

typedef struct {
    float x, y;
    int is_active;
    int type;  // 0: coin, 1: power-up
    float animation_offset;
} Collectible;

// Main game structure
typedef struct {
    int fb_fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    unsigned char *fbp;
    unsigned char *buffer;  // Double buffer
    Player player;
    Platform platforms[MAX_PLATFORMS];
    Collectible collectibles[MAX_COLLECTIBLES];
    enum GameState state;
    int frame_count;
    float delta_time;
    struct timespec last_frame;
} Game;

// Function prototypes
void init_game(Game *game);
void cleanup_game(Game *game);
void draw_rect(Game *game, int x, int y, int width, int height, unsigned int color);
void draw_circle(Game *game, int x, int y, int radius, unsigned int color);
void clear_buffer(Game *game);
void swap_buffers(Game *game);
void update_game(Game *game);
void draw_game(Game *game);
int check_collision(float x1, float y1, int w1, int h1, float x2, float y2, int w2, int h2);
char get_input(void);
void spawn_collectible(Game *game, float x, float y);
void update_collectibles(Game *game);
void draw_collectibles(Game *game);
void handle_collectible_collision(Game *game);
void update_platforms(Game *game);
float get_delta_time(Game *game);

int main() {
    Game game;
    struct termios old_term, new_term;

    srand(time(NULL));
    init_game(&game);

    // Set up terminal
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Game loop
    while (game.state != GAME_OVER) {
        game.delta_time = get_delta_time(&game);

        char input = get_input();
        if (input == 'q') break;
        if (input == 'p') {
            game.state = (game.state == GAME_RUNNING) ? GAME_PAUSED : GAME_RUNNING;
            continue;
        }

        if (game.state == GAME_RUNNING) {
            // Handle input
            if (input == ' ' && game.player.jumps_remaining > 0) {
                game.player.vel_y = (game.player.jumps_remaining == 2) ? JUMP_FORCE : DOUBLE_JUMP_FORCE;
                game.player.jumps_remaining--;
            }
            if (input == 'a') {
                game.player.vel_x = -MOVE_SPEED;
                game.player.is_facing_right = 0;
            }
            if (input == 'd') {
                game.player.vel_x = MOVE_SPEED;
                game.player.is_facing_right = 1;
            }
            if (!input) {
                game.player.vel_x *= 0.8f;  // Friction
            }

            update_game(&game);
        }

        draw_game(&game);
        game.frame_count++;
        usleep(16666); // ~60 FPS
    }

    // Cleanup
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    cleanup_game(&game);
    return 0;
}

void init_game(Game *game) {
    // Open framebuffer
    game->fb_fd = open("/dev/fb0", O_RDWR);
    if (game->fb_fd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    // Get screen info
    ioctl(game->fb_fd, FBIOGET_VSCREENINFO, &game->vinfo);
    ioctl(game->fb_fd, FBIOGET_FSCREENINFO, &game->finfo);

    // Allocate double buffer
    long screensize = game->vinfo.xres * game->vinfo.yres * game->vinfo.bits_per_pixel / 8;
    game->buffer = (unsigned char *)malloc(screensize);
    if (!game->buffer) {
        perror("Error allocating double buffer");
        close(game->fb_fd);
        exit(1);
    }

    // Map framebuffer
    game->fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, game->fb_fd, 0);
    if (game->fbp == MAP_FAILED) {
        perror("Error mapping framebuffer");
        free(game->buffer);
        close(game->fb_fd);
        exit(1);
    }

    // Initialize player
    game->player.x = game->vinfo.xres / 2;
    game->player.y = game->vinfo.yres / 2;
    game->player.vel_x = 0;
    game->player.vel_y = 0;
    game->player.jumps_remaining = 2;
    game->player.score = 0;
    game->player.is_facing_right = 1;

    // Initialize platforms with some moving platforms
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        game->platforms[i].width = 80 + rand() % 120;
        game->platforms[i].x = rand() % (game->vinfo.xres - game->platforms[i].width);
        game->platforms[i].y = 100 + (game->vinfo.yres - 200) * i / (MAX_PLATFORMS - 1);
        game->platforms[i].is_moving = (rand() % 3 == 0);  // 33% chance of moving platform
        game->platforms[i].move_speed = (rand() % 100) / 100.0f * 4.0f + 1.0f;
        game->platforms[i].initial_x = game->platforms[i].x;
        game->platforms[i].move_range = 100 + rand() % 100;
    }

    // Initialize collectibles
    for (int i = 0; i < MAX_COLLECTIBLES; i++) {
        game->collectibles[i].is_active = 0;
        if (rand() % 2 == 0) {  // 50% chance to spawn initial collectibles
            spawn_collectible(game,
                            rand() % (game->vinfo.xres - COLLECTIBLE_SIZE),
                            rand() % (game->vinfo.yres - COLLECTIBLE_SIZE));
        }
    }

    game->state = GAME_RUNNING;
    game->frame_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &game->last_frame);
}

void cleanup_game(Game *game) {
    long screensize = game->vinfo.xres * game->vinfo.yres * game->vinfo.bits_per_pixel / 8;
    munmap(game->fbp, screensize);
    free(game->buffer);
    close(game->fb_fd);
}

void draw_rect(Game *game, int x, int y, int width, int height, unsigned int color) {
    for (int i = y; i < y + height && i < game->vinfo.yres; i++) {
        for (int j = x; j < x + width && j < game->vinfo.xres; j++) {
            if (i >= 0 && j >= 0) {
                long location = (j * (game->vinfo.bits_per_pixel / 8)) +
                              (i * game->finfo.line_length);
                *(game->buffer + location) = color & 0xFF;
                *(game->buffer + location + 1) = (color >> 8) & 0xFF;
                *(game->buffer + location + 2) = (color >> 16) & 0xFF;
                *(game->buffer + location + 3) = 0;
            }
        }
    }
}

void draw_circle(Game *game, int x, int y, int radius, unsigned int color) {
    for (int i = -radius; i <= radius; i++) {
        for (int j = -radius; j <= radius; j++) {
            if (i*i + j*j <= radius*radius) {
                int draw_x = x + j;
                int draw_y = y + i;
                if (draw_x >= 0 && draw_x < game->vinfo.xres &&
                    draw_y >= 0 && draw_y < game->vinfo.yres) {
                    long location = (draw_x * (game->vinfo.bits_per_pixel / 8)) +
                                  (draw_y * game->finfo.line_length);
                    *(game->buffer + location) = color & 0xFF;
                    *(game->buffer + location + 1) = (color >> 8) & 0xFF;
                    *(game->buffer + location + 2) = (color >> 16) & 0xFF;
                    *(game->buffer + location + 3) = 0;
                }
            }
        }
    }
}

void clear_buffer(Game *game) {
    memset(game->buffer, 0, game->vinfo.xres * game->vinfo.yres * game->vinfo.bits_per_pixel / 8);
}

void swap_buffers(Game *game) {
    memcpy(game->fbp, game->buffer, game->vinfo.xres * game->vinfo.yres * game->vinfo.bits_per_pixel / 8);
}

void update_game(Game *game) {
    // Update player physics
    game->player.vel_y += GRAVITY;

    // Clamp velocities
    if (game->player.vel_y > MAX_VELOCITY) game->player.vel_y = MAX_VELOCITY;
    if (game->player.vel_y < -MAX_VELOCITY) game->player.vel_y = -MAX_VELOCITY;

    game->player.x += game->player.vel_x;
    game->player.y += game->player.vel_y;

    // Update platforms
    update_platforms(game);

    // Check platform collisions
    int on_platform = 0;
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        if (check_collision(game->player.x, game->player.y, PLAYER_WIDTH, PLAYER_HEIGHT,
                          game->platforms[i].x, game->platforms[i].y,
                          game->platforms[i].width, PLATFORM_HEIGHT)) {
            if (game->player.vel_y > 0) {
                game->player.y = game->platforms[i].y - PLAYER_HEIGHT;
                game->player.vel_y = 0;
                game->player.jumps_remaining = 2;
                on_platform = 1;

                // Move player with platform if standing on it
                if (game->platforms[i].is_moving) {
                    game->player.x += game->platforms[i].move_speed *
                                    (game->platforms[i].x > game->platforms[i].initial_x ? -1 : 1);
                }
            }
        }
    }

    // Update and check collectible collisions
    update_collectibles(game);
    handle_collectible_collision(game);

    // Screen boundaries
    if (game->player.x < 0) {
        game->player.x = 0;
        game->player.vel_x = 0;
    }
    if (game->player.x > game->vinfo.xres - PLAYER_WIDTH) {
        game->player.x = game->vinfo.xres - PLAYER_WIDTH;
        game->player.vel_x = 0;
    }
    if (game->player.y > game->vinfo.yres - PLAYER_HEIGHT) {
        game->player.y = game->vinfo.yres - PLAYER_HEIGHT;
        game->player.vel_y = 0;
        game->player.jumps_remaining = 2;
    }

    // Spawn new collectibles occasionally
    if (rand() % 120 == 0) {  // Roughly every 2 seconds at 60 FPS
        spawn_collectible(game,
                         rand() % (game->vinfo.xres - COLLECTIBLE_SIZE),
                         rand() % (game->vinfo.yres - COLLECTIBLE_SIZE));
    }
}

void update_platforms(Game *game) {
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        if (game->platforms[i].is_moving) {
            float movement = sinf(game->frame_count * 0.05f) * game->platforms[i].move_range;
            game->platforms[i].x = game->platforms[i].initial_x + movement;
        }
    }
}

void update_collectibles(Game *game) {
    for (int i = 0; i < MAX_COLLECTIBLES; i++) {
        if (game->collectibles[i].is_active) {
            // Simple floating animation
            game->collectibles[i].animation_offset = sinf(game->frame_count * 0.1f) * 5.0f;
        }
    }
}

void draw_game(Game *game) {
    clear_buffer(game);

    // Draw platforms
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        unsigned int platform_color = game->platforms[i].is_moving ? COLOR_CYAN : COLOR_GREEN;
        draw_rect(game, game->platforms[i].x, game->platforms[i].y,
                 game->platforms[i].width, PLATFORM_HEIGHT, platform_color);
    }

    // Draw collectibles
    draw_collectibles(game);

    // Draw player with simple animation
    int player_color = game->player.jumps_remaining == 2 ? COLOR_RED : COLOR_PURPLE;
    draw_rect(game, game->player.x, game->player.y, PLAYER_WIDTH, PLAYER_HEIGHT, player_color);

    // Draw player direction indicator (eyes)
    int eye_x = game->player.is_facing_right ?
                game->player.x + PLAYER_WIDTH - 8 : game->player.x + 3;
    draw_rect(game, eye_x, game->player.y + 5, 5, 5, COLOR_WHITE);

    // Draw score
    char score_text[32];
    sprintf(score_text, "Score: %d", game->player.score);
    // Note: In a real implementation, you'd want to add proper text rendering here
    // For now, we'll just draw a score indicator rectangle that grows with score
    draw_rect(game, 10, 10, game->player.score % 100 + 20, 10, COLOR_YELLOW);

    // If game is paused, draw pause indicator
    if (game->state == GAME_PAUSED) {
        draw_rect(game, game->vinfo.xres/2 - 20, game->vinfo.yres/2 - 30, 10, 60, COLOR_WHITE);
        draw_rect(game, game->vinfo.xres/2 + 10, game->vinfo.yres/2 - 30, 10, 60, COLOR_WHITE);
    }

    // Swap buffers to display the frame
    swap_buffers(game);
}

void draw_collectibles(Game *game) {
    for (int i = 0; i < MAX_COLLECTIBLES; i++) {
        if (game->collectibles[i].is_active) {
            float y_offset = game->collectibles[i].animation_offset;
            unsigned int color = game->collectibles[i].type == 0 ? COLOR_YELLOW : COLOR_PURPLE;

            if (game->collectibles[i].type == 0) {
                // Coin (circle)
                draw_circle(game,
                          game->collectibles[i].x + COLLECTIBLE_SIZE/2,
                          game->collectibles[i].y + COLLECTIBLE_SIZE/2 + y_offset,
                          COLLECTIBLE_SIZE/2,
                          color);
            } else {
                // Power-up (diamond shape)
                int center_x = game->collectibles[i].x + COLLECTIBLE_SIZE/2;
                int center_y = game->collectibles[i].y + COLLECTIBLE_SIZE/2 + y_offset;
                for (int j = 0; j < COLLECTIBLE_SIZE; j++) {
                    int width = COLLECTIBLE_SIZE - abs(j - COLLECTIBLE_SIZE/2) * 2;
                    if (width > 0) {
                        draw_rect(game,
                                center_x - width/2,
                                center_y - COLLECTIBLE_SIZE/2 + j,
                                width, 1, color);
                    }
                }
            }
        }
    }
}

void spawn_collectible(Game *game, float x, float y) {
    for (int i = 0; i < MAX_COLLECTIBLES; i++) {
        if (!game->collectibles[i].is_active) {
            game->collectibles[i].x = x;
            game->collectibles[i].y = y;
            game->collectibles[i].is_active = 1;
            game->collectibles[i].type = rand() % 2;  // 0: coin, 1: power-up
            game->collectibles[i].animation_offset = 0;
            break;
        }
    }
}

void handle_collectible_collision(Game *game) {
    for (int i = 0; i < MAX_COLLECTIBLES; i++) {
        if (game->collectibles[i].is_active) {
            if (check_collision(game->player.x, game->player.y,
                              PLAYER_WIDTH, PLAYER_HEIGHT,
                              game->collectibles[i].x, game->collectibles[i].y,
                              COLLECTIBLE_SIZE, COLLECTIBLE_SIZE)) {

                if (game->collectibles[i].type == 0) {
                    // Coin collected
                    game->player.score += 10;
                } else {
                    // Power-up collected
                    game->player.score += 25;
                    game->player.jumps_remaining = 2;  // Refresh double jump
                    game->player.vel_y *= 0.5f;  // Slow fall
                }

                game->collectibles[i].is_active = 0;
            }
        }
    }
}

float get_delta_time(Game *game) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    float delta = (current_time.tv_sec - game->last_frame.tv_sec) +
                 (current_time.tv_nsec - game->last_frame.tv_nsec) / 1e9;

    game->last_frame = current_time;
    return delta;
}

int check_collision(float x1, float y1, int w1, int h1, float x2, float y2, int w2, int h2) {
    return x1 < x2 + w2 &&
           x1 + w1 > x2 &&
           y1 < y2 + h2 &&
           y1 + h1 > y2;
}

char get_input(void) {
    char buf[1];
    int n = read(STDIN_FILENO, buf, 1);
    return (n > 0) ? buf[0] : 0;
}