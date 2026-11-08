# 🏰 Conquista del Territorio - Progetto LSO 2025

Progetto per l'esame di **Laboratorio di Sistemi Operativi** (A.A. 2024-2025), sviluppato in linguaggio C su piattaforma UNIX.

## 📖 Descrizione del Progetto (Traccia B)
Il progetto implementa un sistema Client-Server basato su socket TCP per un gioco multiplayer di conquista del territorio. I giocatori si muovono su una griglia, conquistando le celle libere al loro passaggio. Gli ostacoli (muri) sono nascosti dalla *Fog of War* e vengono rivelati individualmente solo quando un giocatore vi si scontra. La partita ha una durata prefissata (timeout globale), al termine della quale vince chi possiede più celle.

---

## 🛠️ Architettura e Dettagli Implementativi

L'architettura di rete è di tipo **Client-Server** con comunicazione su socket TCP (dominio `PF_INET`, stile `SOCK_STREAM`).

### Il Server
Il server gestisce molteplici connessioni in modo concorrente senza bloccare l'esecuzione.
* **Multithreading:** È stato utilizzato il modello a thread (libreria `pthread`). Il *Main Thread* esegue un loop sulla `accept()`. Per ogni nuovo client, viene generato un thread `client_handler` distaccato (`pthread_detach`) dedicato alla ricezione dei comandi tramite `read()`.
* **Asincronicità del Broadcast:** Per soddisfare il requisito di invio periodico della mappa globale senza bloccare la ricezione dei comandi, è stato implementato un thread demone (`broadcast_thread`). Questo thread si risveglia ogni $T$ secondi (`sleep()`), calcola lo stato del gioco, verifica il timeout e invia la `GMAP` a tutti i socket attivi.
* **Sincronizzazione:** La mappa globale (`global_map`), l'array degli utenti (`players`) e la matrice delle proprietà (`cell_owners`) sono risorse condivise. L'accesso a queste strutture è regolato da un Mutex POSIX (`pthread_mutex_t game_mutex`) per evitare *race conditions* (es. due giocatori che conquistano la stessa cella nel medesimo istante).
* **Fog of War:** Il server mantiene la "Source of Truth" della mappa. Ogni volta che il server riceve un comando `MOV`, verifica se la destinazione è un muro. In caso positivo, il muro viene aggiunto alla matrice privata `known_walls` del giocatore, senza rivelarlo agli altri.

### Il Client
Il client è anch'esso multithread per garantire reattività:
* Un thread principale acquisisce l'input dell'utente da `stdin` (in modo non bloccante per l'interfaccia di rete) e lo invia al server tramite `write()`.
* Un secondo thread (`receive_thread`) resta in attesa sulla `read()` del socket, pronto a stampare i messaggi asincroni (come gli aggiornamenti `GMAP` o gli esiti `ACK`/`ERR`) non appena arrivano.

---

## 🗣️ Protocollo Applicativo
La comunicazione avviene tramite stringhe testuali (ASCII) per garantire robustezza e facilità di debug.

### Messaggi `Client -> Server`
* `REG:<nickname>:<password>` : Registrazione e ingresso in mappa (credenziali limitate a 31 caratteri).
* `MOV:<direzione>` : Spostamento del giocatore. Direzioni valide: `U` (Su), `D` (Giù), `L` (Sinistra), `R` (Destra).
* `LIST` : Richiede la lista degli utenti attualmente connessi.
* `QUIT` : Disconnessione e chiusura del socket.

### Messaggi `Server -> Client`
* `ACK:<messaggio>` : Conferma di un'azione (es. *Mosso e conquistato!*, *Ostacolo scoperto!*).
* `ERR:<messaggio>` : Errore o azione non valida (es. *Fuori mappa!*, *Server pieno*).
* `LMAP:<griglia>` : Inviata in risposta a un comando `MOV`. Contiene la visione locale del giocatore (`P` = Player, `#` = Muro noto, `X` = Propria cella, `O` = Cella nemica, `.` = Libero/Ignoto).
* `GMAP:<dati>` : Broadcast asincrono inviato periodicamente a tutti i client, contenente le posizioni attuali in chiaro e la mappa globale delle proprietà.
* `END:<messaggio>` : Inviato a fine partita (allo scadere del timer). Contiene l'esito della partita (vincitore) e impone la chiusura della connessione.

---

## 🚀 Guida all'Uso ed Esecuzione

### Requisiti
* Ambiente UNIX/Linux
* Compilatore `gcc` e `make`
* (Opzionale ma consigliato) `docker` e `docker-compose`

### Metodo 1: Compilazione ed Esecuzione Standard
1. Compilare i sorgenti tramite il `Makefile` incluso:
   ```bash
   make
   ```
2. Avviare il Server (indicando la porta, es. 8080). *Nota: Il server non produrrà output a schermo, come da specifiche.*
   ```bash
   ./server 8080
   ```
3. Su altri terminali (o macchine fisiche diverse), avviare uno o più Client:
   ```bash
   ./client 127.0.0.1 8080
   ```

### Metodo 2: Esecuzione del Server tramite Docker 🐳
Per isolare l'ambiente di esecuzione del server e garantire la riproducibilità, è possibile utilizzare Docker.
1. Creare e avviare il container del server in background:
   ```bash
   docker-compose up -d --build
   ```
2. Avviare i client nativamente (o da altri container) puntando all'indirizzo del container:
   ```bash
   make client
   ./client 127.0.0.1 8080
   ```
3. Per fermare il server e rimuovere il container:
   ```bash
   docker-compose down
   ```

### Gameplay Rapido
Una volta avviato il client, digita:
1. `REG:MioNome:MiaPass` per registrarti.
2. `MOV:R` per muoverti a destra (o `U`, `D`, `L`).
3. `LIST` per vedere chi sta giocando con te.

---

## 👥 Componenti del Gruppo
* **Nome Cognome** - Matricola: 0123456789
* **Nome Cognome** - Matricola: 0123456789
