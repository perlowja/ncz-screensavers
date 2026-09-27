#!/bin/bash
cd $HOME/apothecary-data
NAMES="formamide acetamide glycolaldehyde acetic-acid aminoacetonitrile
propylene-oxide ethylene-glycol indene 1-cyanonaphthalene
acenaphthylene pyrene naphthalene anthracene coronene
fullerene-c70 methanol formaldehyde acetaldehyde acetone
methylamine ethanimine cyanoacetylene glycine"
for n in $NAMES; do
  [ -s "$n.sdf" ] && continue
  curl -sS --max-time 25 -o "$n.sdf" "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/$n/SDF?record_type=3d" 2>/dev/null
  sleep 0.25
done
echo FETCH3_DONE
