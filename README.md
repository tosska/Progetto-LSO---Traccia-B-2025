# 🏰 Conquista del Territorio - Progetto LSO 2025

Progetto per l'esame di **Laboratorio di Sistemi Operativi** (A.A. 2025-2026), sviluppato in linguaggio C su piattaforma UNIX/Linux.

## 📖 Descrizione del Progetto (Traccia B)
Il progetto implementa un sistema Client-Server basato su socket TCP per un gioco multiplayer di conquista del territorio. I giocatori si muovono su una griglia, conquistando le celle libere al loro passaggio. Gli ostacoli (muri) sono nascosti dalla *Fog of War* e vengono rivelati individualmente solo quando un giocatore vi si scontra. La partita ha una durata prefissata (timeout globale), al termine della quale vince chi possiede più celle.

---

## 🛠️ Architettura e Dettagli Implementativi

L'architettura di rete è di tipo **Client-Server** con comunicazione su socket TCP (dominio `PF_INET`, stile `SOCK_STREAM`).

### Il Server
Il server gestisce molteplici connessioni in modo concorrente e senza limiti a priori sul numero di utenti:
* **Multithreading e Concorrenza:** Modello a thread (libreria POSIX `pthread`). Il thread principale accetta le connessioni tramite `accept()`, generando per ciascuna un thread `client_handler` distaccato (`pthread_detach`), garantendo il rilascio automatico delle risorse alla chiusura.
* **Scalabilità Illimitata (Liste Concatenate):** Lo stato degli utenti non è gestito tramite array statici pre-allocati, bensì tramite una struttura dati dinamica (`PlayerNode`). Ciò consente un numero illimitato di registrazioni e permette ai giocatori disconnessi di mantenere il dominio territoriale o di riprendere la sessione in caso di riconnessione.
* **Asincronicità del Broadcast:** Un thread demone isolato (`broadcast_thread`) si risveglia periodicamente ogni $T$ secondi per inviare gli aggiornamenti di stato globale (`GMAP`) a tutti i client attivi e monitorare il timeout globale della partita.
* **Sincronizzazione e Sicurezza:** L'accesso alle risorse condivise (mappa e struttura dinamica dei giocatori) è protetto da un Mutex POSIX (`pthread_mutex_t game_mutex`). È stata implementata una gestione rigorosa del rilascio del lock (es. prima del `break` in caso di `QUIT`) per prevenire qualsiasi situazione di *deadlock*.
* **Fog of War:** Il server mantiene la "Source of Truth" della mappa. Ogni movimento valuta le collisioni: se la destinazione è un muro, la posizione reale non cambia ma viene aggiornata la matrice privata `known_walls` del singolo utente.

### Il Client
Il client adotta un design a due thread per assicurare la massima reattività:
* Un thread principale gestisce l'acquisizione non bloccante dei comandi da tastiera (`stdin`) e l'invio al socket tramite `write()`.
* Un secondo thread (`receive_thread`) rimane in ascolto asincrono sulla `read()` del socket per stampare in tempo reale le notifiche di gioco (`LMAP`, `GMAP`, `ACK`, `ERR`).

---

## 🗣️ Protocollo Applicativo (L7)
La comunicazione avviene tramite stringhe testuali in chiaro (Human-Readable) delimitate da caratteri di newline (`\n`).

### Messaggi `Client -> Server`
* `REG:<nickname>:<password>\n` : Registrazione o autenticazione dell'utente.
* `MOV:<direzione>\n` : Spostamento del giocatore (`U` = Su, `D` = Giù, `L` = Sinistra, `R` = Destra).
* `LIST\n` : Richiede l'elenco degli utenti correntemente online.
* `QUIT\n` : Segnala la disconnessione volontaria e chiude il socket.

### Messaggi `Server -> Client`
* `ACK:<messaggio>\n` : Conferma di esecuzione positiva.
* `ERR:<messaggio>\n` : Notifica di errore o azione non valida.
* `LMAP:\n<mappa>\n` : Mappa locale inviata in unicast dopo ogni mossa (`P` = Player, `#` = Muro noto, `X` = Propria cella, `O` = Cella nemica, `.` = Libero).
* `GMAP:\n<dati>\n` : Aggiornamento periodico in broadcast contenente posizioni e mappa globale delle proprietà.
* `END:<messaggio>\n` : Termine della partita per scadenza del timeout, seguito dalla chiusura delle connessioni.

---

## 🚀 Guida all'Uso ed Esecuzione

### Metodo 1: Compilazione ed Esecuzione Nativa (Senza Docker)
1. Compilare i sorgenti con il compilatore GCC:
   ```bash
   gcc -o server server.c -lpthread
   gcc -o client client.c -lpthread
   ```
2. Avviare il Server specificando la porta (il server non produce alcun output su standard output come da specifiche):
   ```bash
   ./server 8080
   ```
3. Da un altro terminale, avviare il Client:
   ```bash
   ./client 127.0.0.1 8080
   ```

### Metodo 2: Esecuzione tramite Docker Compose 🐳
Il progetto include una configurazione completa per l'orchestrazione tramite container.
1. Avviare i servizi in background (il server verrà compilato ed eseguito automaticamente in un container Ubuntu isolato):
   ```bash
   docker-compose up -d server client
   ```
2. Aprire una sessione interattiva sul container client per giocare:
   ```bash
   docker-compose exec client ./client server 8080
   ```
3. Per terminare l'infrastruttura:
   ```bash
   docker-compose down
   ```

---

## 👥 Componenti del Gruppo
* **Lorenzo Trignano** - Matricola: N86004496