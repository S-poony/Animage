# Animage — Tablettes, couleur, licence

## 1. Tablettes : faut-il écrire son propre driver ?

### Ça a déjà été fait, et ça marche

**OpenTabletDriver** (LGPL-3.0, C#/.NET, ~3 900 étoiles, encore actif — v0.6.7
en avril 2026) est un driver **user-mode** multiplateforme qui gère Wacom,
Huion, XP-Pen, Gaomon, VEIKK, UGEE, Parblo, XenceLabs, Genius, UC-Logic,
RobotPen et une trentaine d'autres marques. Windows, macOS, Linux.

Le principe : la tablette est un périphérique **HID**. Le driver ouvre le
périphérique en USB brut, envoie une séquence d'initialisation pour le faire
sortir du mode HID générique et passer en **mode vendeur** — le mode qu'utilisent
les drivers officiels, et le seul qui expose toutes les fonctions (pression,
inclinaison, molettes, touches express). Ensuite il décode les rapports HID
selon un fichier de configuration par modèle.

Autrement dit : ce n'est pas un driver noyau, c'est **du parsing de rapports USB
en espace utilisateur**. Un fichier de config par modèle, écrit par la
communauté, souvent en un aller-retour sur leur Discord.

### Et pourquoi ce serait plus facile pour vous que pour eux

OpenTabletDriver fait deux choses. Animage n'a besoin que de la première :

| | OpenTabletDriver | Animage |
|---|---|---|
| Lire le HID brut et décoder pression/tilt | oui | **oui** |
| Réinjecter ça comme événement système | oui | **non** |

C'est la seconde moitié qui coûte cher. Sur Windows, pour sortir de la pression
vers le système, OTD a besoin d'un pilote de périphérique virtuel (**VMulti**) et
d'un plugin Windows Ink. Une application qui consomme les données directement
n'a rien de tout ça à faire : elle lit, elle dessine.

Donc oui, techniquement, c'est faisable. Et sans doute plus simple que ce que
suggère la taille du projet OTD.

### Les vrais obstacles

- **Le conflit avec le driver constructeur.** OTD documente noir sur blanc le
  symptôme : deux curseurs à l'écran quand un autre driver tourne. Leur solution
  est de faire désinstaller tous les autres drivers — ils fournissent même un
  outil, `TabletDriverCleanup`. Demander à un artiste de désinstaller son driver
  Wacom pour utiliser votre logiciel, c'est perdre l'artiste.
- **Le Bluetooth n'est pas géré du tout** par OTD, et seulement partiellement les
  dongles sans fil. Beaucoup de tablettes récentes sont sans fil.
- **La base de configs ne finit jamais.** Chaque nouveau modèle est un ticket.
  C'est un travail de maintenance perpétuel, financé par Patreon dans le cas
  d'OTD.
- **Mobile : impossible.** Ni Android ni iOS ne donnent l'accès USB HID brut aux
  périphériques d'entrée système, et l'Apple Pencil n'est de toute façon pas un
  périphérique USB. Sur ces plateformes il faut passer par les API système, point.
- **Permissions.** Accès HID privilégié sur Windows, règles udev sur Linux,
  autorisation "Surveillance de la saisie" sur macOS. Chaque OS a son rituel
  d'installation.

### Recommandation

**Ne pas en faire une dépendance. En faire une option.**

1. **Chemin par défaut** — consommer les API standard : WinTab **et** Windows Ink
   sur Windows (les deux, pas l'un ou l'autre : les pros utilisent encore
   massivement WinTab), NSEvent sur macOS, libinput/XInput2 sur Linux,
   MotionEvent sur Android, UITouch/UIPencil sur iOS. Ça marche pour tout le
   monde, avec le driver que l'artiste a déjà installé.
2. **Mode expert optionnel** — un backend HID brut, désactivé par défaut, activé
   par l'utilisateur averti qui veut la latence minimale ou qui a une tablette
   mal servie par son driver officiel. Il vit derrière la même interface interne
   que le chemin standard.

Le point décisif : **l'architecture est la même dans les deux cas**. Une couche
d'entrée abstraite (`InputBackend` → flux de `PenEvent { x, y, pressure, tiltX,
tiltY, timestamp }`) avec une implémentation par plateforme, plus une
implémentation HID. Vous pouvez donc livrer le mode expert plus tard sans rien
casser, ou jamais.

Et si vous allez jusqu'au backend HID : **contribuez à OpenTabletDriver plutôt
que de repartir de zéro**. La base de configurations par modèle est la vraie
valeur du projet, elle représente des années de travail communautaire, et elle
est déjà sous LGPL — compatible avec un projet GPL.

## 2. Couleur : le LAB est une bonne intuition, mais pas au bon endroit

Le problème du LAB comme espace de travail :

- **La composition alpha n'y a pas de sens physique.** `a·C₁ + (1−a)·C₂` en LAB
  ne correspond à aucun mélange réel de lumière. Superposer deux calques
  semi-transparents donne des couleurs fausses, et le flou gaussien aussi.
- **L'accumulation de pinceau est un phénomène lumineux.** Repasser deux fois
  avec une brosse à 50 % doit se comporter comme de la lumière qui s'ajoute.
- **Le LAB n'est pas borné.** Une grande partie de l'espace ne correspond à
  aucune couleur affichable, ce qui complique le stockage, l'écrêtage et l'export.
- **Aucun GPU ne travaille en LAB.** Chaque opération de blending imposerait un
  aller-retour de conversion.

La séparation que font tous les moteurs modernes :

| Couche | Espace | Pourquoi |
|---|---|---|
| Stockage et composition | **RGB linéaire**, 16 bits demi-flottant, primaires larges (Rec.2020 ou ACEScg) | Le blending y est physiquement correct et natif GPU |
| Interface : roue, dégradés, mélange de teintes, palettes | **OKLab / OKLCh** | Perceptuellement uniforme, c'est ce que vous cherchez |
| Affichage | sRGB / Display P3 via transformation d'affichage | Ce que voit l'écran |

**OKLab plutôt que CIELAB** : même objectif de perception uniforme, mais sans le
défaut bien connu du LAB sur les bleus (le fameux virage violet dans les
dégradés). Conversion en quelques lignes, pas de table.

Vous obtenez exactement ce que vous vouliez — un sélecteur où "plus clair" veut
dire plus clair, des dégradés qui ne passent pas par de la boue — sans casser la
composition. Décision à prendre maintenant : le passage en linéaire 16 bits est
très pénible à rétro-installer.

## 3. Licence : GPL, avec une réserve concrète

La GPL est cohérente avec le projet, mais elle **bloque la distribution sur l'App
Store iOS**. Ce n'est pas théorique : Apple a retiré VLC de l'App Store en
janvier 2011, à la demande d'un contributeur qui a fait valoir que la
distribution violait la GPL sous laquelle il avait contribué. Le conflit est
structurel : la GPL exige que l'utilisateur puisse copier et redistribuer
librement le binaire, les conditions de l'App Store imposent une DRM qui
l'interdit. La question n'a jamais été tranchée en justice et ne le sera
probablement jamais, ce qui la rend pire, pas meilleure : le risque est permanent
et hors de votre contrôle — n'importe quel contributeur peut le déclencher.

Le document liste iOS dans les cibles. Il faut donc choisir :

| Option | Conséquence |
|---|---|
| **GPL-3.0 strict** | Pas d'iOS via l'App Store. Android reste possible (F-Droid, et le Play Store est nettement plus tolérant). |
| **GPL + exception App Store** | Une clause additionnelle explicite autorisant la distribution sur les magasins à DRM. Approche retenue par plusieurs projets. Demande que **tous** les contributeurs acceptent la clause dès le départ — donc à écrire dans le `CONTRIBUTING.md` du premier jour. |
| **MPL-2.0** | Copyleft par fichier. Protège le cœur, compatible App Store, permet des greffons propriétaires. |
| **Apache-2.0** | Aucune contrainte, protection brevets explicite. Mais autorise un fork propriétaire de votre travail. |

Si iOS compte, la **GPL-3.0 avec exception App Store** est le meilleur compromis :
vous gardez le copyleft, vous gardez la porte iOS ouverte. À décider avant le
premier contributeur externe, parce que changer de licence ensuite exige l'accord
écrit de chacun d'eux.

Effet de bord favorable : la GPL rend compatible l'usage de **ffmpeg** (export
mkv), de **Qt** sous GPL, et d'**OpenTabletDriver** (LGPL).

---

## Sources

- [OpenTabletDriver — dépôt GitHub (LGPL-3.0)](https://github.com/OpenTabletDriver/OpenTabletDriver)
- [OpenTabletDriver — FAQ générale (pression, conflits de drivers, Bluetooth)](https://opentabletdriver.net/Wiki/FAQ/General)
- [OpenTabletDriver — guide d'ajout d'une nouvelle tablette (mode vendeur)](https://opentabletdriver.net/Wiki/Documentation/ConfigurationGuide)
- [The GPL, the App Store, and you — Engadget](https://www.engadget.com/2011-01-09-the-gpl-the-app-store-and-you.html)
- [Apple removes VLC app from App Store, GPL to blame — TechSpot](https://www.techspot.com/news/41908-apple-removes-vlc-app-from-app-store-gpl-to-blame.html)
