#include <Bounce2.h>
#include <setjmp.h>

/* interface utilisateur */
#define PIN_ARRET 2         // arrêt d'urgence (fil jaune) -- interruption
#define PIN_NETTOYAGE A2    // lancement du nettoyage (fil rouge)
#define PIN_DESINFECTION A1 // lancement de la désinfection (fil orange)
#define PIN_BEEP A3         // avertisseur sonore

/* asservissement */
#define PIN_RESEAU A0  // prise d'eau du réseau (fil blanc)
#define PIN_EGOUT 3    // écoulement vers l'égout (fil gris)
#define PIN_BAC 4      // entrée circuit fermé (fil violet)
#define PIN_RECUP 5    // écoulement circuit fermé (fil bleu)
#define PIN_POMPE 6    // pompe électrique (fil orange)

/* quelques macros pour rendre le code plus explicite */
#define reseau(X) digitalWrite(PIN_RESEAU, !X)
#define egout(X) digitalWrite(PIN_EGOUT, !X)
#define bac(X) digitalWrite(PIN_BAC, !X)
#define recup(X) digitalWrite(PIN_RECUP, !X)
#define pompe(X) digitalWrite(PIN_POMPE, !X)

enum actionId {
    VANNE_PRODUIT,
    VANNE_RINCAGE,
    VANNE_FERME,
    COUPER_RESEAU,
    POMPER,
    ATTENDRE,
    FIN
};

struct action {
    enum actionId nom;
    unsigned int param;
};

struct action nettoyage[] = {
    {VANNE_RINCAGE, 0},
    {POMPER, 10},
    {COUPER_RESEAU, 0},
    {ATTENDRE, 10},
    {VANNE_PRODUIT, 0},
    {POMPER, 40},
    {ATTENDRE, 30},
    {POMPER, 40},
    {ATTENDRE, 30},
    {POMPER, 40},
    {ATTENDRE, 30},
    {POMPER, 40},
    {ATTENDRE, 30},
    {VANNE_RINCAGE, 0},
    {POMPER, 10},
    {COUPER_RESEAU, 0},
    {ATTENDRE, 10},
    {VANNE_RINCAGE, 0},
    {POMPER, 10},
    {COUPER_RESEAU, 0},
    {ATTENDRE, 10},
    {VANNE_RINCAGE, 0},
    {POMPER, 10},
    {COUPER_RESEAU, 0},
    {ATTENDRE, 10},
    {FIN, 0}
};

struct action desinfection[] = {
    {VANNE_PRODUIT, 0},
    {POMPER, 30},
    {ATTENDRE, 30},
    {POMPER, 30},
    {ATTENDRE, 30},
    {VANNE_RINCAGE, 0},
    {POMPER, 6},
    {COUPER_RESEAU, 0},
    {ATTENDRE, 30},
    {FIN, 0}
};

// Boutons pour lancer les cycles de nettoyage et désinfection
Bounce btnNettoyage = Bounce();
Bounce btnDesinfection = Bounce();
Bounce btnArret = Bounce();

jmp_buf env; // pour faire le saut en cas d'arrêt d'urgence

/* l'état dans lequel se trouve la laveuse */
static enum {ARRET, NETTOYAGE, DESINFECTION} etat;

/*
 * attendre X secondes, vérifier la chauffe toutes les secondes et lancer la
 * procédure d'arrêt d'urgence si nécessaire.
 */
void attendre(int secondes)
{
    unsigned long t = millis();
    while(millis()-t < secondes*1000UL) {
        // lire le bouton d'arrêt d'urgence
        if(btnArret.update() && btnArret.read() == LOW) {
            etat = ARRET;
            Serial.println("Arret d'urgence");
            pompe(0);
            reseau(0);
            bac(0);
            longjmp(env, 1);
        }
    }
}

void beep() {
    tone(PIN_BEEP, 1000);
    delay(1000);
    noTone(PIN_BEEP);
}

int executer(struct action a)
{
    switch (a.nom) {
        case VANNE_PRODUIT:
            Serial.println("VANNE_PRODUIT");
            reseau(0);
            egout(0);
            bac(1);
            recup(1);
            attendre(5);
            return 0;
        case VANNE_RINCAGE:
            Serial.println("VANNE_RINCAGE");
            bac(0);
            recup(0);
            egout(1);
            reseau(1);
            attendre(5);
            return 0;
        case COUPER_RESEAU:
            Serial.println("COUPER_RESEAU");
            reseau(0);
            attendre(5);
            return 0;
        case VANNE_FERME:
            Serial.println("VANNE_FERME");
            reseau(0);
            egout(0);
            bac(0);
            recup(0);
            attendre(5);
            return 0;
        case POMPER:
            Serial.print("POMPER ");
            Serial.println(a.param);
            pompe(1);
            attendre(a.param);
            pompe(0);
            return 0;
        case ATTENDRE:
            Serial.print("ATTENDRE ");
            Serial.println(a.param);
            attendre(a.param);
            return 0;
        case FIN:
            Serial.println("FIN");
            pompe(0);
            reseau(0);
            bac(0);
            egout(0);
            recup(0);
            beep();
            return 1;
        default:
            return 1;
    }
}

void derouler(struct action as[])
{
    int i = 0;
    int ret = 0;
    while (ret == 0) {
        ret = executer(as[i]);
        i++;
    }
}

void nettoyer()
{
    derouler(nettoyage);
}

void desinfecter()
{
    derouler(desinfection);
}

void setup() {
    Serial.begin(9600);
    // interface utilisateur
    pinMode(PIN_ARRET, INPUT_PULLUP);
    btnArret.attach(PIN_ARRET);
    btnArret.interval(5);
    pinMode(PIN_NETTOYAGE, INPUT_PULLUP);
    btnNettoyage.attach(PIN_NETTOYAGE);
    btnNettoyage.interval(5);
    pinMode(PIN_DESINFECTION, INPUT_PULLUP);
    btnDesinfection.attach(PIN_DESINFECTION);
    btnDesinfection.interval(5);
    // relais (4 électrovannes + chauffe + sonde (SPI CS)
    pinMode(PIN_RESEAU, OUTPUT);
    pinMode(PIN_EGOUT, OUTPUT);
    pinMode(PIN_BAC, OUTPUT);
    pinMode(PIN_RECUP, OUTPUT);
    pinMode(PIN_POMPE, OUTPUT);
    // état initial de la machine
    etat = ARRET;
    pompe(0);
    reseau(0);
    bac(0);
    egout(0);
    recup(0);
    delay(100);
}

void loop() {
    setjmp(env);
    Serial.println("en attente d'une commande");
    while (1) {
        // démarrage du nettoyage
        if(btnNettoyage.update() && btnNettoyage.read() == LOW) {
            nettoyer();
            break;
        }
        // démarrage de la désinfection
        if(btnDesinfection.update() && btnDesinfection.read() == LOW) {
            desinfecter();
            break;
        }
    }
}

