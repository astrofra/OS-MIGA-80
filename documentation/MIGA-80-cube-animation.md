# Cube Lua animé — état du runtime

Le cube wireframe tourne autour de X et Y en perspective pendant dix secondes sur le viewport
256 × 256, avec quatre intensités de bleu selon la profondeur moyenne des
arêtes. Le source complet tient dans la vue actuelle de 30 lignes :
[`assets/demo/cube.lua`](../assets/demo/cube.lua). Les vitesses sont de 0,55 rad/s
sur X et 0,8 rad/s sur Y, avec une inclinaison initiale de 0,65 rad sur X.
Les quatre valeurs trigonométriques sont calculées une seule fois par image.

## Lancer la démo

```sh
gmake miga80-cube-adf
```

Démarrer `build/distribution/miga80-cube.adf` sur un A1200 PAL, Kickstart 3.0
ou 3.1. Le cube est compilé sur l’Amiga puis lancé automatiquement. Après dix
secondes, la vue source revient ; F5 relance, ESC interrompt un calcul en cours,
Ctrl-Q ferme l’application depuis la vue source ou résultat.

L’ADF normal contient aussi `DATA/CUBE.LUA`. Depuis le Shell :

```text
MIGA80:MIGA80 MIGA80:DATA/CUBE.LUA RAM:MIGA80-BOOTED.TXT CUBE
```

Sans l’argument `CUBE`, F5 lance le source et conserve la dernière image avec
le bandeau de résultat jusqu’à ESC. Le source est toujours en lecture seule.

## Primitives Lua

| Appel | Contrat |
|---|---|
| `sin(angle)` / `cos(angle)` | Argument et résultat `fix` Q16.16 signé ; angle en radians. |
| `cls(color)` | Efface la couche sélectionnée ; couleur `u8`, 0 à 15. Les autres couleurs sont ignorées. |
| `flip()` | Termine les dessins, affiche le tampon préparé et attend que l’ancien soit réutilisable. |
| `time()` | Secondes `fix` depuis la première image affichée ; zéro avant le premier `flip()`. |

Exemple minimal :

```lua
function main(): void
 local t:fix=0.0
 layer(PLANAR)
 while t<10.0 do
  cls(0)
  line(128,128,128+i32(cos(t)*100.0),
       128+i32(sin(t)*100.0),8)
  flip()
  t=time()
 end
end
```

L’horloge est indépendante du nombre d’images ; un rendu plus lent diminue le
nombre d’images, sans allonger l’animation à un nombre arbitraire de frames.
La sortie intervient après la première image achevée à partir de dix secondes.
`time()` sature à la valeur positive maximale de Q16.16 après 32 768 secondes.

La trigo utilise une table de quart de sinus de 257 valeurs, une interpolation
entière et une réduction d’angle entière 64 bits. Aucun flottant ni FPU au
runtime. Le test de 100 000 angles signés couvre aussi les grandes amplitudes ;
son erreur absolue maximale observée est 0,00010799.

## Dessin et double buffer

Le runtime prépare deux bitmaps AGA de huit plans, soit 128 Kio de Chip RAM
supplémentaire, et conserve le bitmap original de l’éditeur. Le code Lua dessine
dans le tampon caché. `flip()` échange les deux images complètes ; les deux
playfields sont donc publiés ensemble.

PLANAR écrit directement les quatre plans de PF2 du tampon caché. `line()`
utilise le mode ligne du blitter et `cls()` un remplissage blitter, avec les
registres matériels et l’ownership par lots décrits dans
[les primitives de dessin](MIGA-80-drawing-primitives.md). Le cube n’utilise
aucune conversion chunky-to-planar pendant l’animation.

PIXEL conserve son tampon chunky CPU ; lorsqu’il a été utilisé, `flip()` le
convertit vers les quatre plans PF1 du tampon caché. `cls()` affecte seulement
la couche sélectionnée. Après un échange, les anciens pixels PLANAR du nouveau
tampon de dessin ne sont pas effacés ni recopiés automatiquement : commencer
chaque image complète par `cls(0)`. Le tampon chunky PIXEL reste persistant.

Le propriétaire appelle `ChangeScreenBuffer` et attend **les deux** messages
`dbi_SafeMessage` et `dbi_DispMessage` avant réutilisation ou libération. Le
superviseur continue de traiter ESC pendant cette attente. Le worker ne
possède aucun bitmap, port, I/O ou DMA : son arrêt est suivi du drainage des
messages, de la restauration de l’écran original et de la libération des deux
bitmaps. L’horloge utilise `timer.device/ReadEClock`, avec la fréquence renvoyée
par le système.

Références de contrat : [ChangeScreenBuffer](https://d0.se/autodocs/intuition.library/ChangeScreenBuffer),
[AllocDBufInfo](https://d0.se/autodocs/graphics.library/AllocDBufInfo) et
[ReadEClock](https://d0.se/autodocs/timer.device/ReadEClock).

## Compilateur et ABI

Le contexte graphique passe de 52 à 72 octets, en conservant tous les offsets
existants. Les cinq nouveaux services suivent le même contrat D0–D2/A0–A1 ;
les fonctions mathématiques et l’horloge renvoient leur résultat dans D0.
Les valeurs vivantes traversent aussi les appels sans argument. Le compilateur
accepte désormais 128 statements et 512 nœuds AST, avec maintenant 24 variables
locales et 32 blocs de contrôle.

L’encodeur direct O1 prend désormais en charge les divisions entières et fixes,
et les conversions `fix(i32)` / `i32(fix)`, nécessaires à la projection. Les
fautes de division par zéro et de conversion hors limites restent contrôlées.
Le cube fait 1 540 octets natifs sans gardes, 1 600 avec budget ; sa borne de
pile est de 1 164 octets, dans les 4 Kio réservés au programme.

## Vérifications

```sh
gmake animation-test
gmake check
gmake miga80-cube-fs-uae
```

- `gmake check` : suite générale passée, compilations hôte/Amiga et smoke tests inclus.
- STOPTEST : treize arrêts, services bloqués et relance finale de Mandelbrot passés.
- GRAPHICSTEST : trois cycles des deux playfields, avec hashes, palette Copper et
  libération des ressources inchangés.
- Hôte ASan/UBSan : trigo, simulation de 250 images / 3 000 arêtes et comparaison
  à une projection flottante indépendante, avec les quatre bandes de profondeur.
- Musashi : mêmes 3 000 arêtes et même trace `559273d3` pour O1 direct/GNU,
  avec et sans gardes, malgré des services qui détruisent D0–D2/A0–A1.
  Fixture distincte des appels avec résultat en O0/O1/guarded.
- 1 326 exécutions natives de divisions/conversions, limites et valeurs variées,
  plus les fautes contrôlées ; résultats identiques entre encodeur direct et GNU.
- FS-UAE, A1200 PAL, 2 Mio Chip sans Fast : trois arrêts ESC pendant une
  animation non gardée, puis deux exécutions complètes par F5. Retour source,
  relance, Ctrl-Q, palette Copper, gardes de pile, signaux et mémoire stable
  contrôlés. Le rapport indique le nombre d’images et la durée mesurés à chaque exécution.
- Inspection visuelle : rotation, cube dans le viewport, fond effacé et arêtes
  lointaines sombres. Capture : `build/reports/cube-fs-uae.png`.

Rapports : `build/reports/animation-host.txt`, `animation-math.txt`,
`cube-fs-uae.txt` et `cube-check.log`. Les tests ADF partagent leur répertoire
et doivent s’exécuter successivement.

La mesure FS-UAE valide le fonctionnement émulé. Le retour des tests physiques
sur A1200 reste attendu ; aucune cadence sur machine réelle n’est encore validée.

## Variante chunky / PF1

[`assets/demo/cube-chunky.lua`](../assets/demo/cube-chunky.lua) est la même
animation sur deux axes, avec une seule différence : `layer(PIXEL)` remplace
`layer(PLANAR)`. `cls()` efface le tampon chunky sur le CPU, `line()` utilise
Bresenham CPU et chaque `flip()` convertit les 65 536 pixels vers PF1 dans le
bitmap caché. La conversion utilise par défaut l'adaptation Kalms, choix validé
le 14 septembre 2026. `C2P=MASK32` sélectionne notre assembleur mask32,
`C2P=REFERENCE` la référence C.
Voir [l'intégration et la comparaison C2P](MIGA-80-c2p-runtime-comparison.md).

```sh
gmake animation-chunky-test
gmake miga80-cube-chunky-fs-uae
```

`build/distribution/miga80-cube-chunky.adf` démarre cette variante automatiquement.
Sur ce disque, `DATA/CUBE.LUA` et `DATA/DEFAULT.LUA` contiennent le source chunky.
F5 relance, ESC interrompt et Ctrl-Q quitte. Le source PLANAR reste dans
`assets/demo/cube.lua`, et son ADF conserve son nom habituel.

Le test hôte impose que les deux sources diffèrent uniquement par la couche.
Il vérifie 250 images, 3 000 arêtes, les pixels chunky non vides et les plans
PLANAR vides. La trace native chunky attendue est `559233d3` ; les sorties O1
et guarded font toujours 1 540 / 1 600 octets, avec une borne de pile de
1 164 octets. Le profil ADF `CUBEPIXELTEST` contrôle PF1 par lecture des pixels
AGA après C2P, PF2 vide, zéro ligne blitter, les échanges de buffers, les arrêts
ESC et les relances. La lecture de contrôle intervient après capture de la durée. L’injection ESC
reste programmée après six secondes pour couvrir aussi le témoin C lent ; le profil PLANAR garde son délai de 0,5 seconde.

Rapports : `build/reports/animation-chunky-host.txt` et
`build/reports/cube-chunky-fs-uae.txt`. Les mesures restent celles de FS-UAE,
sans validation physique A1200.

Premier essai, avant intégration de l'ASM, A1200 PAL / 2 Mio Chip / sans Fast dans FS-UAE :

| Variante | Images affichées | Durée depuis la première image |
|---|---:|---:|
| PLANAR, blitter direct | 168 | 10,04 s |
| PIXEL, Bresenham CPU + C2P C | 5 | 10,42 s |

Avec cette référence C, la variante chunky fonctionne mais la rotation est très saccadée. Ces nombres
mesurent la chaîne de rendu complète, pas le coût isolé de Bresenham ou de la
C2P. Le test passe trois interruptions ESC puis deux animations complètes, avec
lecture de contrôle des pixels et sans croissance mémoire. Capture :
`build/reports/cube-chunky-fs-uae.png`.

La comparaison actuelle retrouve 5 images avec la référence, 40 avec notre
ASM mask32 et 83 avec l'adaptation Kalms, sur deux exécutions par backend.
Le disque `build/distribution/miga80-cube-chunky-kalms.adf` démarre Kalms ;
`miga80-cube-chunky.adf` utilise également Kalms par défaut. Tous deux
conservent le même Lua.

Les [cubes pleins éclairés](MIGA-80-solid-cube.md) étendent ce runtime avec
`tri()` et un contexte graphique de 80 octets, sans déplacer les anciens champs.
