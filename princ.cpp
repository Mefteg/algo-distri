#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

using namespace std;

//on définit un site par son port et sa socket
struct Site {
    int port;
    int socket;
};

//on definit un message par la chaine qu'il contient
struct message {
    string str;
    int i;
};

////////////////////////
// VARIABLES GLOBALES //
////////////////////////

//Nombre de sites
const int n=5;
//Commencer l'inclusion dans le tableau des voisins par cette valeur
const int port=1988;
//Le numéro de port du site
int mon_port;
//taille maximale des messages
const int MAX_SIZE=2048;
struct timeval tv;
fd_set readdfs;
//adresse du site
struct sockaddr_in site;
//adresse des voisins
struct sockaddr_in neighbors;
//Tableau contenant les sockets des sites voisins
int voisins[port+n];
//Message partagé par les threads
struct message m;
//La socket du programme
int ma_sock;
//Indique si le site doit s'arrêter ou continuer
int continuer=1;
//Pointeur sur le dernier détenteur probable/connu du jeton
int last=-1;
//Pointeur sur le site à qui je dois transmettre le jeton 
int next=-1;
//mutex pour le traitement des messages
pthread_mutex_t verrouMsg = PTHREAD_MUTEX_INITIALIZER;
//mutex pour l'ajout des sites
pthread_mutex_t verrouSite = PTHREAD_MUTEX_INITIALIZER;

///////////////
// FONCTIONS //
///////////////

int connexion( char * addr, int port ) {
    int sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( sock < 0 )
        cerr << "Impossible de créer la socket";

    //on définie la structure d'échange
    site.sin_family = AF_INET;
    site.sin_addr.s_addr = htonl(INADDR_ANY);
    site.sin_port = htons(port);

    //on bind la socket
    int erreur = bind( sock, (struct sockaddr *) &site, sizeof(site));
    if ( erreur < 0 )
        cerr << "xx Impossible de binder la socket" << endl;

    //on ecoute les connexions
    listen( sock, 5 );

    return sock;
}

int envoyer( int voisin, char * message ) {
    int taille=4;
    char m[4];
    strcpy( m, (char *) "aaaa" );
    write( voisin, m, taille );
}

//traite le message et débloque son accès
void traiterMessage() {
}


//attend le message d'un site ( fonction threadée )
void * attendreMessage( void * s ) {
    struct Site * site = (struct Site *) s;
	struct Site tmp;
	tmp.port = site->port;
	tmp.socket = site->socket;
	pthread_mutex_unlock( &verrouSite );
    cout << "-- -- J'attends des messages du site: " << tmp.port << endl;

    int continuer=1;
    //tant que la socket du site est opérationnelle
    while ( continuer > 0 ) {
        char mess[MAX_SIZE];
        strcpy( mess, "" );
        continuer = read( tmp.socket, mess, MAX_SIZE );
		//on bloque l'accès au message global
		pthread_mutex_lock( &verrouMsg );
        cout << "-- -- Message de " << tmp.port << ": " << mess << endl;
        m.str = mess;
        m.i = tmp.port;
		traiterMessage();
		//on débloque le verrou
		pthread_mutex_unlock( &verrouMsg );
    }

    cout << "J'ai fini d'attendre des messages du site: " << tmp.port << endl;
    //on enleve la socket comme ça le site peut se reconnecter
    voisins[tmp.port] = -1;

    return NULL;
}

//Thread pour accepter les sites voisins
void * accepterVoisins( void * s ) {
    while ( continuer ) {
        socklen_t t = sizeof( sockaddr_in );
        int res = accept( ma_sock, (struct sockaddr *) &neighbors, &t );
        //si l'acceptation a été faite
        if ( res >= 0 ) {
            string m( "aaaa" );
            //je lis le port du site
            read( res, (char *) m.c_str(), 4);
            //on recupere le port du site
            int pr=atoi(m.c_str());
            //si je ne me suis pas encore connecté avec ce site
            if ( voisins[pr] < 0 ) {
                voisins[pr] = res;
                cout << "-- Acceptation du site: " << pr << endl;
				pthread_mutex_lock( &verrouSite );
                struct Site s;
                s.port = pr;
                s.socket = voisins[pr];
                pthread_t Id;
                pthread_create(&Id, NULL, attendreMessage, (void *) &s);
            }
            //sinon
            else {
                cout << "-- Pas besoin de refaire une ACCEPTATION avec: " << pr << endl;
            }
        }
        //sinon
        else {
            cout << "xx Impossible d'accepter le client." << endl;
        }
    }

    return NULL;
}

//Thread pour se connecter aux sites voisins
void * connecterVoisins( void * s ) {
    //on se connecte aux n voisins consecutifs du port 1988
    for ( int cpt=0; cpt<n; cpt++ ) {
        //si je ne suis pas deja connecté à ce site et que je ne suis pas le site courant
        if ( voisins[port+cpt] < 0 && port+cpt != mon_port ) {
            voisins[port+cpt] = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
            neighbors.sin_family = AF_INET;
            neighbors.sin_port = htons( port+cpt );
            inet_pton( AF_INET, "127.0.0.1", &neighbors.sin_addr );
            int res = connect( voisins[port+cpt], (struct sockaddr *) &neighbors, sizeof( sockaddr_in ) );
            //si la connexion s'est bien faite
            if ( res < 0 ) {
                cout << "xx Impossible de se connecter à ce voisin: " << port+cpt << endl;
                voisins[port+cpt] = -1;
            }
            //sinon
            else {
                cout << "-- J'ai réussi à me connecter à ce voisin: " << port+cpt << endl;
                string m;
                stringstream p;
                p << mon_port;
                m = p.str();
                write( voisins[port+cpt], (char *) m.c_str(), 4 );
				pthread_mutex_lock( &verrouSite );
                struct Site s;
                s.port = port+cpt;
                s.socket = voisins[port+cpt];
                //on lance un thread qui écoutera les messages de ce site
                pthread_t Id;
                pthread_create(&Id, NULL, attendreMessage, (void *) &s);

                //on lui envoie un petit message
                m.clear();
                m.assign( "Hey Hey Hey! Salut!!" );
                write( voisins[port+cpt], (char *) m.c_str(), MAX_SIZE );
                cout << "J'ai envoyé à " << s.port << " : " << m << endl;
            }
        }
        else {
            cout << "-- Pas besoin de refaire une CONNEXION avec: " << port+cpt << endl;
        }
    }

    return NULL;
}

////////////////
// PRINCIPALE //
////////////////

int main ( int argc, char ** argv )
{
    cout << "-- Fonction principale" << endl;

    //on indique qu'il n'y a pas de voisins pour l'instant
    for ( int i=0; i<port+n; i++ ) voisins[i] = -1;

    m.str = "";
    m.i = -1;

    //port par défaut
    mon_port=1988;
    //si un port est passé en argument
    if( argv[1] != NULL ) {
        //c'est celui-ci qu'on prend
        mon_port = atoi(argv[1]);
    }
    cout << "-- Paramètre du port: " << mon_port << endl;

    cout << "-- On lance la connexion." << endl;
    ma_sock = connexion( (char *) "127.0.0.1", mon_port );

    //thread d'acception de connexion
    pthread_t IdAccept;
    pthread_create(&IdAccept, NULL, accepterVoisins, (void *) NULL);
    //thread de demande de connexion
    pthread_t IdConnect;
    pthread_create(&IdConnect, NULL, connecterVoisins, (void *) NULL);

    //tant que le site est actif
    while ( continuer == 1 ) {
        //Taper 1 pour continuer, autre chose sinon ( genre 0 )
        cin >> continuer;
    }

    //à faire
    //envoyer un message à tous mes voisins pour leur dire que
    //je me ferme

	//destruction du verrou
    cout << "-- On detruit le verrou." << endl;
	pthread_mutex_destroy( &verrouMsg );

	//on ferme la socket
    cout << "-- On ferme la socket." << endl;
    shutdown( ma_sock, SHUT_RDWR );
    close( ma_sock );

    return EXIT_SUCCESS;
}
