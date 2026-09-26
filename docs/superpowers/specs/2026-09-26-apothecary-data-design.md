
# apothecary — the molecular piece (Afterimage proof of concept)

Chosen as the PoC because it is self-contained: it exercises the whole pipeline
(published data → geometry → modern materials → randomisation → acceptance
gate) without depending on the family-director architecture.

## Data sources and why

| source | licence | role | verdict |
|---|---|---|---|
| **PubChem** (NIH/NCBI) | **public domain** (US government work) | 3D conformers, drug names | **primary** |
| ChEMBL (EMBL-EBI) | CC BY-SA 3.0 | curated bioactives | secondary — usable with attribution + share-alike on the derived data asset, but it ships **2D only**, so we would have to generate conformers ourselves (RDKit ETKDG) and own the resulting geometry errors |
| ChemSpider (RSC) | API terms | — | not used |
| **RCSB PDB** | **public domain** | protein structures | primary for the protein piece |
| AlphaFold DB | CC BY 4.0 | predicted structures by UniProt accession | optional; attribution only, no share-alike |
| UniProt | CC BY 4.0 | protein **identity**, not coordinates | naming/curation only |

The earlier framing that ChEMBL was ruled out on licence grounds was wrong and
is corrected here: share-alike is an obligation we could accept. The real
reason it is secondary is that it has no 3D.

The upstream `molecules.h` bundled with the legacy hack is **not** used —
clean-room rule. Coordinates come from the authoritative source directly.

**Zero network I/O at runtime.** Everything is baked at build time.

## The selection lesson — structural scoring alone fails

First attempt scored PubChem chunk 1 (18,269 records, 12,492 renderable) by
ring count, heteroatom variety and *globularity* — a PCA of atom coordinates
where the ratio of smallest to largest eigenvalue distinguishes a genuine 3D
object from a flat sheet that renders badly from most angles. It produced 4,677
candidates and a clean top-4000.

**Then: only 1 of those 4,000 had a recognisable name.** Structural scoring
optimises geometry and selects compounds nobody has heard of. Since the piece
displays the molecule name, that set was nearly useless.

**The pipeline is therefore inverted: select from NAMED compounds, then fetch
their geometry.** `Drug-Names.tsv.gz` (858KB, 92,520 rows) yields **3,094
uniquely-named drug CIDs**. 973 fall in the first 3D chunk; 18 chunks (415MB,
already downloaded) cover 2,051; the remaining ~1,043 are scattered thinly and
are cheaper via batched REST.

Globularity and ring-count scoring are retained — but as a *ranking within the
named set*, never as the selection mechanism.

## Format

A C header does not scale: 98 molecules is already 162KB, so ~3,000 would be
~5MB and would wreck compile times. The shipped form is a packed binary asset —
3×int16 quantised coordinates plus an element byte (7 bytes/atom), uint16 bond
pairs plus order. ~3,000 molecules lands near 1.1MB, loaded at startup.

## Displaying names — an explicit carve-out

The standing rule bars readable text on a lock screen. That rule exists to stop
**private** information leaking (hostname, username, IPs, file contents). A
molecule name discloses nothing about the user or the machine.

The carve-out is therefore narrow and auditable: **the only text this piece may
render is a molecule name drawn from the embedded database.** Never a string
originating from the system. Requires a font path (the harness has
`texfont.c`).

## Proteins are a separate piece, not a mode

A small molecule is 20-70 atoms as ball-and-stick. A protein is 1,000-50,000
atoms and must be a cartoon/ribbon render driven by secondary structure — a
different geometry pipeline entirely, and the more spectacular of the two.

Verified: RCSB PDB files carry `HELIX` and `SHEET` records directly
(haemoglobin 1HHO: 2,192 atoms, 16 secondary-structure records; GFP 1EMA: 1,717
atoms), so **ribbon rendering needs no DSSP dependency** — the assignment ships
with the file. Icosahedral subjects (ferritin, virus capsids) are the obvious
showpieces.

Proposed name for that sibling piece: **chaperone**.
