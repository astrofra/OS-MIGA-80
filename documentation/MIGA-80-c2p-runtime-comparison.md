# C2P du runtime : mask32 et Kalms

Le runtime utilise désormais notre assembleur `mask32` pour convertir PIXEL
vers les quatre plans PF1, dans le tampon caché comme lors de la publication
du résultat. Le source Lua, le buffer byte4 de 64 Kio et la conversion complète
256 × 256 restent identiques. PLANAR continue d'utiliser le blitter directement.

Trois choix sont disponibles dans le même exécutable : `C2P=MASK32` (défaut),
`C2P=KALMS` et `C2P=REFERENCE`. Sur l'ADF chunky, depuis le Shell :

```text
MIGA80:MIGA80 MIGA80:DATA/CUBE.LUA RAM:CUBE.TXT CUBE C2P=KALMS
```

Le choix reste actif lors des relances F5. Il ne change pas l'API Lua.
La référence C reste un témoin de correction et de comparaison.

`gmake miga80-cube-chunky-kalms-adf` produit
`build/distribution/miga80-cube-chunky-kalms.adf`, qui sélectionne Kalms au
démarrage. `miga80-cube-chunky.adf` utilise mask32.

## Adaptation Kalms

Source : [Mikael Kalms, c2p1x1_4_c5_bm.s](https://github.com/Kalmalyzer/kalms-c2p/blob/d8ecf79a3325615305dd800ae7704b518e0d9dda/bitmap/c2p1x1_4_c5_bm.s),
révision `d8ecf79a3325615305dd800ae7704b518e0d9dda`.
L'original et sa notice Public Domain sont conservés dans
[`third_party/kalms-c2p`](../third_party/kalms-c2p/README.md).

[`c2p4_kalms.S`](../src/graphics/c2p4_kalms.S) conserve la transposition et
l'ordre des écritures en pipeline de la boucle sans modulo. L'adaptation
porte sur la syntaxe GNU, l'ABI C, les quatre pointeurs de destination et les
pas de lignes. Lorsque la source et les plans sont contigus par ligne, le
wrapper traite toute la surface en un seul pipeline. Les destinations sont
indépendantes ; aucun appel graphics.library, tampon intermédiaire ou LUT.

Le contrat `miga80_c2p4_kalms_color4` exige des octets de couleur **0 à 15**,
une largeur non nulle multiple de 32 et des buffers source/destination valides
sans recouvrement. Les bits hauts ne sont pas masqués. Toutes les écritures
PIXEL du runtime et de son interface respectent déjà ce contrat. Les API
`byte4` existantes continuent d'ignorer les bits hauts arbitraires.

## Vérifications et mesures

```sh
gmake c2p4-asm-test
gmake miga80-cube-c2p-compare
```

Le premier test contrôle les arguments et marges du wrapper avec ASan/UBSan,
puis exécute **80 cas des véritables boucles ASM** dans Musashi : les deux
backends, cinq tailles, avec/sans marges de lignes, noir, blanc, rampe des
16 couleurs et pixels pseudo-aléatoires. Une transposition Python bit par bit
sert d'oracle indépendant ; source en lecture seule, marges de chaque plan,
ordre des bits et registres non volatils sont contrôlés. Le wrapper hôte est
explicitement une référence C, pas une émulation de l'assembleur.
Ce test est inclus dans `gmake check`.

Le test ADF `GRAPHICSTEST` passe aussi avec `C2P=MASK32` et `C2P=KALMS`,
sur trois cycles chacun : mélange de pset/line/effacement dans PF1 et PF2,
clipping et octants, palette Copper, retour source et ressources libérées.
Les checksums restent `003971a5` (PIXEL) et `4235248a` (PLANAR).
Rapports : `build/reports/source-view-adf-graphics-{mask32,kalms}-fs-uae.txt`.

La comparaison FS-UAE exécute successivement la référence, mask32 puis Kalms
avec **le même ADF** : A1200 PAL, accuracy 1, 2 Mio Chip, sans Fast par défaut.
Chaque backend passe trois arrêts ESC puis deux animations de dix secondes,
avec contrôle des pixels PF1, PF2 vide, échanges de buffers, relance F5,
retour source, Ctrl-Q et absence de croissance mémoire.

Les rapports bruts `build/reports/cube-chunky-{reference,mask32,kalms}-fs-uae.txt`
conservent les deux exécutions complètes. Leurs fichiers JSON associés
identifient l'ADF par SHA-256 et la configuration ; l'agrégateur refuse une
comparaison entre ADF ou configurations différents. La synthèse est
`build/reports/cube-c2p-comparison.md`. Une copie de l'ADF mesuré est conservée
dans `build/distribution/miga80-cube-c2p-tested.adf`, indépendamment des
reconstructions ultérieures des disques de démonstration.

Les appels `ReadEClock` encadrent uniquement la conversion de chaque `flip`,
dispatch et validation inclus. Les ticks restent bruts, sans soustraction
de l'overhead du chronomètre. Dessin, effacement, échange des buffers,
publication du résultat et lecture de contrôle sont hors de ce compteur.
Le système et le DMA d'affichage restent actifs : c'est une durée écoulée
dans le runtime hébergé, pas un nombre de cycles CPU exclusifs.

Le compteur C2P inclut la première conversion ; la durée de l'animation
commence à sa première image affichée. Il ne faut donc pas soustraire
directement le total C2P de cette durée. La cadence modifie aussi les
orientations échantillonnées par `time()` : même animation, mais pas une
liste identique de frames entre backends. Les mesures FS-UAE ne certifient
aucune cadence sur un A1200 physique.

Les tuiles dirty ne sont pas implémentées dans cette comparaison.

## Résultats FS-UAE

| Backend | Exécution | Images | Durée (s) | C2P moyenne (ms/image) |
|---|---:|---:|---:|---:|
| Référence C | 1 | 5 | 10,4200 | 2 526,072 |
| Référence C | 2 | 5 | 10,4200 | 2 526,129 |
| Notre ASM mask32 | 1 | 40 | 10,1585 | 179,148 |
| Notre ASM mask32 | 2 | 40 | 10,1595 | 179,091 |
| Adaptation Kalms | 1 | 83 | 10,0994 | 48,838 |
| Adaptation Kalms | 2 | 83 | 10,0193 | 48,424 |

Le coût moyen C2P est réduit d'environ ×14,1 par mask32 par rapport à la
référence, puis de ×3,68 par Kalms par rapport à mask32. Le coût du dessin CPU
et des échanges reste présent, d'où un gain plus faible sur l'animation entière.
La suite générale `gmake check` passe également ; son journal est conservé dans
`build/reports/cube-c2p-check.log`. Les trois profils passent tous les contrôles fonctionnels. Capture du résultat
Kalms : `build/reports/cube-chunky-kalms-fs-uae.png`.

ADF mesuré SHA-256 :
`a876f61e034e6155d52828480b4101e9c3cfddf98a7faf9f2f3ae707e1d6c95a`.

Une alternance de demi-trames pourrait réduire la quantité de C2P par
présentation, au prix d'une mise à jour moins fréquente de chaque zone.
Sur cette mesure Kalms (~123 ms entre présentations, ~49 ms de C2P), enlever
la moitié de la C2P donnerait environ +25 % de cadence dans un modèle sans
surcoût. Ce n'est pas une mesure de ce mode : regroupement des petits blocs,
historique propre à chaque bitmap et éventuelles copies restent à concevoir.
