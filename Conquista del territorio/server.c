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
#define UPDATE_INTERVAL 5
#define GAME_DURATION 120

// Struttura a lista concatenata per numero illimitato di client
typedef struct PlayerNode {
    int fd;
    char nick[32];
    char pass[32];
    int logged_in;
    int x, y;
    int score;
    char known_walls[MAP_SIZE][MAP_SIZE];
    struct PlayerNode *next;
} PlayerNode;

char global_map[MAP_SIZE][MAP_SIZE];
PlayerNode* cell_owners[MAP_SIZE][MAP_SIZE]; // Puntatore al proprietario della cella
PlayerNode* players_head = NULL;

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int game_active = 1;

void init_game() {
    srand(time(NULL));
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            cell_owners[i][j] = NULL;
            // 20% di probabilità di muro
            global_map[i][j] = (rand() % 100 < 20) ? '#' : '.'; 
        }
    }
}

void send_msg(int fd, const char *msg) {
    write(fd, msg, strlen(msg));
}

void send_local_map(PlayerNode* p) {
    char buf[1024] = "LMAP:\n";
    char line[64];
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (i == p->x && j == p->y) strcat(buf, "P ");
            else if (p->known_walls[i][j] == '#') strcat(buf, "# ");
            else if (cell_owners[i][j] == p) strcat(buf, "X ");
            else if (cell_owners[i][j] != NULL) strcat(buf, "O ");
            else strcat(buf, ". ");
        }
        strcat(buf, "\n");
    }
    send_msg(p->fd, buf);
}

void *broadcast_thread(void *arg) {
    time_t start_time = time(NULL);
    while (game_active) {
        sleep(UPDATE_INTERVAL);
        pthread_mutex_lock(&game_mutex);
        
        if (time(NULL) - start_time > GAME_DURATION) {
            game_active = 0;
            // Calcola vincitore
            int max_score = -1;
            PlayerNode* winner = NULL;
            PlayerNode* curr = players_head;
            
            while (curr) {
                if (curr->score > max_score) {
                    max_score = curr->score;
                    winner = curr;
                }
                curr = curr->next;
            }
            
            char buf[128];
            if (winner) sprintf(buf, "END:Vincitore %s con %d celle!\n", winner->nick, max_score);
            else strcpy(buf, "END:Nessun vincitore.\n");
            
            curr = players_head;
            while (curr) {
                if (curr->logged_in) send_msg(curr->fd, buf);
                curr = curr->next;
            }
            pthread_mutex_unlock(&game_mutex);
            exit(0);
        }

        char buf[2048] = "GMAP:\nPosizioni: ";
        char temp[64];
        PlayerNode* curr = players_head;
        while (curr) {
            if (curr->logged_in) {
                sprintf(temp, "[%s: %d,%d] ", curr->nick, curr->x, curr->y);
                strcat(buf, temp);
            }
            curr = curr->next;
        }
        
        strcat(buf, "\nMappa Globale Proprieta':\n");
        for (int i = 0; i < MAP_SIZE; i++) {
            for (int j = 0; j < MAP_SIZE; j++) {
                if (cell_owners[i][j] != NULL) {
                    sprintf(temp, "[%c] ", cell_owners[i][j]->nick[0]);
                    strcat(buf, temp);
                } else {
                    strcat(buf, ". ");
                }
            }
            strcat(buf, "\n");
        }
        
        curr = players_head;
        while (curr) {
            if (curr->logged_in) send_msg(curr->fd, buf);
            curr = curr->next;
        }
        pthread_mutex_unlock(&game_mutex);
    }
    return NULL;
}

void process_move(PlayerNode* p, char dir) {
    int nx = p->x;
    int ny = p->y;
    if (dir == 'U') nx--;
    else if (dir == 'D') nx++;
    else if (dir == 'L') ny--;
    else if (dir == 'R') ny++;

    if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
        if (global_map[nx][ny] == '#') {
            p->known_walls[nx][ny] = '#';
            send_msg(p->fd, "ACK:Ostacolo scoperto!\n");
        } else {
            p->x = nx;
            p->y = ny;
            if (cell_owners[nx][ny] != p) {
                if (cell_owners[nx][ny] != NULL) cell_owners[nx][ny]->score--; // Rimuove punto al vecchio proprietario
                cell_owners[nx][ny] = p; // Ribalta la conquista
                p->score++;
            }
            send_msg(p->fd, "ACK:Mosso e conquistato!\n");
        }
    } else {
        send_msg(p->fd, "ERR:Fuori mappa!\n");
    }
    send_local_map(p);
}

PlayerNode* find_or_create_player(const char* nick, const char* pass, int fd) {
    PlayerNode* curr = players_head;
    while (curr) {
        if (strcmp(curr->nick, nick) == 0 && strcmp(curr->pass, pass) == 0) {
            curr->fd = fd;
            curr->logged_in = 1;
            return curr; // Riconnessione
        }
        curr = curr->next;
    }
    
    // Nuovo utente
    PlayerNode* new_node = malloc(sizeof(PlayerNode));
    new_node->fd = fd;
    strcpy(new_node->nick, nick);
    strcpy(new_node->pass, pass);
    new_node->logged_in = 1;
    new_node->score = 0;
    memset(new_node->known_walls, '.', sizeof(new_node->known_walls));
    
    do {
        new_node->x = rand() % MAP_SIZE;
        new_node->y = rand() % MAP_SIZE;
    } while(global_map[new_node->x][new_node->y] == '#');

    new_node->next = players_head;
    players_head = new_node;
    return new_node;
}

void *client_handler(void *arg) {
    int fd = *(int*)arg;
    free(arg);
    char buf[256];
    PlayerNode* me = NULL;

    while (game_active) {
        ssize_t n = read(fd, buf, sizeof(buf)-1);
        if (n <= 0) break;
        buf[n] = '\0';

        pthread_mutex_lock(&game_mutex);
        if (strncmp(buf, "REG:", 4) == 0) {
            char t_nick[32], t_pass[32];
            if (sscanf(buf+4, "%31[^:]:%31s", t_nick, t_pass) == 2) {
                me = find_or_create_player(t_nick, t_pass, fd);
                send_msg(fd, "ACK:Login completato.\n");
                send_local_map(me);
            }
        }
        else if (me && me->logged_in) {
            if (strncmp(buf, "MOV:", 4) == 0) process_move(me, buf[4]);
            else if (strncmp(buf, "LIST", 4) == 0) {
                char out[512] = "ACK:Utenti online: ";
                PlayerNode* curr = players_head;
                while (curr) {
                    if (curr->logged_in) {
                        strcat(out, curr->nick); strcat(out, " ");
                    }
                    curr = curr->next;
                }
                strcat(out, "\n");
                send_msg(fd, out);
            }
            else if (strncmp(buf, "QUIT", 4) == 0) {
                pthread_mutex_unlock(&game_mutex);
                break; // Uscita sicura dal loop
            }
        } else {
            send_msg(fd, "ERR:Devi prima registrarti (REG:nick:pass)\n");
        }
        pthread_mutex_unlock(&game_mutex);
    }

    // Disconnessione
    pthread_mutex_lock(&game_mutex);
    if (me) me->logged_in = 0; // Mantiene dominio ma risulta offline
    close(fd);
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

    printf("Server avviato sulla porta %s. In attesa di connessioni...\n", argv[1]);

    while (game_active) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (client_fd < 0) continue;

        int *arg = malloc(sizeof(int));
        *arg = client_fd;
        pthread_t c_thread;
        pthread_create(&c_thread, NULL, client_handler, arg);
        pthread_detach(c_thread);
    }
    return 0;
}