# Animage — Plan de prototype

> **Note ajoutée après implémentation.** Ce plan reste la spécification et n'est
> pas réécrit. Le code s'en écarte délibérément sur plusieurs points — le
> compositing est sur le CPU et non en QRhi, le max-flow est écrit ici plutôt que
> pris dans Boost, les cels ne sont pas des PNG (un PNG 16 bits ne peut pas
> contenir un half-float sans perdre des pixels), et la prédiction du stylet est
> refusée et non reportée. Chacun de ces écarts est un jugement, avec ses raisons
> et ses mesures, et ils sont listés en anglais dans
> [../handover.md](../handover.md) — section « What is not what the plan asked
> for ». Lire les deux : celui-ci dit ce qui était voulu, l'autre ce qui a été
> fait et pourquoi.

**Objectif du prototype** : valider les deux paris du projet — le modèle de
données (calques communs à la track, timing porté par l'image) et le calque
CTG — sur une seule plateforme, avec une vraie tablette, avant d'écrire du code
qu'on regrettera.

**Hors périmètre** : mobile, son, export vidéo, modes de fusion, gestion de
couleur avancée, multi-track. Tout ça est reportable sans casser
l'architecture.

**Cible** : Windows + Linux (macOS gratuit si Qt). Desktop uniquement.

---

## 1. Choix techniques

| Domaine | Choix | Raison |
|---|---|---|
| Langage | **C++20** | |
| Shell / UI / entrée stylet | **Qt 6** (`QTabletEvent`) | Gère déjà WinTab, Windows Ink, NSEvent et XInput2. C'est le risque numéro un du projet, et Qt l'élimine au jour 1. |
| Rendu GPU | **QRhi** (inclus dans Qt 6) | Abstraction sur Vulkan / Metal / D3D11 / OpenGL. Une dépendance de moins. |
| Max-flow (CTG) | **Boost Graph Library**, `boykov_kolmogorov_max_flow` | Boost Software License, compatible GPL. ⚠️ **Ne pas utiliser le code original de Kolmogorov** : sa licence est restreinte à la recherche non commerciale. |
| Sérialisation | JSON + PNG dans un dossier | Lisible, diffable, débogable à la main |
| Licence | **GPL-3.0** | Mobile n'étant plus prioritaire, la question de l'App Store disparaît |

**Alternative Rust + wgpu** : plus agréable à écrire et plus sûr à long terme,
mais l'écosystème d'entrée stylet y est nettement plus faible (`winit` ne couvre
pas WinTab). Il faudrait écrire le shim d'entrée soi-même — c'est-à-dire
commencer par la partie la plus risquée du projet. Réévaluable après le
prototype, quand le modèle de données sera figé et portable.

---

## 2. Structures de données

### Le cœur

```cpp
using CelId   = uint64_t;   // compteur monotone, JAMAIS réutilisé
using ImageId = uint64_t;
using LayerId = uint64_t;
using TrackId = uint64_t;

enum class LayerKind { Raster, CTG };

struct Layer {                       // propriétés seulement — aucun pixel
    LayerId  id;
    QString  name;
    float    opacity   = 1.0f;
    bool     visible   = true;
    bool     locked    = false;
    LayerKind kind     = LayerKind::Raster;
    LayerId  ctg_source = 0;         // pour un CTG : le calque de trait servant de barrière
};

// « Track » = une pile de calques avec son propre temps. « Timeline » ne
// désigne que l'axe temporel commun à la scène, et le panneau qui l'affiche :
// une scène a plusieurs Track et une seule timeline.

struct Image {                       // une "case" = une colonne de la track
    ImageId  id;
    QHash<LayerId, CelId> cels;      // sparse : absent = calque vide sur cette image
    std::optional<QColor> marker;
};

struct Track {
    QVector<Layer>   layers;         // ordre = ordre de composition, index 0 au-dessus
    QVector<ImageId> slots;          // le temps ; exposition = ImageId répété
    QHash<ImageId, Image> images;
};

struct Scene {
    int framerate = 24;
    int width = 1920, height = 1080; // le canevas : le rectangle exporté
    QVector<Track> tracks;           // ordre = groupes empilés
};
```

**Le canevas.** La surface de dessin n'a pas de bords : les tuiles sont creuses
et leurs coordonnées sont signées, et c'est voulu — un rough déborde. Mais le
`Scene` porte quand même un rectangle, le seul du modèle : c'est ce qui définit
« l'image » — ce que montre le cadre, ce qui borne un remplissage CTG, et ce que
M5 écrit dans un fichier. Sans lui, chaque image exportée aurait la taille de sa
propre boîte englobante et deux images ne feraient jamais la même taille.

Le remplissage CTG est donc découpé au canevas : une forme à cheval sur le bord
est coloriée jusqu'au bord et pas au-delà. Ce qui est hors canevas n'est pas
dans l'image, donc il n'y a rien à colorier dehors.

**Les trois invariants à tester en permanence :**

1. Un `CelId` n'est jamais réutilisé, même après suppression. Sinon l'undo casse.
2. `slots` peut contenir plusieurs fois le même `ImageId` — c'est l'exposition.
   Aucun code ne doit supposer l'unicité.
3. Ajouter un calque ne touche à aucune `Image`. Les cels sont créés
   paresseusement au premier trait.

### Stockage raster

```cpp
constexpr int TILE = 128;

struct Tile {
    std::atomic<int> refcount;
    std::array<half, TILE*TILE*4> rgba;   // RGBA linéaire 16 bits
};

struct Cel {
    CelId id;
    QHash<QPoint, TileRef> tiles;   // sparse : pas de tuile = transparent
    int refcount;                   // nombre d'Image qui le référencent
};
```

Copy-on-write : avant d'écrire dans une tuile de `refcount > 1`, on la clone.
C'est ce qui rend le duplicate-link et l'undo gratuits tous les deux.

### Undo

```cpp
struct TileSnapshot { CelId cel; QPoint coord; TileRef old_tile; };

struct Command {
    QString label;
    QVector<TileSnapshot> tiles;    // pour les opérations de dessin
    QVector<StructOp>     ops;      // pour les opérations de track
};
```

Un trait ne stocke que les handles des tuiles remplacées — typiquement 4 à 20
pointeurs, pas une image. La pile d'undo compte comme une référence sur les
`Cel`, donc rien n'est libéré tant qu'on peut revenir en arrière.

### Espace colorimétrique

Tout est stocké et composé en **RGB linéaire, half-float 16 bits**, primaires
sRGB pour le prototype (élargissables plus tard sans changer le format). La
conversion vers l'écran se fait au tout dernier moment, dans le shader.

L'interface (roue chromatique, dégradés, mélange de teintes) travaille en
**OKLab**, converti à l'entrée et à la sortie. Deux fonctions d'une vingtaine de
lignes, aucune dépendance.

C'est le seul choix de ce document qui est vraiment pénible à changer plus tard :
passer de 8 bits sRGB à 16 bits linéaire après coup oblige à réécrire tous les
shaders et à convertir tous les fichiers existants. Le faire maintenant coûte
une journée.

---

## 3. Le calque CTG

**Principe** : un calque CTG ne stocke pas des pixels de couleur, il stocke des
**scribbles**. Le résultat est régénéré à la demande.

```cpp
struct Scribble {
    QColor color;      // l'étiquette
    bool   hard;       // false = soft scribble (λ = 0.95), le cas normal
    QVector<QPolygonF> strokes;
};

struct CtgCel {
    QVector<Scribble> scribbles;
    uint64_t          cache_hash;   // hash(scribbles + source) → invalidation
    TileGrid          cached_fill;
};
```

### Algorithme (LazyBrush, Sýkora et al. 2009)

Entrée : l'image en niveaux de gris `I` du calque source (le trait), les
scribbles.

```
K = 2 * (largeur + hauteur)                    // périmètre : borne haute

// 1. Prétraitement — obligatoire pour le crayon, optionnel pour le trait net
I_f = 1 - max(0, s * LoG(I))                   // Laplacien de Gaussienne
I'  = map(I_f, [0,1] -> [1, K])                // JAMAIS zéro, jamais K sur le trait

// 2. Graphe
//    arête pixel-pixel (4-connexité)  : poids I'_p
//    arête pixel-terminal couleur     : poids (1 - λ) * K
//      λ = 1.0  hors scribble  -> poids 0 -> arête omise (graphe très creux)
//      λ = 0.95 scribble soft
//      λ = 0.0  scribble dur

// 3. Multiway cut glouton
répéter:
    étiqueter les régions ne contenant qu'une seule couleur (cas triviaux)
    si plus de couleur active: fin
    choisir la couleur c avec la plus grande aire estimée
    source = pixels de c, puits = pixels des autres couleurs
    max_flow_min_cut(graphe restreint aux pixels non étiquetés)
    étiqueter en c le côté source
```

**Deux détails qui font toute la qualité du résultat**, et qu'on rate facilement :

- le poids de lissage n'est **jamais nul** quand les étiquettes diffèrent —
  sinon les régions bordées de noir pur se déconnectent et laissent des trous ;
- le poids sur les pixels blancs doit dépasser la longueur du plus long trait de
  l'image — c'est ce qui empêche une frontière de couper à travers le vide.

**Performance.** Traiter les couleurs de la plus grande zone à la plus petite (le
fond en premier) réduit massivement les sous-problèmes dès les premières
itérations. Le papier mesure de 3× à 18× plus rapide que l'α-expansion pour une
énergie équivalente ; environ 0,6 s pour 0,5 Mpix sur un CPU de 2009.

Pour l'interactivité : calculer en résolution ½ pendant que l'utilisateur trace
le scribble, pleine résolution en tâche de fond au relâchement du stylet.
Recalculer uniquement quand un scribble ou le calque source change.

### Ce qu'il faut tester dans le prototype

C'est là qu'est la vraie question ouverte. Trois hypothèses à valider avec de
vrais dessins :

1. **Multi-source.** Autoriser plusieurs calques comme barrière (rough + clean)
   règle-t-il les fuites que TVPaint ne règle pas ?
2. **Onion fill.** Un scribble souple étalé sur 3-4 intervalles superposés
   colorie-t-il tout d'un coup de façon fiable ? Le papier le montre ; votre
   modèle de calques communs le rend presque gratuit. Si ça marche, c'est
   l'argument de vente du logiciel.
3. **Propagation temporelle.** Reporter automatiquement les scribbles de l'image
   N sur l'image N+1. Le papier cite ça comme travail futur — c'est le terrain
   où il y a de la place pour faire mieux que TVPaint.

---

## 4. Jalons

### M0 — Test de latence (1 semaine) · **avant tout le reste**

Une fenêtre, un `QTabletEvent`, un trait à l'écran. Rien d'autre.

Mesurer la latence stylet → pixel avec une caméra à 120 fps ou plus (filmer
l'écran et la pointe, compter les images). Refaire sur Windows avec WinTab, sur
Windows avec Windows Ink, et sur Linux.

Critère de passage : **sous 25 ms**. Au-delà, l'outil sera désagréable quoi qu'on
mette autour, et il faut régler ça avant d'avoir 50 000 lignes de code qui
supposent l'architecture de rendu actuelle.

### M1 — Le cœur, sans interface (2-3 semaines)

Les structures ci-dessus, les tuiles COW, l'undo, en bibliothèque pure avec des
tests unitaires. Pas de fenêtre.

Tests qui doivent passer :

- dessiner sur une image exposée 3× modifie les 3 positions ;
- copier-coller une image produit des cels indépendants ;
- ajouter un intervalle n'alloue aucune tuile ;
- supprimer l'image 3 puis annuler un trait fait sur l'image 5 qui lui était liée
  restaure le bon contenu ;
- ajouter un calque sur une track de 500 images est en O(1).

Ce jalon est le vrai test du modèle de données. S'il est propre ici, tout le
reste est de la mécanique.

### M2 — Dessiner (2-3 semaines)

Un canevas, un pinceau raster avec pression, une gomme, composition GPU des
calques d'une image. Une seule track, pas encore de timeline visible.

### M3 — Animer (3-4 semaines)

La timeline à l'écran, exposition par étirement, ajout d'intervalles, onion skin
(en comptant les `ImageId` distincts), lecture en boucle, et **dessiner pendant
la lecture** — ce dernier point force le découplage des threads entrée / lecture,
autant le faire maintenant que le rétrofitter.

### M4 — Le calque CTG (3-4 semaines)

L'implémentation LazyBrush ci-dessus, plus les trois hypothèses à tester.

### M5 — Sortir (1-2 semaines)

Export séquence PNG 16 bits, un dossier par calque, nomenclature
`{track}_{calque}_{image:04}.png`. Sauvegarde du projet : un dossier avec
`scene.json` + un PNG par cel. Autosave par simple réécriture atomique.

À ce stade, un animateur peut faire un plan complet et l'emmener dans un
compositeur. C'est le premier moment où le logiciel est utile à quelqu'un
d'autre que vous — et donc le premier moment où vous aurez des retours qui
valent quelque chose.

---

## 5. Sur la question du brevet

Deux choses distinctes, souvent confondues :

- **Le code source** de TVPaint n'a aucune importance ici. Vous
  réimplémentez à partir d'un article publié, ce qui est parfaitement légal et
  n'est pas de la rétro-ingénierie.
- **Un brevet**, lui, ne protège pas un texte mais une *invention*. Publier un
  algorithme ne donne pas le droit de le pratiquer s'il est breveté par ailleurs.
  C'est contre-intuitif, mais publication ≠ licence.

**Ce que j'ai cherché** : aucun brevet au nom de Sýkora, Dingliana ou Collins
portant sur LazyBrush n'apparaît. L'article est cité comme *art antérieur* dans
des brevets tiers — ce qui est plutôt bon signe, un brevet antérieur des auteurs
apparaîtrait dans les mêmes citations. Réserve honnête : une recherche négative
n'est pas une preuve d'absence, et je ne peux pas faire une vraie recherche de
liberté d'exploitation.

**Ce qui règle la question en pratique** : **Krita implémente déjà LazyBrush**,
sous le nom de *Colorize Mask*. Implémenté par Dmitry Kazakov à partir de 2016,
livré par défaut depuis Krita 4.0, dans un projet GPL largement diffusé, depuis
huit ans, sans incident. **OpenToonz** a le même sujet ouvert dans son tracker.

Conséquence directe et utile : allez lire l'implémentation de Krita. Elle est
sous GPL, donc réutilisable dans un projet GPL, et elle a déjà résolu les
problèmes d'optimisation que le papier laisse en exercice — notamment
l'option « limiter aux bornes du calque », qui accélère beaucoup en restreignant
le graphe à la zone non vide.

---

## Sources

- [LazyBrush — texte intégral, page de D. Sýkora (CTU Prague)](https://dcgi.fel.cvut.cz/home/sykorad/Sykora09-EG.pdf)
- [LazyBrush — Computer Graphics Forum, Wiley](https://onlinelibrary.wiley.com/doi/10.1111/j.1467-8659.2009.01400.x)
- [Krita — Colorize Mask, manuel de référence](https://docs.krita.org/en/reference_manual/tools/colorize_mask.html)
- [Krita — implémentation initiale de Colorize Mask (KDE Phabricator D2400)](https://phabricator.kde.org/D2400?id=6596)
- [OpenToonz — issue #420, algorithme Lazybrush / Colorize Mask](https://github.com/opentoonz/opentoonz/issues/420)
- [LazyBrush — plugin TVPaint (site officiel)](http://lazy-brush.com/)
