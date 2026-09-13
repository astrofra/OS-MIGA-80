# Point de reprise — dessin Lua et workflow

Mise à jour : 13 septembre 2026, après reprise de la pause avant extinction.
Les modifications sont enregistrées dans le workspace, sans commit.
Cette étape est terminée et l’ADF reconstruit est validé sous FS-UAE.

L’étape suivante est maintenant réalisée : [cube Lua animé, trigo fixe et
double buffer](MIGA-80-cube-animation.md). Ce document conserve le bilan de
l’étape de dessin précédente ; le lien ci-dessus donne le point de reprise actuel.

## Implémentation

- `layer(PLANAR)` / `layer(PIXEL)`, `pset` et
  `line(x0,y0,x1,y1,color)` fonctionnent dans le compilateur et le runtime.
- PIXEL : tampon chunky 256 × 256, Bresenham CPU, C2P vers PF1 devant.
- PLANAR : quatre bitplanes en Chip RAM, points CPU, lignes via registres du
  blitter, publication vers PF2 derrière. Aucun appel graphics.library pour
  tracer chaque ligne.
- Le propriétaire vide des lots de 16 commandes : OwnBlitter / WaitBlit initial,
  Forbid, programmation et attente DMA directes, restauration des bits DMA
  modifiés, DisownBlitter / Permit. Les interruptions restent actives pour ESC.
  La prise de contrôle exclusive de tout l’OS pendant le programme reste une
  étape ultérieure.
- ESC préemptif et Ctrl-Q pour quitter sont conservés ; les ressources du worker
  restent détenues et libérées par le propriétaire après son arrêt.
- Exemple livré : `assets/demo/layers.lua`, copié dans `DATA/LAYERS.LUA` sur ADF.

Description et commandes : [primitives de dessin](MIGA-80-drawing-primitives.md).
Fichiers centraux : `src/graphics/drawing.{c,h}`,
`src/demo/drawing_host.{c,h}`, `src/demo/drawing_bridge.S`, `src/demo/main.c`.

## Problème de palette résolu

Le défaut visible PF2 provenait de la liste Copper du profil Kickstart 3.0 :
BPLCON3=`1c40` (offset PF2 128), BPLCON4=`1011` (XOR palette 16), malgré des
ColorMap / VideoControl corrects en lecture. Les hashes et ReadPixel ne
pouvaient pas détecter cette mauvaise sélection de couleurs.

L’écran original à huit plans est conservé. `install_display_palette()` ajoute
une UCopList à la ligne zéro du viewport, après les chargements de palette OS :
BPLCON3=`1040` (offset PF2 16), BPLCON4=`0011` (XOR zéro). RethinkDisplay fusionne
la liste ; CloseScreen la libère. Les expériences avec deux descripteurs 4+4
ont été retirées, ainsi que l’écriture temporaire `MIGA80:COPPER.BIN`.

Les bleus et l’orange du motif de lignes ont été inspectés visuellement dans
FS-UAE. `GRAPHICSTEST` vérifie désormais la liste réellement installée à chaque
source/résultat/retour : mode huit plans dual, priorité PF1, offset PF2, XOR,
et deux nibbles RGB des 32 couleurs. Ce contrôle est propre à l’écran actuel
sans effets de palette par ligne ; il faudra l’étendre avec ces futurs effets.

## Vérifications

- `gmake check` a passé avant les dernières corrections de palette ; compilateur
  et cœur de dessin inchangés depuis. Log :
  `build/reports/drawing-checkpoint-check.log`.
- Hôte ASan/UBSan : 625 combinaisons de pentes/octants/égalités, 20 000 cas de
  clipping extrêmes, couleurs et coordonnées invalides.
- Musashi O0/O1/guarded, encodeurs direct/GNU : trace `996c9cc4`, PIXEL
  `003971a5`, PLANAR `4235248a`, 19 lignes, borne de pile 1108.
- GRAPHICSTEST final supervisé : trois cycles validés avec contrôles Copper,
  couleurs, géométrie, retour source, Ctrl-Q et libération des ressources.
- Contrôles négatifs hôte sur des dumps réels : l’ancienne liste est rejetée,
  la liste corrigée acceptée ; offset PF2, XOR ou RGB incorrects sont rejetés.
  Rapport : `build/reports/display-palette-negative-controls.txt`.
- GRAPHICSTEST NOSUPERVISOR final : les trois cycles passent également, avec
  les mêmes contrôles Copper et les mêmes checksums que le mode supervisé.
- STOPTEST final : 13 arrêts validés, dont une boucle infinie de lignes PLANAR
  réellement exécutées. Retour source, touches maintenues, libération des
  signaux, mémoire stable, relance de Mandelbrot et fermeture passent.
- Mandelbrot inchangé : 464 octets guarded, code `72eef2e6`, pixels `c4604fc7`,
  budget restant 839712, borne de pile 96. Vue source : `422c03c3`.

Rapports : `build/reports/drawing-host.txt`,
`build/reports/source-view-adf-graphics-fs-uae.txt`,
`build/reports/source-view-adf-graphics-direct-fs-uae.txt`,
`build/reports/source-view-adf-stop-fs-uae.txt`.
Capture corrigée : `build/reports/drawing-palette-corrected.png`.
ADF : `build/distribution/miga80-source-view.adf`.

Les tests ADF partagent `build/fs-uae-demo-adf` : les exécuter successivement,
sans modifier leur script pendant qu’il tourne. Aucun retour physique A1200
n’a encore été confirmé ; les mesures de performance restent ouvertes.
