Ceci est le code de la laveuse de fûts automatique DIY de la [Microbrasserie
Grenaille](http://grenaille.blogspot.fr/).

Dépendances :

- [Bounce2](https://github.com/thomasfredericks/Bounce-Arduino-Wiring/tree/master/Bounce2)
- [Arduino-mk](https://github.com/sudar/Arduino-Makefile) (optionnel)

Cette machine propose deux cycles : nettoyage et désinfection. Les cycles sont
définis dans des tableaux d'actions. Par exemple :

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

Les actions `POMPER` et `ATTENDRE` prennent une durée (en secondes) en paramètres.
