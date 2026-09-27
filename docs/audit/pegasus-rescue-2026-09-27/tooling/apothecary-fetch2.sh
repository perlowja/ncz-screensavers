#!/bin/bash
cd $HOME/apothecary-data
NAMES="geosmin geraniol skatole luciferin epibatidine tubocurarine benzaldehyde
cinnamaldehyde alpha-pinene beta-pinene nepetalactone penicillin-v
deoxycholic-acid pseudouridine vasopressin
pentacene pyrazine ethylene porphyrin iron-protoporphyrin-ix
heme chlorophyll-b quinine strychnine caffeic-acid resveratrol
curcumin quercetin cocaine mescaline dmt codeine
paclitaxel artemisinin vitamin-b12 retinol ergosterol"
for n in $NAMES; do
  [ -s "$n.sdf" ] && continue
  curl -sS --max-time 25 -o "$n.sdf" "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/$n/SDF?record_type=3d" 2>/dev/null
  sleep 0.25
done
echo FETCH2_DONE
