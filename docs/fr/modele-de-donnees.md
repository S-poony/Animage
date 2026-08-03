# Animage — Modèle de données

> Corrigé après relecture. Le document d'origine était cohérent : c'est ma lecture qui était fausse.

## 1. Structure

```
Scene {
  framerate: Framerate            // parmi presets
  timelines: [Timeline]           // l'ordre = l'ordre de composition
  audio_tracks: [AudioTrack]
}

Timeline {
  id
  layers: [Layer]                 // PARTAGÉS par toutes les images
  slots:  [ImageId]               // la timeline dans le temps ; l'exposition
                                  // = plusieurs slots pointant sur le même ImageId
}

Layer {                           // propriétés seulement, aucun pixel
  id, name
  opacity, blend_mode, visible, locked
  kind: Raster | CTG
}

Image {                           // une "case" de la timeline = une colonne
  id
  cels: Map<LayerId, CelId>       // sparse : pas d'entrée = calque vide ici
  marker: Option<Color>
}

Cel {                             // le seul objet qui contient des pixels
  id
  tiles: SparseTileGrid           // tuiles 64×64 ou 128×128, copy-on-write
  refcount
}
```

## 2. Ce que ce modèle implique

**Le calque appartient à la timeline, pas à l'image.** Ajouter un calque l'ajoute
à toutes les images d'un coup. Changer son opacité change l'opacité de tous les
dessins de la timeline. C'est exactement l'exemple du document (rough → baisser
l'opacité → nouveau calque → clean). Le panneau "Image" à droite n'est qu'un
endroit dans l'UI, pas une indication d'appartenance.

**Le timing est porté par l'image, pas par le calque.** C'est *la* différence
avec TVPaint et Toon Boom, où chaque calque a son exposition propre. Ici, si
l'image est exposée 3 fois, rough / TD / clean / colo sont tenus 3 fois ensemble.

C'est une contrainte volontaire, et à mon avis un bon choix : la désynchronisation
des expositions entre calques est une source classique de bugs de production dans
TVPaint (on modifie le clean sans voir que le rough en dessous a une autre
découpe). L'échappatoire — "si les timings sont indépendants, fais une autre
timeline" — est cohérente et lisible.

Le prix à payer, à connaître :

- Un décor fixe derrière un perso animé doit vivre dans sa propre timeline.
- Comme les timelines empilent en blocs, on ne peut pas *intercaler* un calque
  d'une timeline entre deux calques d'une autre. Si le bras d'un perso doit
  passer devant un objet qui a un timing différent, il faut découper le perso en
  deux timelines. C'est le comportement normal des groupes, mais ça se dit dans
  la doc utilisateur.

**L'exposition (duplicate-link) est une égalité d'ImageId, pas une copie.**

| Opération | Effet sur `slots` | Effet sur les `Cel` |
|---|---|---|
| Étendre l'exposition | `[A, B]` → `[A, A, B]` | aucun |
| Ajouter un intervalle | `[A, B]` → `[A, N, B]` | crée `N` avec des cels vides |
| Ctrl+C / Ctrl+V | `[A, B]` → `[A, A', B]` | copie profonde : nouveaux CelId |
| Supprimer une image | retire le slot | décrémente refcount, GC si 0 |

Dessiner sur un calque d'une image exposée modifie le `Cel` partagé, donc les
deux positions dans la timeline changent. C'est le comportement demandé, et il
sort tout seul de la structure — aucun cas particulier à coder.

**Onion skin.** Parcourir les slots en arrière et en avant en collectant les
`ImageId` **distincts**. Une image exposée 5 fois compte pour 1 automatiquement.
Un onion skin par timeline, comme spécifié.

## 3. Ordre de rendu

Confirmé : empilement plat, timelines vues comme des groupes.

```
Scene
├── Timeline 1        →  calque 1   (dessus)
│                        calque 2
└── Timeline 2        →  calque 3
                         calque 4   (dessous)
```

À prévoir quand même dans la structure, même si l'UI ne l'expose pas en v1 :

- une opacité et un mode de fusion **au niveau timeline** (le groupe), pas
  seulement au niveau calque — sinon impossible de faire fondre un perso entier ;
- un décalage temporel (offset) par timeline, pour recaler deux persos sans
  réexposer toutes les images à la main.

## 4. Undo — vous avez raison, ce n'est pas un problème sémantique

Je l'avais mal formulé. Le command pattern marche, et les cels partagés ne
créent aucune ambiguïté : annuler un trait restaure le contenu du cel, donc les
N images qui le référencent redeviennent correctes toutes ensemble. C'est le
comportement attendu.

La vraie difficulté est de **mémoire, pas de logique** — et elle disparaît si les
tuiles sont en copy-on-write :

- Rejouer les commandes depuis le début est impossible en raster (recomposer
  3000 traits pour reculer d'un cran).
- Donc chaque entrée d'undo doit stocker un **état**, pas seulement une action.
- Avec des tuiles COW, cet état = la liste des handles de tuiles remplacées.
  Un trait touche 4 tuiles → l'undo coûte 4 pointeurs, pas une image entière.

Deux points de vigilance, mineurs :

1. **Refcount et undo.** Supprimer une image décrémente le refcount d'un cel qui
   peut tomber à 0. Ne pas libérer immédiatement : le cel doit survivre tant
   qu'une entrée d'undo le référence. Le plus simple est que la pile d'undo
   compte comme une référence.
2. **Ordre des opérations mixtes.** Dessiner sur l'image 5 (liée à la 3), puis
   supprimer l'image 3, puis annuler le trait. Ça marche si et seulement si les
   `CelId` sont stables et jamais réutilisés. Utiliser un compteur monotone, pas
   un index de tableau.

Donc : garder le command pattern, mais concevoir les tuiles COW **avant** l'undo,
pas l'inverse.
