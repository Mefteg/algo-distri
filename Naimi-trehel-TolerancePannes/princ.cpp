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
//mutex pour le traitement des messages
pthread_mutex_t verrouMsg = PTHREAD_MUTEX_INITIALIZER;
//mutex pour l'ajout des sites
pthread_mutex_t verrouSite = PTHREAD_MUTEX_INITIALIZER;

int continuer=1;
//Indique si le site doit s'arrêter ou continuer
int choix=-1;
//Pointeur sur le dernier détenteur probable/connu du jeton
int last=-1;
//Pointeur sur le site à qui je dois transmettre le jeton 
int next=-1;
//Pour savoir s'il possède le jeton
bool avoirJeton;
// Si il reçoit le message "T_Mon_Next" il change cette variable
// pour éviter d'envoyer un message "Failure" (voir main)
int T_Mon_Next=-1;
int Jai_Jeton=-1;

//TimeOut va nous servir pour la tolérance aux pannes
int TimeOut = 4;
int TimeTmp = 0;



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
	//Pour l'Emetteur de la token Request
	int Emetteur = -1;
	//si c'est un token request, et que je suis pas la racine, je le passe a mon père et je modifi mon last au demandeur
	//sinon si je suis la racine, je modifi mon next au demandeur.
	if((m.str).substr(0, 12) == "TokenRequest") {
		Emetteur = atoi((char *)(((m.str).substr(12, 15)).c_str()));
		if(avoirJeton==false) {
			cout << "Token Request tranféré à mon père" << endl;
			//j'envoi a mon père
			write( voisins[last], (char *)((m.str).c_str()), MAX_SIZE );
			//je modifi mon last au demandeur (modifi mon arbre des last dynamiquement)
			last=Emetteur;
		}
		else {
			// Je met à jour mon next à celui qui est noté dans le message "Token Request"
			next=Emetteur;
			last=Emetteur;
			cout << "Token Request reçu, je le passe quand j'ai fini" << endl;
			
			//Je TRAVAIL UN PEU PLUS (pour tester la tolérance aux pannes)
			sleep(2);
				
			write( voisins[next], "Token", MAX_SIZE );
			avoirJeton=false;
			cout << "Je n'ai plus le jeton, je l'ai passé à mon next" << endl;
		}
	}
	
	// Si je reçoit "Token", je peut mettre ma variable avoirJeton à vrai
	// J'ai ainsi le droit d'accèder à la ressource à partir de ce moment là.
	if(m.str=="Token") {
		cout << "C'est bon j'ai le jeton." << endl;
		last=mon_port;
		avoirJeton=true;
	}
	
	// Je sui demandeur de la SC, donc demandeur du jeton...
	// Après un certain temps "TimeOut" si je ne reçoit pas le jeton, J'envoi un message "Consult"
	// Si je reçoit "Consult", et que mon next désigne l'expéditeur, alors je réponds
	// Si aprés un second "TimeOut" je ne reçoit aucune réponse à mon "Consult"
	// alors j'en déduit que mon prédécesseur est défaillant ou que ma demande du jeton à été perdu.
	// Je diffuse alors un message "Failure" pour détecter la présence du jeton
	
	// Variable JAI_JETON
	
	// Si le possesseur du jeton répond => recouvrement individuel, on refait notre demande du jeton
	// Sinon aucune réponse => recouvrement global, on lance une éléction pour régénérer le jeton
	// Alors l’arbre des last est réinitialisé et la file des next est supprimé.
	// Tous les sites en attente de SC devront ensuite réémettre leur requete.
	
	// Si je reçoit "Consult", et que mon next désigne l'expéditeur, alors je réponds "T_Mon_Next"
	if(m.str=="Consult") {
		cout << "Message CONSULT reçu" << endl;
		if(next==m.i) {
			write(voisins[m.i], "T_Mon_Next", MAX_SIZE);
			cout << "T_Mon_Next envoyé à " << m.i << endl;
		}
	}
	
	if(m.str=="T_Mon_Next") {
		T_Mon_Next=1;
	}
	
	// Si je reçoit "FAILURE" et que j'ai le Jeton je répond "Jai_Jeton"
	if(m.str=="Failure") {
		cout << "Message Failure reçu" << endl;
		if(avoirJeton) {
			write(voisins[m.i], "Jai_Jeton", MAX_SIZE);
			cout << "Jai_Jeton envoyé à " << m.i << endl;
		}
	}
	
	if(m.str=="Jai_Jeton") {
		Jai_Jeton=1;
	}
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
            }
        }
        else {
            cout << "-- Pas besoin de refaire une CONNEXION avec: " << port+cpt << endl;
        }
    }

    return NULL;
}

void * FonctionTimeOut(void * s) {
	sleep(TimeOut);
	TimeTmp=TimeOut;
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

	//Initialisation: Jeton, last, next ....
    if(mon_port==1988) {
    	avoirJeton=true;
    }
    else {
    	avoirJeton=false;
    	last = 1988;
    }
    
    //tant que le site est actif
    while ( choix != 0 ) {
        //Petit Menu!!
        cout<<endl<<"***** Petit Menu *****"<<endl<<"0 - Quitter"<<endl<<"1 - Token Request"<<endl<<endl;
        cin >> choix;
        
        //Si il a demandé à avoir le jeton
        if(choix==1) {
        	// S'il l'a deja on lui dit...
        	if( avoirJeton ) cout << "Tu as déjà le jeton !" <<endl;
        	// Sinon on envoi a son last un Token Request avec mon port comme indentifiant
        	// l'identifiant servira surtout quand la Token Request sera tranférée
        	else {
        		ostringstream oss;
				string chaine = "TokenRequest";
				int entier = mon_port;
				oss << chaine << entier;
        		write(voisins[last], (char*)(oss.str()).c_str() , MAX_SIZE);
        		// Si au bout d'un "TimeOut" on a pas le jeton, on envoi "Consult"
        		
        		// thread pour le timeOut car si on reçoit le jeton entre temps, on annule le recouvrement.
				pthread_t IdTimeOut1;
				TimeTmp=0;
				pthread_create(&IdTimeOut1, NULL, FonctionTimeOut, (void *) NULL);
				
				// Temps qu'on a pas le jeton et que le TimeOut n'est pas écoulé => on attends
				while(!avoirJeton && TimeTmp!=TimeOut) {continue;}
				
				// Si on a toujours pas le jeton et que le TimeOut est écoulé => envoi de Consult
        		if( !avoirJeton && (TimeTmp==TimeOut)) {
        			// on envoi à tous CONSULT
        			for(int i=port; i<port+n; i++) {
        				if(voisins[i]!=-1 && i!=mon_port) {
		        			write(voisins[i], "Consult" , MAX_SIZE);
		        			cout << "Message CONSULT envoyé à " << i << endl;
		        		}
	        		}
	        		
        			// Si aprés un second TimeOut il ne répond pas T_Mon_Next on envoi Failure.
        			pthread_t IdTimeOut2;
        			TimeTmp=0;
					pthread_create(&IdTimeOut2, NULL, FonctionTimeOut, (void *) NULL);
				
					// Temps qu'on a pas reçu T_Mon_Next et que le TimeOut n'est pas écoulé => on attends
					while(T_Mon_Next==-1 && TimeTmp!=TimeOut) {continue;}
				
					// Si on a toujours pas reçu T_Mon_Next et que le TimeOut est écoulé => envoi de Failure
        			if(T_Mon_Next==-1 && (TimeTmp==TimeOut)) {
        				// On envoi à tous FAILURE
        				for(int i=port; i<port+n; i++) {
		    				if(voisins[i]!=-1 && i!=mon_port) {
				    			write(voisins[i], "Failure" , MAX_SIZE);
        						cout << "Message FAILURE envoyé à " << i << endl;
				    		}
			    		}
			    		// Si aprés un troisieme TimeOut il ne répond pas Jai_Jeton => recouvrement global
		    			pthread_t IdTimeOut3;
		    			TimeTmp=0;
						pthread_create(&IdTimeOut3, NULL, FonctionTimeOut, (void *) NULL);
				
						// Temps qu'on a pas reçu T_Mon_Next et que le TimeOut n'est pas écoulé => on attends
						while(Jai_Jeton==-1 && TimeTmp!=TimeOut) {continue;}
			    		//si pas de réponse => recouvrement global
			    		if(Jai_Jeton == -1 && TimeTmp==TimeOut) {
			    			cout << "recouvrement GLOBAL" << endl;
			    		}
			    		//sinon => recouvrement individuel => On relance notre requête.
			    		else {
			    			cout << "recouvrement Individuel" << endl;
			    		}
        			}
	        	}
        	}
        }
    }

	//destruction du verrou
    cout << "-- On detruit le verrou." << endl;
	pthread_mutex_destroy( &verrouMsg );

	//on ferme la socket
    cout << "-- On ferme la socket." << endl;
    shutdown( ma_sock, SHUT_RDWR );
    close( ma_sock );

    return EXIT_SUCCESS;
}
