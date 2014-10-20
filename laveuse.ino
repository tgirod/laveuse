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
#define PIN_RESEAU A0 // prise d'eau du réseau (fil blanc)
#define PIN_EGOUT 3   // écoulement vers l'égout (fil gris)
#define PIN_BAC 4     // entrée circuit fermé (fil violet)
#define PIN_RECUP 5   // écoulement circuit fermé (fil bleu)
#define PIN_POMPE 6   // pompe électrique (fil orange)
#define PIN_CHAUFFE 7 // thermoplongeur (fil rouge)

/* capteur */
#define PIN_SONDE 10 // CS PIN pour la sonde
// MOSI 11
// MISO 12
// SCK 13

/* constantes diverses */
#define TEMP_MIN 27           // on ne nettoie pas en dessous de cette température
#define TEMP_MAX 30           // couper la chauffe au dessus de cette température
#define CYCLES_LAVAGE 3       // recommencer X fois
#define DUREE_LAVAGE 60       // balancer la soude X secondes
#define DUREE_DESINFECTION 60 // balancer le désinfectant X secondes
#define DUREE_ECOULEMENT 10   // temps laissé pour l'écoulement de l'eau

/* quelques macros pour rendre le code plus explicite */
#define reseau(X) digitalWrite(PIN_RESEAU, !X)
#define egout(X) digitalWrite(PIN_EGOUT, !X)
#define bac(X) digitalWrite(PIN_BAC, !X)
#define recup(X) digitalWrite(PIN_RECUP, !X)
#define pompe(X) digitalWrite(PIN_POMPE, !X)
#define chauffe(X) digitalWrite(PIN_CHAUFFE, !X)
#define chauffeRead !digitalRead(PIN_CHAUFFE)

//Sensor addresses:
const byte CONFIG_REG_W = 0x80; // Config register write addr
const byte CONFIG_VALUE = 0xC3; // Config register value (Vbias+Auto+FaultClear+50Hz, see pg 12 of datasheet)
const byte ADC_WORD_MSB = 0x01; // Addr of first byte of data (start reading at RTD MSB)
const byte ADC_FAULT_REG = 0x07; // Addr of the fault reg

PWFusion_MAX31865_RTD sonde(PIN_SONDE);

// Boutons pour lancer les cycles de nettoyage et désinfection
Bounce btnNettoyage = Bounce();
Bounce btnDesinfection = Bounce();

jmp_buf env; // pour faire le saut en cas d'arrêt d'urgence

/* l'état dans lequel se trouve la laveuse */
static enum {ARRET, NETTOYAGE, DESINFECTION} etat;

/*
 * mesurer la température de l'eau et retourner le résultat en degrés
 */
double temperature() {
    static struct var_max31865 sonde_reg;
    double tmp = 0;

    sonde_reg.RTD_type = 2;                        // un-comment for PT1000 RTD

    sonde.MAX31865_full_read(&sonde_reg);          // Update MAX31855 readings 

    if(0 == sonde_reg.status)                       // no fault, print info to serial port
    {
        // calculate RTD temperature (simple calc, +/- 2 deg C from -100C to 100C)
        // more accurate curve can be used outside that range
        tmp = ((double)sonde_reg.rtd_res_raw / 32) - 256;
        //Serial.print(tmp);                          // print RTD resistance
        //Serial.println(" deg C");                   // print RTD temperature heading
    }  // end of no-fault handling
    else 
    {
        Serial.print("RTD Fault, register: ");
        Serial.println(sonde_reg.status);
        if(0x80 & sonde_reg.status)
        {
            Serial.println("RTD High Threshold Met");  // RTD high threshold fault
        }
        else if(0x40 & sonde_reg.status)
        {
            Serial.println("RTD Low Threshold Met");   // RTD low threshold fault
        }
        else if(0x20 & sonde_reg.status)
        {
            Serial.println("REFin- > 0.85 x Vbias");   // REFin- > 0.85 x Vbias
        }
        else if(0x10 & sonde_reg.status)
        {
            Serial.println("FORCE- open");             // REFin- < 0.85 x Vbias, FORCE- open
        }
        else if(0x08 & sonde_reg.status)
        {
            Serial.println("FORCE- open");             // RTDin- < 0.85 x Vbias, FORCE- open
        }
        else if(0x04 & sonde_reg.status)
        {
            Serial.println("Over/Under voltage fault");  // overvoltage/undervoltage fault
        }
        else
        {
            Serial.println("Unknown fault, check connection"); // print RTD temperature heading
        }
    }  // end of fault handling

    return tmp;
}

/*
 * arrêt d'urgence. On coupe tout, on ferme tout.
 */
void arreter() {
    if (etat != ARRET) {
        etat = ARRET;
        Serial.println("Arret d'urgence");
        pompe(0);
        chauffe(0);
        reseau(0);
        bac(0);
        delay(DUREE_ECOULEMENT*1000);
        recup(0);
        egout(0);
    }
}

/*
 * attendre X secondes, vérifier la chauffe toutes les secondes et lancer la
 * procédure d'arrêt d'urgence si nécessaire.
 */
void attendre(unsigned int secondes) { 
    unsigned long t = millis();
    while(millis()-t < secondes*1000UL){
        // arrêt d'urgence : longjmp pour sortir de la fonction
        if (etat == ARRET) {
            longjmp(env, 1);
        }
        // contrôle de la chauffe
        if (etat == NETTOYAGE) {
            if (temperature() < TEMP_MIN && !chauffeRead) { 
                Serial.println("Activer la chauffe");
                chauffe(1);
            }
            if (temperature() > TEMP_MAX && chauffeRead) {
                Serial.println("Couper la chauffe");
                chauffe(0);
            }
        }
        delay(1000); 
    }
}

/* cycle de rincage */
void rincage(int secondes) {
    Serial.print("Rincage: ");
    Serial.println(secondes);
    bac(0);
    recup(0);
    egout(1);
    reseau(1);
    attendre(secondes);
    reseau(0);
    attendre(DUREE_ECOULEMENT);
    egout(0);
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
    // attente de la chauffe
    while (temperature() < TEMP_MIN) {
        attendre(10);
    }
    recup(1);
    bac(1);
    // nettoyage
    for (int i=0; i<CYCLES_LAVAGE; i++) {
        pompe(1);
        attendre(DUREE_LAVAGE);
        pompe(0);
        attendre(DUREE_ECOULEMENT);
    }
    bac(0);
    recup(0);
    // post-rincage
    rincage(20);
    etat = ARRET;
    beep();
}

/* cycle de désinfection */
void desinfection() {
    Serial.println("Desinfection");
    etat = DESINFECTION;
    recup(1);
    bac(1);
    pompe(1);
    attendre(DUREE_DESINFECTION);
    pompe(0);
    attendre(DUREE_ECOULEMENT);
    bac(0);
    recup(0);
    rincage(10);
    etat = ARRET;
    beep();
}

void setup() {
    // interface utilisateur
    pinMode(PIN_ARRET, INPUT_PULLUP);
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
    pinMode(PIN_CHAUFFE, OUTPUT);
    pinMode(PIN_SONDE, OUTPUT);
    // état initial de la machine
    etat = ARRET;
    pompe(0);
    chauffe(0);
    reseau(0);
    bac(0);
    egout(0);
    recup(0);
    // initialiser le bouton d'arrêt d'urgence
    attachInterrupt(0, arreter, FALLING);
    // initialiser la lecture de température
    Serial.begin(9600);
    Serial.println("serial ok");
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
        }
        // démarrage de la désinfection
        if(btnDesinfection.update() && btnDesinfection.read() == LOW) {
            desinfection();
        }
    }
}

