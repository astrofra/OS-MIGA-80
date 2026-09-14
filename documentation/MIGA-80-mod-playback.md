# Lecture des MOD depuis Lua

Les deux samples `cube-solid.lua` et `cube-solid-chunky.lua` jouent maintenant
`93_10_12_A_SYNTH_1.mod` pendant leur animation. Le fichier fourni dans
`works/mods/` est copié sans modification dans `SYS:mods/` sur l'ADF de référence.
Son titre interne est `a_synth_1`, signé « by erk 12/10/93 » dans le premier
instrument : 4 voies, 8 positions, 4 patterns et 14 912 octets de samples.

## API

```lua
music_play("SYS:mods/93_10_12_A_SYNTH_1.mod")
music_mute(3)
music_mute(0)
music_stop()
```

- `music_play("chemin")` démarre ou redémarre le morceau depuis le début.
  Le chemin doit être une chaîne littérale ; chaque chemin référencé est
  préchargé avant de lancer le code natif, même dans une branche non exécutée.
  Utiliser `SYS:mods/...` pour les démos distribuées. Un chemin relatif suit
  le répertoire courant du processus AmigaDOS, pas celui du fichier Lua.
- `music_stop()` coupe la lecture et le DMA audio. Il est aussi appelé
  automatiquement à la fin du programme, sur ESC, sur faute ou erreur.
- `music_position()` retourne un `i32` : `ordre*64 + ligne`, indices à partir
  de zéro, ou `-1` à l'arrêt. Il s'agit de la **prochaine ligne à lire** du
  replayer, pas d'une horloge ni du numéro de pattern dans le fichier.
- `music_mute(masque)` prend un `u8` ; les quatre bits bas coupent respectivement
  les voies 0 à 3. `15` coupe les quatre voies, `0` les rétablit, les autres
  bits sont ignorés. Le morceau continue d'avancer pendant ce mute.

Il n'y a qu'un morceau actif ; jouer un autre fichier arrête le précédent.
Chaque démarrage restaure les données originales et les mémoires d'effets.
Sans `music_play` dans le source, aucune ressource audio n'est réservée ; stop
et mute sont alors sans effet, et position retourne `-1`.
Les cubes gardent leur durée de dix secondes et leur affichage sur 30 lignes.

## Lecteur et ressources

Le runtime utilise [ptplayer 6.4 de Frank Wille](https://aminet.net/package/mus/play/ptplayer),
en assembleur 68000, avec sa variante compatible AmigaOS. Il réserve les
quatre voies auprès d'`audio.device` à priorité minimale, et les timers A/B
auprès de `ciab.resource`. Une allocation impossible produit une erreur dans
l'éditeur ; le programme ne vole pas les voies à une autre application.

Le CIA A cadence les ticks (125 BPM par défaut, 50 ticks/s en PAL, commandes
Fxx de vitesse et tempo conservées). Le CIA B réalise les deux étapes différées
nécessaires au démarrage DMA et aux boucles de samples. Les samples sont en
Chip RAM et Paula joue par DMA ; aucune synthèse ni conversion audio n'est
faite dans `flip()`. Les interruptions restent actives pendant les lots blitter
et la conversion Kalms. La musique conserve donc son tempo à faible cadence
vidéo. Le jingle de démarrage reste distinct et ne se rejoue pas avec les cubes.

Le propriétaire du runtime charge et valide les fichiers, ferme les handles
DOS, puis réserve l'audio avant de lancer le worker. Les appels Lua play/stop/mute
soumettent une commande bornée au propriétaire et attendent son acquittement.
Le worker ne possède ni allocation, ni fichier, ni timer. ESC peut le retirer
pendant cette attente ; le propriétaire annule ensuite les timers et le DMA,
retire les interruptions, restitue l'audio et libère les buffers. Une interruption
CIA B différée ne peut pas redémarrer un sample après sa libération.
Les masques sont manipulés via [AbleICR](https://d0.se/autodocs/cia.resource/AbleICR)
et les requêtes en attente via [SetICR](https://d0.se/autodocs/cia.resource/SetICR),
sans lire directement le registre ICR partagé avec AmigaOS.
Le bit de filtre audio antérieur est restauré au retour à l'éditeur.

L'[adaptation du lecteur](../third_party/ptplayer/README.md) réserve notamment
un mot silencieux en Chip RAM, sans supposer que l'adresse zéro contient du
silence. Les sources upstream et leur licence Public Domain sont conservées.

## Format accepté et limites actuelles

Le chargeur accepte les MOD ProTracker à 31 instruments et quatre voies,
signatures `M.K.`, `M!K!`, `4CHN`. Il vérifie le header, les 128 entrées d'ordre,
les bornes des patterns et samples, finetunes/volumes, boucles, numéros
d'instruments, périodes et sauts. Les notes doivent rester dans 113..856.
Les samples de longueur zéro ou un mot sont normalisés comme instruments vides
sur la copie de lecture, en conservant correctement les offsets du fichier.

Limites de cette version : 256 Kio par fichier, 512 Kio au total pour les
originaux et copies de lecture, au plus 32 ressources littérales (pool partagé
avec les autres constantes). Les données tronquées ou supplémentaires sont
refusées. L'effet 8 de panning est refusé explicitement ; il n'existe pas de
panoramique logiciel sur les quatre voies Paula. Le replayer conserve ses
autres effets ProTracker, dont les arpèges, portamentos, vibrato/volume slide,
retrigger et changements de vitesse employés par ce morceau.

La compatibilité 1.0 de la roadmap reste ouverte : cette intégration ne remplace
pas le corpus de référence de chaque effet et de leurs interactions, ni les
tests sur Amiga physique. Les formats 15 instruments, multivoies, XM, S3M et IT,
l'import graphique de morceaux et l'édition tracker ne sont pas implémentés.

## Vérification

`gmake music-test` valide tous les préfixes tronqués du morceau, 30 000 mutations,
les erreurs de bornes et les instruments vides sous ASan/UBSan. Il vérifie les
types et l'ordre des appels Lua, puis exécute les six émissions natives
O0/O1/gardée, directe/GNU, avec destruction volontaire des registres volatils.

`gmake solid-cube-fs-uae solid-cube-chunky-fs-uae` vérifie en A1200 PAL / 2 Mio
Chip / sans Fast : échec propre si l'audio est occupé, erreur de fichier absent,
play/mute/stop/redémarrage, notes/effets avant le premier instrument,
absence de DMA après arrêt, trois interruptions ESC
et deux exécutions complètes par cube. Les ticks CIA et la position musicale
doivent avancer au même rythme malgré les différences de cadence vidéo ; les
quatre voies DMA doivent être observées. Les relances doivent rendre les
canaux audio, timers, signaux et mémoire disponibles.
Les rapports sont `build/reports/music-host.txt`, `SOLIDTEST-fs-uae.txt` et
`SOLIDPIXELTEST-fs-uae.txt`.

Mesures du dernier passage complet, 2026-09-14 :

| Rendu | Images / durée vidéo | Ticks CIA | Prochaine ligne | Masque DMA observé |
| --- | --- | --- | --- | --- |
| Blitter | 91 / 10.042 s | 506 | 84 | 15 (4 voies) |
| Chunky / Kalms | 77 / 10.102 s | 511 | 85 | 15 (4 voies) |

Le compteur musical démarre à `music_play`, avant le premier `flip` qui fixe
l'origine de l'horloge vidéo : il peut donc dépasser légèrement 500 ticks.
Ces observations vérifient la cadence du replayer et l'activité DMA en
émulation ; elles ne constituent pas une comparaison exhaustive de formes
d'onde avec ProTracker ni une validation sur machine physique.
