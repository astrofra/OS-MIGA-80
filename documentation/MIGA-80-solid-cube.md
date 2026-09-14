# Cubes pleins éclairés

`assets/demo/cube-solid.lua` et `cube-solid-chunky.lua` montrent le même cube
pendant dix secondes, avec rotation sur X et Y, projection perspective et
double buffering. Ils sont inclus dans `SYS:demos` sur `release/miga80.adf`.
Le seul changement entre les sources est `layer(PLANAR)` / `layer(PIXEL)`.
Les anciens cubes wireframe restent disponibles.

Les deux versions jouent désormais le MOD fourni via `music_play`. Voir
[l’API musicale et son interruption CIA](MIGA-80-mod-playback.md). La musique
s’arrête automatiquement au terme des dix secondes ou sur ESC.

## Calcul Lua

Les quatre sinus/cosinus sont calculés une fois par image. Chaque normale
de face est un axe signé du cube, transformé par les mêmes rotations que
les sommets : aucune racine carrée, normalisation, texture ou interpolation
de lumière par pixel n'est nécessaire.

La caméra est à `(0,0,-6)` ; pour ce cube centré de demi-côté 1, la condition
de visibilité est `1 + 6*nz < 0`. Le sample utilise `nz < -0.167`, une
approximation légèrement conservatrice. Le cube étant convexe, les faces
retenues ne se recouvrent pas : ni tri par profondeur ni Z-buffer.

La lumière directionnelle vient du haut, de la gauche et de la caméra.
`0.2 - 0.7*nz + 0.2*ny - 0.2*nx` combine une lumière ambiante et un produit
scalaire. Une courte boucle quantifie ce résultat en couleurs 2 à 9 de la
palette actuelle, du bleu sombre au blanc. Chaque quadrilatère est dessiné
par deux triangles de même couleur, avec quatre sommets projetés par face.

Les sources restent dans les limites de la vue actuelle : 30 lignes, au plus
64 colonnes et moins de 4 Kio. Le compilateur accepte maintenant 24 variables
locales ; ce sample en utilise 20, avec 262 nœuds AST et 80 statements.
Les limites de 384 nœuds, 80 statements et 32 blocs restent inchangées.
Le code natif occupe 1 884 octets, ou 1 964 avec budget, pour une borne de
pile de 1 176 octets sur les 4 Kio réservés.

## Primitive `tri`

```lua
layer(PLANAR)
tri(32,32,224,64,128,224,8)
```

Les six coordonnées sont des `i32` dans `-32768..32767` ; le rasteriseur
clippe à `0..255`. La couleur est un `u8` dans `0..15`. Une coordonnée hors
du domaine, une couleur invalide, un triangle dégénéré ou entièrement hors
écran ne produit aucun pixel. La couleur zéro efface les pixels couverts.
L'ordre des sommets est indifférent.

Le rasteriseur teste les centres des pixels, avec inclusion des bords haut
et gauche et exclusion des bords bas et droit. Les triangles adjacents
partagent exactement leurs bords, sans trous ni double couverture.
Un DDA rationnel entier calcule les intersections : les divisions sont
limitées à l'initialisation des arêtes ; la boucle par ligne utilise des
additions et une retenue. Les bornes horizontales sont semi-ouvertes
`[gauche,droite)`.

Le chemin PLANAR prépare deux bits de frontière par ligne dans un masque
Chip RAM, aux positions `gauche-1` et `droite-1`. Une passe descendante du
blitter en remplissage exclusif produit le masque plein. Quatre copies
masquées écrivent ou effacent les bits couleur dans PF2. Le clear du masque,
le remplissage et les copies sont limités au rectangle utile, arrondi aux
mots de 16 pixels. À gauche de l'écran, la frontière négative est omise et
la retenue sort naturellement de la ligne.

Ce chemin utilise directement les registres, sous le même protocole
`OwnBlitter` / `Forbid` que les lignes existantes, avec les interruptions
actives. Il n'appelle pas les routines de dessin de `graphics.library`.
Le comportement de remplissage suit le
[Hardware Reference Manual, section Area Fill Mode](https://www.theflatnet.de/pub/cbm/amiga/AmigaDevDocs/hard_6.html#6-5).

Le chemin PIXEL écrit les bandes en ASM 68020 : alignement sur quatre octets,
boucle déroulée de 32 octets, puis fin en longwords et octets. Kalms reste
le convertisseur C2P par défaut à chaque `flip()`, sur tout le buffer chunky.
Il n'y a pas de conversion alternée ni de tuiles sales dans ce sample.

Le propriétaire préalloue un masque de 8 Kio en Chip RAM et environ 1 Kio
de bornes de lignes. Le worker n'alloue rien, ne possède aucun DMA et garde
ses petits arguments sur sa pile. Les commandes de triangles partagent la
file bornée de 16 commandes. ESC reste traité entre les lots de blitter ;
tous les DMA terminent avant de restituer le blitter et libérer le masque.

## Vérification

- `gmake drawing-test` : ASan/UBSan, 300 triangles dans les deux orientations
  comparés à un oracle indépendant par fonctions de bord, clipping,
  dégénérescences, guards et appels O0/O1/gardés avec registres volatils détruits.
- `gmake triangle-asm-test` : 2 048 bandes exécutées réellement dans Musashi,
  couvrant couleurs, alignements, fins de lignes et intégrité de l'ABI C.
- `gmake solid-cube-test` : 250 frames par version, normales, faces visibles,
  projection et éclairage confrontés à un calcul flottant indépendant ; absence
  de trous dans la silhouette et images PLANAR/PIXEL identiques. Les émissions
  directes et GNU O1/gardées passent la même trace native.
- `gmake miga80-demo-adf-fs-uae-graphics` : pixels réellement relus dans les
  deux playfields, y compris remplissage matériel, clipping et effacement.
- `gmake solid-cube-fs-uae solid-cube-chunky-fs-uae` : dix secondes, double
  buffering, ESC, relance, retour au source et absence de croissance mémoire.
- `gmake release-fs-uae` : les six démos se chargent via le sélecteur réel.

Les résultats FS-UAE décrivent un A1200 PAL, 2 Mio Chip et sans Fast RAM.
Les mesures sur Amiga physique restent à confirmer.

### Mesure FS-UAE avant ajout de la musique

| Version | Frames / durée | Cadence moyenne | C2P moyen par frame |
| --- | --- | --- | --- |
| PLANAR, dernier passage complet | 93 / 10,02 s | 9,3 images/s | Aucun pour le cube |
| PIXEL, deux passages complets | 78 / 10,10 s et 78 / 10,08 s | 7,7 images/s | 48,91 ms et 48,82 ms |

Rapports bruts : `build/reports/SOLIDTEST-fs-uae.txt` et
`build/reports/SOLIDPIXELTEST-fs-uae.txt`. Ces chiffres mesurent le pipeline
complet des samples dans l'émulateur, pas le débit isolé du polygoniseur.
