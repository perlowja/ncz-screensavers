#!/bin/bash
cd $HOME/apothecary-data
NAMES="water methane ammonia carbon-dioxide ozone hydrogen-peroxide benzene
ibuprofen acetaminophen penicillin-g morphine amoxicillin naproxen
sucrose fructose cholesterol testosterone estradiol melatonin histamine
adenosine-triphosphate urea citric-acid ascorbic-acid lactic-acid
menthol vanillin beta-carotene lycopene indigo chlorophyll-a
buckminsterfullerene cubane dodecahedrane adamantane
tetrahydrocannabinol psilocybin taurine glycine tryptophan"
for n in $NAMES; do
  [ -s "$n.sdf" ] && continue
  curl -sS --max-time 25 -o "$n.sdf" "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/$n/SDF?record_type=3d" 2>/dev/null
  sleep 0.25
done
echo FETCH_DONE
