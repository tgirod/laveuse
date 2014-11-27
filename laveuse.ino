#include <Bounce2.h>
#include <SPI.h>
#include <setjmp.h>

#include <PlayingWithFusion_MAX31865.h>        // core library
#include <PlayingWithFusion_MAX31865_STRUCT.h> // struct library

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
#define PIN_PLONGEUR 7 // thermoplongeur (fil rouge)

/* capteur */
#define PIN_SONDE 10 // CS PIN pour la sonde
// MOSI 11
// MISO 12
// SCK 13

/* constantes diverses */
#define TEMP_MIN 80           // on ne nettoie pas en dessous de cette température FIXME
#define TEMP_MAX 85           // couper le plongeur au dessus de cette température FIXME
#define CYCLES_LAVAGE 4       // recommencer X fois
#define DUREE_LAVAGE 40       // balancer la soude X secondes
#define DUREE_DESINFECTION 60 // balancer le désinfectant X secondes
#define DUREE_ECOULEMENT 20   // temps laissé pour l'écoulement de l'eau

/* quelques macros pour rendre le code plus explicite */
#define reseau(X) digitalWrite(PIN_RESEAU, !X)
#define egout(X) digitalWrite(PIN_EGOUT, !X)
#define bac(X) digitalWrite(PIN_BAC, !X)
#define recup(X) digitalWrite(PIN_RECUP, !X)
#define pompe(X) digitalWrite(PIN_POMPE, !X)
#define plongeur(X) digitalWrite(PIN_PLONGEUR, !X)
#define plongeurRead !digitalRead(PIN_PLONGEUR)

//Sensor addresses:
const byte CONFIG_REG_W = 0x80; // Config register write addr
const byte CONFIG_VALUE = 0xC3; // Config register value (Vbias+Auto+FaultClear+50Hz, see pg 12 of datasheet)
const byte ADC_WORD_MSB = 0x01; // Addr of first byte of data (start reading at RTD MSB)
const byte ADC_FAULT_REG = 0x07; // Addr of the fault reg

PWFusion_MAX31865_RTD sonde(PIN_SONDE);

// Boutons pour lancer les cycles de nettoyage et désinfection
Bounce btnNettoyage = Bounce();
Bounce btnDesinfection = Bounce();
Bounce btnArret = Bounce();

jmp_buf env; // pour faire le saut en cas d'arrêt d'urgence

/* l'état dans lequel se trouve la laveuse */
static enum {ARRET, NETTOYAGE, DESINFECTION} etat;
double temp = 0;

/*
 * mesurer la température de l'eau et retourner le résultat en degrés
 */
void temperature() {
    static struct var_max31865 sonde_reg;
    temp = 0;

    sonde_reg.RTD_type = 2;                        // un-comment for PT1000 RTD

    sonde.MAX31865_full_read(&sonde_reg);          // Update MAX31855 readings

    if(0 == sonde_reg.status) {
        // calculate RTD temperature (simple calc, +/- 2 deg C from -100C to 100C)
        // more accurate curve can be used outside that range
        temp = ((double)sonde_reg.rtd_res_raw / 32) - 256;
        Serial.print(temp);        // print RTD resistance
        Serial.println(" deg C"); // print RTD temperature heading
    }
    else {
        Serial.print("RTD Fault, register: ");
        Serial.println(sonde_reg.status);
        if(0x80 & sonde_reg.status) {
            Serial.println("RTD High Threshold Met");  // RTD high threshold fault
        }
        else if(0x40 & sonde_reg.status) {
            Serial.println("RTD Low Threshold Met");   // RTD low threshold fault
        }
        else if(0x20 & sonde_reg.status) {
            Serial.println("REFin- > 0.85 x Vbias");   // REFin- > 0.85 x Vbias
        }
        else if(0x10 & sonde_reg.status) {
            Serial.println("FORCE- open");             // REFin- < 0.85 x Vbias, FORCE- open
        }
        else if(0x08 & sonde_reg.status) {
            Serial.println("FORCE- open");             // RTDin- < 0.85 x Vbias, FORCE- open
        }
        else if(0x04 & sonde_reg.status) {
            Serial.println("Over/Under voltage fault");  // overvoltage/undervoltage fault
        }
        else {
            Serial.println("Unknown fault, check connection"); // print RTD temperature heading
        }
    }  // end of fault handling
}

void chauffer() {
    temperature();
    if (!plongeurRead && temp < TEMP_MIN) {
        Serial.println("Activer la chauffe");
        plongeur(1);
    } else if (plongeurRead && temp > TEMP_MAX) {
        Serial.println("Couper la chauffe");
        plongeur(0);
    }
}

/*
 * attendre X secondes, vérifier la chauffe toutes les secondes et lancer la
 * procédure d'arrêt d'urgence si nécessaire.
 */
void attendre(int secondes) {
    unsigned long t = millis();
    while(millis()-t < secondes*1000UL) {
        // lire le bouton d'arrêt d'urgence
        if(btnArret.update() && btnArret.read() == LOW) {
            etat = ARRET;
            Serial.println("Arret d'urgence");
            pompe(0);
            plongeur(0);
            reseau(0);
            bac(0);
            delay(DUREE_ECOULEMENT*1000);
            recup(0);
            egout(0);
            longjmp(env, 1);
        }
        if (millis() % 1000 == 0 && etat == NETTOYAGE) {
            chauffer();
            delay(1);
        }
    }
}

/* cycle de rincage */
void rincage(int secondes) {
    Serial.print("Rincage: ");
    Serial.print(secondes);
    Serial.println(" secondes");
    bac(0);
    recup(0);
    egout(1);
    reseau(1);
    attendre(3);
    Serial.println("demarrer la pompe");
    pompe(1);
    attendre(secondes);
    Serial.println("arreter la pompe");
    pompe(0);
    reseau(0);
    attendre(DUREE_ECOULEMENT);
    egout(0);
    Serial.println("Fin du rincage");
}

void beep() {
    tone(PIN_BEEP, 1000);
    delay(1000);
    noTone(PIN_BEEP);
}

/* cycle de nettoyage */
void nettoyage() {
    Serial.println("Nettoyage");
    etat = NETTOYAGE;
    // pré-rincage
    rincage(10);
    // attendre que la température soit bonne
    while (temp < TEMP_MAX-1) {
        attendre(10);
    }
    recup(1);
    bac(1);
    attendre(3);
    // nettoyage
    for (int i=0; i<CYCLES_LAVAGE; i++) {
        Serial.print("Nettoyage, cycle ");
        Serial.println(i+1);
        pompe(1);
        attendre(DUREE_LAVAGE);
        pompe(0);
        Serial.println("ecoulement");
        attendre(DUREE_ECOULEMENT);
    }
    Serial.println("Fin des cycles de nettoyage");
    bac(0);
    recup(0);
    // post-rincage
    rincage(10);
    rincage(10);
    rincage(10);
    etat = ARRET;
    Serial.println("Fin du nettoyage");
}

/* cycle de désinfection */
void desinfection() {
    Serial.println("Desinfection");
    etat = DESINFECTION;
    recup(1);
    bac(1);
    attendre(3);
    pompe(1);
    attendre(DUREE_DESINFECTION);
    Serial.println("ecoulement");
    pompe(0);
    attendre(DUREE_ECOULEMENT);
    bac(0);
    recup(0);
    rincage(6);
    etat = ARRET;
    Serial.println("Fin de la desinfection");
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
    pinMode(PIN_BEEP, OUTPUT);
    // relais (4 électrovannes + chauffe + sonde (SPI CS)
    pinMode(PIN_RESEAU, OUTPUT);
    pinMode(PIN_EGOUT, OUTPUT);
    pinMode(PIN_BAC, OUTPUT);
    pinMode(PIN_RECUP, OUTPUT);
    pinMode(PIN_POMPE, OUTPUT);
    pinMode(PIN_PLONGEUR, OUTPUT);
    pinMode(PIN_SONDE, OUTPUT);
    // état initial de la machine
    etat = ARRET;
    pompe(0);
    plongeur(0);
    reseau(0);
    bac(0);
    egout(0);
    recup(0);
    // initialiser la lecture de température
    SPI.begin();
    SPI.setDataMode(SPI_MODE3);
    sonde.MAX31865_config();
    delay(100);
}

void loop() {
    setjmp(env);
    Serial.println("en attente d'une commande");
    while (1) {
        // démarrage du nettoyage
        if(btnNettoyage.update() && btnNettoyage.read() == LOW) {
            nettoyage();
            break;
        }
        // démarrage de la désinfection
        if(btnDesinfection.update() && btnDesinfection.read() == LOW) {
            desinfection();
            break;
        }
    }
}

