#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int sock_fd;
int client_active = 1;

void *receive_thread(void *arg) {
    char buf[2048];
    while (client_active) {
        ssize_t n = read(sock_fd, buf, sizeof(buf)-1);
        if (n <= 0) {
            printf("\nDisconnesso dal server.\n");
            client_active = 0;
            exit(0);
        }
        buf[n] = '\0';
        printf("\n%s\n> ", buf);
        fflush(stdout);
        if (strncmp(buf, "END:", 4) == 0) exit(0);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Uso: %s <IP_Server> <Porta>\n", argv[0]);
        exit(1);
    }

    struct hostent *host = gethostbyname(argv[1]);
    if (!host) {
        perror("Errore risoluzione nome");
        exit(1);
    }

    sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    memcpy(&serv_addr.sin_addr.s_addr, host->h_addr, host->h_length);

    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connessione fallita");
        exit(1);
    }

    pthread_t r_thread;
    pthread_create(&r_thread, NULL, receive_thread, NULL);

    printf("Connessione stabilita!\nComandi: REG:nick:pass | MOV:U/D/L/R | LIST | QUIT\n");
    char input[256];
    
    while (client_active) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        
        // Rimuove l'accapo dalla stringa letta da tastiera
        input[strcspn(input, "\n")] = 0; 
        
        if (strlen(input) > 0) {
            write(sock_fd, input, strlen(input));
            if (strncmp(input, "QUIT", 4) == 0) break;
            sleep(1); // Pausa per sincronizzare l'interfaccia 
        }
    }

    client_active = 0;
    close(sock_fd);
    return 0;
}