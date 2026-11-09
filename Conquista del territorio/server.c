#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAP_SIZE 10
#define MAX_CLIENTS 50
#define UPDATE_INTERVAL 5
#define GAME_DURATION 120

typedef struct {
    int fd;
    char nick[32];
    char pass[32];
    int logged_in;
    int x, y;
    int score;
    char known_walls[MAP_SIZE][MAP_SIZE];
} Player;

char global_map[MAP_SIZE][MAP_SIZE];
int cell_owners[MAP_SIZE][MAP_SIZE]; // Indice del player proprietario, -1 se nessuno
Player players[MAX_CLIENTS];
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int game_active = 1;

void init_game() {
    srand(time(NULL));
    for (int i = 0; i < MAX_CLIENTS; i++) players[i].fd = -1;
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            cell_owners[i][j] = -1;
            // 20% di probabilità di muro
            global_map[i][j] = (rand() % 100 < 20) ? '#' : '.'; 
        }
    }
}

void send_msg(int fd, const char *msg) {
    write(fd, msg, strlen(msg));
}

void send_local_map(int player_idx) {
    char buf[1024] = "LMAP:\n";
    char line[64];
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (i == players[player_idx].x && j == players[player_idx].y) strcat(buf, "P ");
            else if (players[player_idx].known_walls[i][j] == '#') strcat(buf, "# ");
            else if (cell_owners[i][j] == player_idx) strcat(buf, "X ");
            else if (cell_owners[i][j] != -1) strcat(buf, "O ");
            else strcat(buf, ". ");
        }
        strcat(buf, "\n");
    }
    send_msg(players[player_idx].fd, buf);
}

void *broadcast_thread(void *arg) {
    time_t start_time = time(NULL);
    while (game_active) {
        sleep(UPDATE_INTERVAL);
        pthread_mutex_lock(&game_mutex);
        if (time(NULL) - start_time > GAME_DURATION) {
            game_active = 0;
            // Calcola vincitore
            int max_score = -1, winner_idx = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (players[i].fd != -1 && players[i].score > max_score) {
                    max_score = players[i].score;
                    winner_idx = i;
                }
            }
            char buf[128];
            if (winner_idx != -1) sprintf(buf, "END:Vincitore %s con %d celle!\n", players[winner_idx].nick, max_score);
            else strcpy(buf, "END:Nessun vincitore.\n");
            
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (players[i].fd != -1) send_msg(players[i].fd, buf);
            }
            pthread_mutex_unlock(&game_mutex);
            exit(0);
        }

        char buf[2048] = "GMAP:\nPosizioni: ";
        char temp[64];
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].fd != -1 && players[i].logged_in) {
                sprintf(temp, "[%s: %d,%d] ", players[i].nick, players[i].x, players[i].y);
                strcat(buf, temp);
            }
        }
        strcat(buf, "\nMappa Globale Proprieta':\n");
        for (int i = 0; i < MAP_SIZE; i++) {
            for (int j = 0; j < MAP_SIZE; j++) {
                if (cell_owners[i][j] != -1) {
                    sprintf(temp, "%d ", cell_owners[i][j]);
                    strcat(buf, temp);
                } else {
                    strcat(buf, ". ");
                }
            }
            strcat(buf, "\n");
        }
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].fd != -1 && players[i].logged_in) send_msg(players[i].fd, buf);
        }
        pthread_mutex_unlock(&game_mutex);
    }
    return NULL;
}

void process_move(int p_idx, char dir) {
    int nx = players[p_idx].x;
    int ny = players[p_idx].y;
    if (dir == 'U') nx--;
    else if (dir == 'D') nx++;
    else if (dir == 'L') ny--;
    else if (dir == 'R') ny++;

    if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
        if (global_map[nx][ny] == '#') {
            players[p_idx].known_walls[nx][ny] = '#';
            send_msg(players[p_idx].fd, "ACK:Ostacolo scoperto!\n");
        } else {
            players[p_idx].x = nx;
            players[p_idx].y = ny;
            if (cell_owners[nx][ny] != p_idx) {
                if (cell_owners[nx][ny] != -1) players[cell_owners[nx][ny]].score--;
                cell_owners[nx][ny] = p_idx;
                players[p_idx].score++;
            }
            send_msg(players[p_idx].fd, "ACK:Mosso e conquistato!\n");
        }
    } else {
        send_msg(players[p_idx].fd, "ERR:Fuori mappa!\n");
    }
    send_local_map(p_idx);
}

void *client_handler(void *arg) {
    int p_idx = *(int*)arg;
    free(arg);
    char buf[256];
    int fd = players[p_idx].fd;

    while (game_active) {
        int n = read(fd, buf, sizeof(buf)-1);
        if (n <= 0) break;
        buf[n] = '\0';

        pthread_mutex_lock(&game_mutex);
        if (strncmp(buf, "REG:", 4) == 0) {
            sscanf(buf+4, "%31[^:]:%31s", players[p_idx].nick, players[p_idx].pass);
            players[p_idx].logged_in = 1;
            do {
                players[p_idx].x = rand() % MAP_SIZE;
                players[p_idx].y = rand() % MAP_SIZE;
            } while(global_map[players[p_idx].x][players[p_idx].y] == '#');
            memset(players[p_idx].known_walls, '.', sizeof(players[p_idx].known_walls));
            send_msg(fd, "ACK:Login completato.\n");
            send_local_map(p_idx);
        }
        else if (players[p_idx].logged_in) {
            if (strncmp(buf, "MOV:", 4) == 0) process_move(p_idx, buf[4]);
            else if (strncmp(buf, "LIST", 4) == 0) {
                char out[512] = "ACK:Utenti online: ";
                for (int i=0; i<MAX_CLIENTS; i++) 
                    if (players[i].fd != -1 && players[i].logged_in) {
                        strcat(out, players[i].nick); strcat(out, " ");
                    }
                strcat(out, "\n");
                send_msg(fd, out);
            }
            else if (strncmp(buf, "QUIT", 4) == 0) break;
        } else {
            send_msg(fd, "ERR:Devi prima registrarti (REG:nick:pass)\n");
        }
        pthread_mutex_unlock(&game_mutex);
    }

    pthread_mutex_lock(&game_mutex);
    close(players[p_idx].fd);
    players[p_idx].fd = -1;
    pthread_mutex_unlock(&game_mutex);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(1);
    }
    
    init_game();
    
    int server_fd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(atoi(argv[1]));
    
    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind fallita");
        exit(1);
    }
    listen(server_fd, 10);

    pthread_t b_thread;
    pthread_create(&b_thread, NULL, broadcast_thread, NULL);
    pthread_detach(b_thread);

    while (game_active) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (client_fd < 0) continue;

        pthread_mutex_lock(&game_mutex);
        int free_idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].fd == -1) { free_idx = i; break; }
        }
        if (free_idx != -1) {
            players[free_idx].fd = client_fd;
            players[free_idx].logged_in = 0;
            players[free_idx].score = 0;
            int *arg = malloc(sizeof(int));
            *arg = free_idx;
            pthread_t c_thread;
            pthread_create(&c_thread, NULL, client_handler, arg);
            pthread_detach(c_thread);
        } else {
            write(client_fd, "ERR:Server pieno\n", 17);
            close(client_fd);
        }
        pthread_mutex_unlock(&game_mutex);
    }
    return 0;
}