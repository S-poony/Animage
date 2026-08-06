# Documents de conception (français)

Ces quatre documents sont la spécification d'origine du projet. Ils font
autorité sur les décisions d'architecture ; le code les suit, pas l'inverse.

Ils ne sont pas réécrits au fil de l'implémentation. Là où le code s'en écarte —
et il s'en écarte, toujours délibérément — une note **ajoutée après
implémentation** le signale sur place et renvoie au document anglais qui donne
les raisons et les mesures. Ce qui a été construit se raconte dans
[../handover.md](../handover.md) ; ce qui était voulu se lit ici.

| Document | Sujet |
|---|---|
| [plan-de-prototype.md](plan-de-prototype.md) | Choix techniques, structures de données, jalons M0–M5 |
| [modele-de-donnees.md](modele-de-donnees.md) | Scene / Track / Layer / Image / Cel, exposition, undo |
| [lazybrush-et-calques-ctg.md](lazybrush-et-calques-ctg.md) | L'algorithme LazyBrush, le calque CTG |
| [tablettes-couleur-licence.md](tablettes-couleur-licence.md) | Entrée stylet, espaces colorimétriques, licence |

Le reste du dépôt (code, commentaires, README, messages de commit) est en
anglais.
