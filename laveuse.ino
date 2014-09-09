#define PIN_ARRET 2        // arrêt d'urgence (int.0)
#define PIN_LAVAGE 3       // lancement du lavage
#define PIN_DESINFECTION 4 // lancement de la désinfection
#define PIN_VANNES  5      // contrôle des électrovannes
#define PIN_POMPE   6      // contrôle de la pompe
#define PIN_CHAUFFE 7      // contrôle du thermo-plongeur
#define PIN_BUZZER  8      // beep de fin
#define PIN_SONDE   A0     // sonde de température

#define TEMP 80               // température de la soude
#define CYCLES_LAVAGE 3       // recommencer X fois
#define DUREE_LAVAGE 60       // balancer la soude X secondes
#define DUREE_ECOULEMENT 10   // attendre X secondes
#define DUREE_RINCAGE 20      // balancer la flotte X secondes
#define DUREE_DESINFECTION 60 // balancer le désinfectant X secondes

/*
 * l'état dans lequel se trouve la laveuse
 */
static enum {ARRET, LAVAGE, DESINFECTION} etat;

/*
 * quelques macros pour rendre le code plus explicite
 */
#define ouvrirCircuit() digitalWrite(PIN_VANNES, HIGH)
#define fermerCircuit() digitalWrite(PIN_VANNES, LOW)
#define demarrerPompe() digitalWrite(PIN_POMPE, HIGH)
#define arreterPompe() digitalWrite(PIN_POMPE, LOW)
#define demarrerChauffe() digitalWrite(PIN_POMPE, HIGH)
#define arreterChauffe() digitalWrite(PIN_POMPE, LOW)

/*
 * mesurer la température de l'eau et retourner le résultat en degrés
 */
float mesurerTemperature() {
    const int v1 = 5; // tension fournie par l'arduino
    const int r2 = 1000; // résistance 1k
    float v2 = analogRead(PIN_SONDE) * 5 / 1023.0; // tension mesurée aux bornes
    float r1 = v2 * r2 / (v1 - v2);
    // FIXME faire la lecture qui va bien
    return r1;
}

/*
 * attendre X secondes, vérifier la chauffe toutes les secondes et lancer la
 * procédure d'arrêt d'urgence si nécessaire.
 */
int attendre(unsigned int secondes) { unsigned long t = millis();
    while(millis()-t < secondes*1000UL){
        // arrêt d'urgence
        if (etat == ARRET) {
            return 1;
        }
        // contrôler la chauffe
        if (etat == LAVAGE && mesurerTemperature() < TEMP) { 
            demarrerChauffe(); 
        }
        else { 
            arreterChauffe(); 
        } 
        delay(1000); 
    }
    return 0;
}

void arreter() {
    arreterPompe();
    fermerCircuit();
    etat = ARRET;
}

/*
 * cycle de nettoyage
 */
void laver() {
    // quelque chose est déjà en route
    if (etat != ARRET) return;
    // lancer le cycle
    etat = LAVAGE;
    fermerCircuit();
    // attendre que l'eau soit chaude
    while (mesurerTemperature() < TEMP) {
        if (attendre(1)) return;
    }
    // lavage
    for (int i=0; i<CYCLES_LAVAGE; i++) {
        demarrerPompe();
        if (attendre(DUREE_LAVAGE)) return;
        arreterPompe();
        if (attendre(DUREE_ECOULEMENT)) return;
    }
    // rincage
    ouvrirCircuit();
    if (attendre(DUREE_RINCAGE)) return;
    fermerCircuit();
    if (attendre(DUREE_ECOULEMENT)) return;
    // fin
    etat = ARRET;
}

/*
 * cycle de désinfection
 */
void desinfecter() {
    // quelque chose est déjà en route
    if (etat != ARRET) return;
    // lancer le cycle
    etat = DESINFECTION;
    // désinfection
    fermerCircuit();
    demarrerPompe();
    if (attendre(DUREE_DESINFECTION)) return;
    arreterPompe();
    if (attendre(DUREE_ECOULEMENT)) return;
    // rincage
    ouvrirCircuit();
    if (attendre(DUREE_RINCAGE)) return;
    fermerCircuit();
    if (attendre(DUREE_ECOULEMENT)) return;
    // fin
    etat = ARRET;
}

//void setup() {
    //pinMode(PIN_VANNES, OUTPUT);
    //pinMode(PIN_POMPE, OUTPUT);
    //pinMode(PIN_CHAUFFE, OUTPUT);
    //pinMode(PIN_LAVAGE, INPUT_PULLUP);
    //pinMode(PIN_DESINFECTION, INPUT_PULLUP);
    //pinMode(PIN_ARRET, INPUT_PULLUP);
    //// dans le doute
    //etat = ARRET;
    //fermerCircuit();
    //arreterPompe();
    //arreterChauffe();
    //// initialiser le bouton d'arrêt d'urgence
    //attachInterrupt(0, arreter, FALLING);
//}

//void loop() {
    //// lancer le cycle de lavage
    //if (!digitalRead(PIN_LAVAGE)) {
        //laver();
    //}
    //// lancer le cycle de désinfection
    //if (!digitalRead(PIN_DESINFECTION)) {
        //desinfecter();
    //}
//}

void setup() {
    Serial.begin(9600);
    pinMode(8, OUTPUT);
    pinMode(9, OUTPUT);
    digitalWrite(8, LOW);
    digitalWrite(9, LOW);
}

void loop() {
    Serial.println("fermer 1/4");
    digitalWrite(8, LOW); // fermer 1/4
    delay(5000);
    Serial.println("ouvrir 2/3");
    digitalWrite(9, HIGH); // ouvrir 2/3
    delay(5000);
    Serial.println("fermer 2/3");
    digitalWrite(9, LOW); // fermer 2/3
    delay(5000);
    Serial.println("ouvrir 1/4");
    digitalWrite(8, HIGH); // ouvrir 1/4
    delay(5000);
}
