# LazyBrush et les calques CTG — recherche

## 1. La filiation est confirmée

- **LazyBrush**, Daniel Sýkora, John Dingliana, Steven Collins (Trinity College
  Dublin), *Computer Graphics Forum* 28(2), Eurographics 2009, p. 599–608.
- D'abord distribué comme **plugin commercial pour TVPaint** (lazy-brush.com).
- Intégré nativement à **TVPaint Animation 11 Pro** sous le nom de **calque CTG**
  — "Colors and Textures to Generate". Les propriétaires de TVP 11 Pro n'ont plus
  besoin d'acheter le plugin.

Donc oui : le CTG *est* LazyBrush, plus une couche de gestion de textures
par-dessus (les textures suivent la déformation des zones colorées d'image en
image).

Fait notable pour Animage : le papier remercie le studio **AniFilm** comme
"initiateurs et premiers utilisateurs du système". L'algorithme est né d'une
analyse de besoins menée avec des coloristes professionnels, pas d'une intuition
de chercheur. C'est exactement le type de contrainte qui rend un outil agréable.

## 2. Le problème résolu

Le pot de peinture classique échoue sur le dessin fait main :

- fuites de couleur par les trous du trait ;
- hachures et petites zones qui obligent à cliquer cent fois ;
- il faut boucher les trous à la main, tâche longue et fatigante ;
- l'anticrénelage du scan est détruit au remplissage.

Les approches concurrentes (colorisation de Levin et al. 2004, colorisation manga
de Qu et al. 2006, segmentation de Sýkora et al. 2005) supposent chacune un style
particulier — régions homogènes, motifs répétitifs — et s'effondrent en dehors.

## 3. Le cahier des charges des coloristes

Quatre propriétés, formulées avec les illustrateurs avant toute mathématique :

- **Frontière optimale.** L'outil remplit le maximum de surface en trouvant la
  meilleure frontière possible, *malgré* les trous du trait. L'utilisateur affine
  ensuite avec des traits supplémentaires si besoin.
- **Étiquetage connexe.** Une couleur produit une région d'un seul tenant. Pas de
  remplissage à distance : la localité est essentielle pour le coloriste.
- **Scribbles souples (*soft scribbles*).** Le trait indicatif n'a pas besoin
  d'être placé précisément. Règle de majorité : la région prend la couleur dont
  le trait a la plus grande part de ses pixels à l'intérieur. Par la loi de
  Fitts, élargir un peu le pinceau réduit fortement le temps de visée sur les
  zones fines — c'est là que se trouve le gain de temps réel.
- **Anticrénelage préservé.** La frontière est poussée vers les pixels
  d'intensité **minimale** (le cœur du trait noir), pas vers le gradient maximal
  comme en segmentation d'image classique. C'est ce qui rend la discontinuité de
  couleur invisible.

## 4. L'algorithme

### Énergie

Image en niveaux de gris `I`, pixels `P` en 4-connexité, scribbles `S` de
couleurs `C`. On cherche l'étiquetage `c` minimisant :

```
E(c) = Σ_{p,q ∈ N} V_pq(c_p, c_q)  +  Σ_{p ∈ P} D_p(c_p)
```

### Terme de lissage (où placer la frontière)

```
V_pq = I'_p   si c_p ≠ c_q
     = 0      sinon
```

Les intensités `[0,1]` sont mappées sur `[1, K]` avec **K = 2·(largeur + hauteur)**,
soit le périmètre de l'image. Deux détails qui font tout :

- **jamais zéro** quand les étiquettes diffèrent — sinon les régions à trait noir
  pur ne coûtent rien, se déconnectent et laissent des trous ;
- **K très grand sur le blanc** — au moins la longueur du plus long trait de
  l'image — pour interdire les raccourcis à travers les zones claires. Une
  frontière ne traverse du blanc que s'il n'existe aucun chemin le long du trait.

Pour les dessins au crayon ou à faible contraste, prétraiter par **Laplacien de
Gaussienne** :

```
I_f = 1 − max(0, s · LoG(I))
```

Le LoG correspond au mécanisme *light-over-dark* des premiers étages de la vision
humaine ; ses maxima locaux tombent au centre des traits. Après filtrage,
l'intérieur des régions devient blanc quelle que soit son intensité d'origine, et
le contraste des traits est renforcé. Le résultat de l'étiquetage s'applique
ensuite sur l'image **non modifiée**.

### Terme de données (les scribbles)

```
D_p(c_p) = λ · K
```

| λ | Signification |
|---|---|
| `1` | pixel sans scribble |
| `0` | scribble dur (contrainte absolue) |
| `0.95` | scribble souple (valeur retenue par les auteurs) |

La contrainte formelle est `λ > 1 − ∂S/|S|` (`|S|` = aire du scribble, `∂S` = son
périmètre). En pratique `1 − ∂S/|S| < 0.95` pour la quasi-totalité des traits,
d'où la constante. La règle de majorité se démontre en deux lignes à partir de là.

### Minimisation

L'énergie satisfait le modèle de **Potts** → équivalente à un problème de
**multiway cut** sur un graphe où les pixels sont les sommets, les arêtes
pixel-pixel portent `V_pq`, et les arêtes vers les terminaux couleur portent
`K − D_p(c)`.

Le graphe est **très creux** : la plupart des pixels ont un poids nul vers tous
les terminaux, donc l'arête est supprimée. Conséquence directe et élégante :
l'étiquetage résultant est **forcément connecté aux scribbles**, ce qui satisfait
la propriété d'étiquetage connexe sans avoir à la coder.

À 2 terminaux, c'est un max-flow/min-cut classique. À 3 terminaux ou plus, c'est
NP-difficile. Les auteurs proposent un algorithme glouton hiérarchique :

```
1. Initialiser l'ensemble des couleurs actives C et le masque M des pixels non étiquetés.
2. Pour chaque région non étiquetée de M ne contenant des scribbles que d'une seule
   couleur c_r : étiqueter en c_r. Si plus aucune région ne contient c_r, retirer c_r de C.
3. Si C est vide → fin.
4. Choisir une couleur c ∈ C.
5. Construire le graphe G sur les pixels non étiquetés de M.
6. Relier les pixels de couleur c à la source S, ceux des couleurs C−{c} au puits T.
7. Résoudre max-flow/min-cut (Boykov–Kolmogorov 2004).
8. Étiqueter en c les pixels affectés à S.
9. Retirer c de C, retourner en (2).
```

Moins de N coupes pour N couleurs, avec un sous-problème qui rétrécit à chaque
tour et des cas triviaux élagués.

**Performances mesurées** (2.4 GHz, 2009) : de 3× à 18× plus rapide que
l'α-expansion, pour une énergie finale identique voire meilleure. L'exemple à
0,5 Mpix passe de 11 s (α-expansion) à environ 0,6 s. Le gain croît avec le
nombre de couleurs.

Deux optimisations mentionnées :

- traiter les couleurs de la plus grande région à la plus petite (le fond en
  premier) réduit massivement les sous-problèmes dès les premières étapes ;
- par le **théorème des quatre couleurs**, si la topologie du résultat est
  prévisible, on peut regrouper les couleurs en 4 terminaux et obtenir une
  solution en un nombre **constant** de coupes, quel que soit le nombre de
  couleurs.

## 5. Limites connues (à documenter côté utilisateur)

| Cas | Symptôme | Remède |
|---|---|---|
| Ambiguïté | Deux solutions d'énergie égale ; le résultat dépend de l'ordre des étiquettes | Un scribble décisif de plus |
| Raccourcis | Un scribble trop fin dans une région à long contour troué : la coupe encercle le scribble | Pinceau plus large — le périmètre du scribble doit dépasser la somme des trous |
| Biais de majorité | Dans les "criques" fines, l'énergie basse du scribble compense le raccourci | Scribble supplémentaire |
| Faible contraste | Trait à peine plus sombre que la zone → étiquetage aberrant | Prétraitement LoG ou rehaussement non linéaire |
| Artefacts de métrication | Longs trous → frontière en escalier (norme L¹ minimisée) | Post-traitement par contour actif |

## 6. Ce que ça implique pour Animage

**Le calque CTG ne stocke pas des pixels de couleur, il stocke des scribbles.**
C'est le point d'architecture à retenir. Le rendu est *généré* à la volée à
partir de (trait source + scribbles + palette). D'où :

- on peut modifier un scribble à tout moment, la zone entière se recalcule ;
- le fichier reste minuscule comparé à un aplat raster ;
- une texture peut être appliquée par zone et suivre la déformation d'image en
  image, puisque la zone est une étiquette et non des pixels figés.

Dans le modèle de données, ça donne un `Layer { kind: CTG }` dont les `Cel`
contiennent des scribbles + une référence au calque de trait servant de source,
et un cache raster invalidé à chaque édition.

**"Onion fill" — le vrai gain de production.** Le papier montre qu'on peut
colorier **plusieurs intervalles superposés en une seule passe** : un scribble
souple assez long couvre le même détail sur 3 ou 4 dessins consécutifs. Vu le
modèle d'Animage (calques communs à toute la timeline), c'est presque gratuit à
implémenter et c'est probablement la fonctionnalité qui fera la différence face à
TVPaint.

**Le "en mieux" du document.** Trois pistes concrètes, par ordre de rapport
bénéfice/effort :

1. **Multi-source de trait.** Le CTG de TVPaint se base sur un calque de trait.
   Autoriser la combinaison de plusieurs calques comme barrière (rough + clean)
   règle une grande partie des cas de fuite.
2. **Ordre des couleurs automatique.** Le papier note le gain à traiter du plus
   grand au plus petit. En animation, la taille des zones est presque identique
   d'une image à l'autre : on peut donc la prédire depuis l'image précédente et
   accélérer beaucoup.
3. **Propagation temporelle des scribbles.** Le "patch pasting" du papier
   transfère les couleurs d'une image déjà coloriée à la suivante par mise en
   correspondance de points d'intérêt — et avec LazyBrush, la pré-segmentation
   n'est plus nécessaire. Les auteurs citent eux-mêmes l'extension au domaine
   spatio-temporel comme travail futur. C'est là qu'il y a de la place.

**Performance et interactivité.** Le max-flow se parallélise mal sur GPU. La
bonne stratégie est ailleurs :

- calculer sur une **version réduite** (½ ou ¼) pendant l'édition du scribble,
  pleine résolution en tâche de fond au relâchement ;
- recalculer **uniquement** à la modification d'un scribble, jamais au dessin sur
  un autre calque ;
- utiliser le regroupement en 4 terminaux pour borner le coût ;
- mettre en cache le résultat par cel, invalidé par un hash des scribbles.

**Point juridique à vérifier avant d'écrire une ligne :** LazyBrush a été
commercialisé (plugin payant, puis intégration TVPaint). Il faut vérifier s'il
existe un brevet déposé autour de 2008-2009 et son statut aujourd'hui. Un brevet
de cette période serait normalement expiré ou proche de l'être, mais ça se
vérifie, ça ne se suppose pas. La publication académique en elle-même
n'interdit rien : réimplémenter un algorithme publié est légal, c'est le brevet
éventuel qui pose question, pas le papier.

---

## Sources

- [LazyBrush: Flexible Painting Tool for Hand-drawn Cartoons — texte intégral (PDF)](https://scispace.com/pdf/lazybrush-flexible-painting-tool-for-hand-drawn-cartoons-1mkyl75gx2.pdf)
- [LazyBrush — Computer Graphics Forum, Wiley](https://onlinelibrary.wiley.com/doi/10.1111/j.1467-8659.2009.01400.x)
- [LazyBrush — Eurographics Digital Library](https://diglib.eg.org/handle/10.2312/CGF.v28i2pp599-608)
- [LazyBrush — plugin de colorisation pour TVPaint](http://lazy-brush.com/)
- [LazyBrush layers compatible with CTG layers ? — forum TVPaint](https://forum.tvpaint.com/viewtopic.php?f=26&t=10327)
- [CTG Layer — forum TVPaint](https://forum.tvpaint.com/viewtopic.php?t=10874)
- [Coloriser avec les calques CTG — manuel TVPaint](https://doc.tvpaint.com/docs/colorize-texturize/colorize-with-ctg-layers/application-methods)
- [Créer une texture — manuel TVPaint](https://doc.tvpaint.com/docs/colorize-texturize/apply-textures-with-ctg-layers/creating-a-texture)
