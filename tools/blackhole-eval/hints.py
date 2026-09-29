import re,collections
rows=collections.defaultdict(dict)
for l in open("sweep_o6n.txt"):
    m=re.match(r"(\S+) (\S+) \[stats\].*steady n=(\d+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+)",l)
    if m:
        h,s,n,p50,p95,p99=m.groups(); rows[h][s]=(int(n)/7.0,float(p95))
out=["# Per-hack render scale hints for Mali-G720 (Sky1). Columns (tab separated): id, scale, measured fps at that scale, note.",
"# Measured on O6N (mali_kbase, 1080p output, 0.4.0): the smallest reduction that reaches 45 fps, from the ladder 1, 0.75, 0.5, 0.35.",
"# On a 4K Sky1 panel the harness already caps the render height at 1080, so these 1080p measurements apply to it as well.",
"# Hacks not listed run at scale 1 (they reach 45 fps at full size). A hint seeds NCZ_RENDER_SCALE; the adaptive mode may still step lower."]
tab=[]
for h,r in sorted(rows.items()):
    f1=r.get("1",(0,0))[0]
    if f1>=45: continue
    pick=None
    for s in ("0.75","0.5","0.35"):
        if s in r and r[s][0]>=45: pick=s;break
    if pick is None:
        cand=[(r[s][0],s) for s in ("0.75","0.5","0.35") if s in r]
        if not cand: continue
        pick=max(cand)[1]; note="best available, still below 45 fps"
    else: note="reaches 45 fps"
    tab.append(f"{h}\t{pick}\t{r[pick][0]:.0f}\t{note} (scale 1: {f1:.0f} fps)")
open("render-hints.tsv","w").write("\n".join(out+tab)+"\n")
print(len(tab)); print("\n".join(tab))
